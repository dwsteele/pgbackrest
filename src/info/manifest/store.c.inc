/***********************************************************************************************************************************
Store manifest data in memory
***********************************************************************************************************************************/

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
STRING_EXTERN(BACKUP_MANIFEST_FILE_STR,                             BACKUP_MANIFEST_FILE);

STRING_EXTERN(MANIFEST_TARGET_PGDATA_STR,                           MANIFEST_TARGET_PGDATA);
STRING_EXTERN(MANIFEST_TARGET_PGTBLSPC_STR,                         MANIFEST_TARGET_PGTBLSPC);

// All block incremental sizes must be divisible by this factor
#define BLOCK_INCR_SIZE_FACTOR                                      8192

/***********************************************************************************************************************************
// Initialize store
***********************************************************************************************************************************/
static void
manifestStoreInit(Manifest *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
    FUNCTION_TEST_END();

    // Prepare statements in the db context since they will exist for the lifetime of the db
    MEM_CONTEXT_OBJ_BEGIN(this->db)
    {
        sqliteExec(this->db, STRDEF("create table path(id integer primary key,name text not null unique)"));

        sqliteExec(
            this->db,
            STRDEF(
                // {uncrustify_off - indentation}
                "create table file("
                    "path_id integer not null references path (id),name text not null,flg integer,ck blob,ckR blob,mode integer,"
                    "usr text,grp text,ref integer,bndId integer,bndOff integer,biSz integer,biCkSz integer,biMapSz integer,"
                    "sz integer,szO integer,szR integer,time integer,ckPgErr text,"
                    "constraint file_pk primary key (path_id, name)"
                ")without rowid"
                // {uncrustify_on}
                ));

        this->dbPathInsertStmt = sqliteStmtNew(this->db, STRDEF("insert or ignore into path(name)values(?)"));

        // !!! THIS CAN BE IMPROVED BY SELECTING FROM PATH RATHER THAN DOING A SEPARATE QUERY
        this->dbFileInsertStmt = sqliteStmtNew(
            this->db,
            STRDEF(
                // {uncrustify_off - indentation}
                "insert into file("
                    "flg,ck,ckR,mode,usr,grp,ref,bndId,bndOff,biSz,biCkSz,biMapSz,sz,szO,szR,time,ckPgErr,name,path_id)"
                "select ?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,id "
                "from path "
                "where name=?"
                // {uncrustify_on}
                ));

        // !!! THIS UPDATE CAN IMPROVED BY USING PATH.NAME INSTEAD OF LOOKING UP ID
        this->dbFileUpdateStmt =
            sqliteStmtNew(
                this->db,
                STRDEF(
                    // {uncrustify_off - indentation}
                    "update file set "
                        "flg=?,ck=?,ckR=?,mode=?,usr=?,grp=?,ref=?,bndId=?,bndOff=?,biSz=?,biCkSz=?,biMapSz=?,sz=?,szO=?,szR=?,"
                        "time=?,ckPgErr=? "
                    "from path "
                    "where file.name=?and path.name=?and file.path_id=path.id"
                    // {uncrustify_on}
                    ));

        this->dbFileSelectStmt =
            sqliteStmtNew(
                this->db,
                STRDEF(
                    // {uncrustify_off - indentation}
                    "select "
                        "path.name||'/'||file.name,flg,ck,ckR,mode,usr,grp,ref,bndId,bndOff,biSz,biCkSz,biMapSz,sz,szO,szR,time,"
                        "ckPgErr "
                    "from file inner join path on file.path_id=path.id "
                    "where file.name=?and path.name=?"
                    // {uncrustify_on}
                    ));
    }
    MEM_CONTEXT_OBJ_END();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
// Add owner to the owner list if it is not there already and return the pointer. This saves a lot of space.
***********************************************************************************************************************************/
static const String *
manifestOwnerCache(Manifest *const this, const String *const owner)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, owner);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (owner != NULL)
        FUNCTION_TEST_RETURN_CONST(STRING, strLstAddIfMissing(this->ownerList, owner));

    FUNCTION_TEST_RETURN_CONST(STRING, NULL);
}

/***********************************************************************************************************************************
Db functions
***********************************************************************************************************************************/
static void
manifestDbAdd(Manifest *const this, const ManifestDb *const db)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(MANIFEST_DB, db);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(db != NULL);
    ASSERT(db->name != NULL);

    MEM_CONTEXT_BEGIN(lstMemContext(this->pub.dbList))
    {
        const ManifestDb dbAdd =
        {
            .id = db->id,
            .lastSystemId = db->lastSystemId,
            .name = strDup(db->name),
        };

        lstAdd(this->pub.dbList, &dbAdd);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
File functions
***********************************************************************************************************************************/
// Base time used as a delta to reduce the size of packed timestamps. This will be set on the first call to manifestFilePack().
static time_t manifestPackBaseTime = -1;

// Flags used to reduce the size of packed data. They should be ordered from most to least likely and can be reordered at will.
typedef enum
{
    manifestFilePackFlagChecksum,
    manifestFilePackFlagChecksumRepo,
    manifestFilePackFlagReference,
    manifestFilePackFlagBundle,
    manifestFilePackFlagBlockIncr,
    manifestFilePackFlagCopy,
    manifestFilePackFlagDelta,
    manifestFilePackFlagResume,
    manifestFilePackFlagChecksumPage,
    manifestFilePackFlagChecksumPageError,
    manifestFilePackFlagChecksumPageErrorList,
    manifestFilePackFlagSizeOriginal,
    manifestFilePackFlagMode,
    manifestFilePackFlagUser,
    manifestFilePackFlagUserNull,
    manifestFilePackFlagGroup,
    manifestFilePackFlagGroupNull,
} ManifestFilePackFlag;

// Pack file into a compact format to save memory
static ManifestFilePack *
manifestFilePack(const Manifest *const manifest, const ManifestFile *const file)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, manifest);
        FUNCTION_TEST_PARAM(MANIFEST_FILE, file);
    FUNCTION_TEST_END();

    ASSERT(manifest != NULL);
    ASSERT(file != NULL);

    uint8_t buffer[512];
    size_t bufferPos = 0;

    // Flags
    uint64_t flag = 0;

    if (file->checksumSha1 != NULL)
        flag |= 1 << manifestFilePackFlagChecksum;

    if (file->checksumRepoSha1 != NULL)
        flag |= 1 << manifestFilePackFlagChecksumRepo;

    if (file->copy)
        flag |= 1 << manifestFilePackFlagCopy;

    if (file->delta)
        flag |= 1 << manifestFilePackFlagDelta;

    if (file->resume)
        flag |= 1 << manifestFilePackFlagResume;

    if (file->checksumPage)
        flag |= 1 << manifestFilePackFlagChecksumPage;

    if (file->checksumPageError)
        flag |= 1 << manifestFilePackFlagChecksumPageError;

    if (file->checksumPageErrorList != NULL)
        flag |= 1 << manifestFilePackFlagChecksumPageErrorList;

    if (file->reference != NULL)
        flag |= 1 << manifestFilePackFlagReference;

    if (file->bundleId != 0)
        flag |= 1 << manifestFilePackFlagBundle;

    if (file->blockIncrSize != 0)
        flag |= 1 << manifestFilePackFlagBlockIncr;

    if (file->sizeOriginal != file->size)
        flag |= 1 << manifestFilePackFlagSizeOriginal;

    if (file->mode != manifest->fileModeDefault)
        flag |= 1 << manifestFilePackFlagMode;

    if (file->user == NULL)
        flag |= 1 << manifestFilePackFlagUserNull;
    else if (!strEq(file->user, manifest->fileUserDefault))
        flag |= 1 << manifestFilePackFlagUser;

    if (file->group == NULL)
        flag |= 1 << manifestFilePackFlagGroupNull;
    else if (!strEq(file->group, manifest->fileGroupDefault))
        flag |= 1 << manifestFilePackFlagGroup;

    cvtUInt64ToVarInt128(flag, buffer, &bufferPos, sizeof(buffer));

    // Size
    cvtUInt64ToVarInt128(file->size, buffer, &bufferPos, sizeof(buffer));

    // Original size
    if (flag & (1 << manifestFilePackFlagSizeOriginal))
        cvtUInt64ToVarInt128(file->sizeOriginal, buffer, &bufferPos, sizeof(buffer));

    // Use the first timestamp that appears as the base for all other timestamps. Ideally we would like a timestamp as close to the
    // middle as possible but it doesn't seem worth doing the calculation.
    if (manifestPackBaseTime == -1)
        manifestPackBaseTime = file->timestamp;

    // Timestamp
    cvtUInt64ToVarInt128(cvtInt64ToZigZag(manifestPackBaseTime - file->timestamp), buffer, &bufferPos, sizeof(buffer));

    // SHA1 checksum
    if (file->checksumSha1 != NULL)
    {
        memcpy((uint8_t *)buffer + bufferPos, file->checksumSha1, HASH_TYPE_SHA1_SIZE);
        bufferPos += HASH_TYPE_SHA1_SIZE;
    }

    // SHA1 repo checksum
    if (file->checksumRepoSha1 != NULL)
    {
        memcpy((uint8_t *)buffer + bufferPos, file->checksumRepoSha1, HASH_TYPE_SHA1_SIZE);
        bufferPos += HASH_TYPE_SHA1_SIZE;
    }

    // Reference
    if (file->reference != NULL)
    {
        cvtUInt64ToVarInt128(
            strLstFindIdxP(manifest->pub.referenceList, file->reference, .required = true), buffer, &bufferPos, sizeof(buffer));
    }

    // Mode
    if (flag & (1 << manifestFilePackFlagMode))
        cvtUInt64ToVarInt128(file->mode, buffer, &bufferPos, sizeof(buffer));

    // User/group
    if (flag & (1 << manifestFilePackFlagUser))
        cvtUInt64ToVarInt128((uintptr_t)file->user, buffer, &bufferPos, sizeof(buffer));

    if (flag & (1 << manifestFilePackFlagGroup))
        cvtUInt64ToVarInt128((uintptr_t)file->group, buffer, &bufferPos, sizeof(buffer));

    // Repo size
    cvtUInt64ToVarInt128(file->sizeRepo, buffer, &bufferPos, sizeof(buffer));

    // Bundle
    if (flag & (1 << manifestFilePackFlagBundle))
    {
        cvtUInt64ToVarInt128(file->bundleId, buffer, &bufferPos, sizeof(buffer));
        cvtUInt64ToVarInt128(file->bundleOffset, buffer, &bufferPos, sizeof(buffer));
    }

    // Block incremental
    if (flag & (1 << manifestFilePackFlagBlockIncr))
    {
        ASSERT(file->blockIncrSize % BLOCK_INCR_SIZE_FACTOR == 0);

        cvtUInt64ToVarInt128(file->blockIncrSize / BLOCK_INCR_SIZE_FACTOR, buffer, &bufferPos, sizeof(buffer));
        cvtUInt64ToVarInt128(file->blockIncrChecksumSize, buffer, &bufferPos, sizeof(buffer));
        cvtUInt64ToVarInt128(file->blockIncrMapSize, buffer, &bufferPos, sizeof(buffer));
    }

    // Allocate memory for the file pack
    const size_t nameSize = strSize(file->name) + 1;

    uint8_t *const result = memNew(
        sizeof(StringPub) + nameSize + bufferPos +
        (file->checksumPageErrorList != NULL ?
             ALIGN_OFFSET(StringPub, nameSize + bufferPos) + sizeof(StringPub) + strSize(file->checksumPageErrorList) + 1 : 0));

    // Create string object for the file name
    *(StringPub *)result = (StringPub){.size = (unsigned int)strSize(file->name), .buffer = (char *)result + sizeof(StringPub)};
    size_t resultPos = sizeof(StringPub);

    memcpy(result + resultPos, (const uint8_t *)strZ(file->name), nameSize);
    resultPos += nameSize;

    // Copy pack data
    memcpy(result + resultPos, buffer, bufferPos);

    // Create string object for the checksum error list
    if (file->checksumPageErrorList != NULL)
    {
        resultPos += bufferPos + ALIGN_OFFSET(StringPub, nameSize + bufferPos);

        *(StringPub *)(result + resultPos) = (StringPub)
        {
            .size = (unsigned int)strSize(file->checksumPageErrorList),
            .buffer = (char *)result + resultPos + sizeof(StringPub),
        };

        resultPos += sizeof(StringPub);
        memcpy(result + resultPos, (const uint8_t *)strZ(file->checksumPageErrorList), strSize(file->checksumPageErrorList) + 1);
    }

    FUNCTION_TEST_RETURN_TYPE_P(ManifestFilePack, (ManifestFilePack *)result);
}

FN_EXTERN ManifestFile
manifestFileUnpack(const Manifest *const manifest, const ManifestFilePack *const filePack)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, manifest);
        FUNCTION_TEST_PARAM_P(VOID, filePack);
    FUNCTION_TEST_END();

    ASSERT(filePack != NULL);
    ASSERT(manifestPackBaseTime != -1);

    ManifestFile result = {0};
    size_t bufferPos = 0;

    // Name
    result.name = (const String *)filePack;
    bufferPos += sizeof(StringPub) + strSize(result.name) + 1;

    // Flags
    const uint64_t flag = cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);

    result.copy = (flag >> manifestFilePackFlagCopy) & 1;
    result.delta = (flag >> manifestFilePackFlagDelta) & 1;
    result.resume = (flag >> manifestFilePackFlagResume) & 1;

    // Size
    result.size = cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);

    // Original size
    if (flag & (1 << manifestFilePackFlagSizeOriginal))
        result.sizeOriginal = cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);
    else
        result.sizeOriginal = result.size;

    // Timestamp
    result.timestamp =
        manifestPackBaseTime - (time_t)cvtInt64FromZigZag(cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX));

    // Checksum page
    result.checksumPage = (flag >> manifestFilePackFlagChecksumPage) & 1;

    // SHA1 checksum
    if (flag & (1 << manifestFilePackFlagChecksum))
    {
        result.checksumSha1 = (const uint8_t *)filePack + bufferPos;
        bufferPos += HASH_TYPE_SHA1_SIZE;
    }

    // SHA1 repo checksum
    if (flag & (1 << manifestFilePackFlagChecksumRepo))
    {
        result.checksumRepoSha1 = (const uint8_t *)filePack + bufferPos;
        bufferPos += HASH_TYPE_SHA1_SIZE;
    }

    // Reference
    if (flag & (1 << manifestFilePackFlagReference))
    {
        result.reference = strLstGet(
            manifest->pub.referenceList, (unsigned int)cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX));
    }

    // Mode
    if (flag & (1 << manifestFilePackFlagMode))
        result.mode = (mode_t)cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);
    else
        result.mode = manifest->fileModeDefault;

    // User/group
    if (flag & (1 << manifestFilePackFlagUser))
        result.user = (const String *)(uintptr_t)cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);
    else if (!(flag & (1 << manifestFilePackFlagUserNull)))
        result.user = manifest->fileUserDefault;

    if (flag & (1 << manifestFilePackFlagGroup))
        result.group = (const String *)(uintptr_t)cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);
    else if (!(flag & (1 << manifestFilePackFlagGroupNull)))
        result.group = manifest->fileGroupDefault;

    // Repo size
    result.sizeRepo = cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);

    // Bundle
    if (flag & (1 << manifestFilePackFlagBundle))
    {
        result.bundleId = cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);
        result.bundleOffset = cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);
    }

    // Block incremental
    if (flag & (1 << manifestFilePackFlagBlockIncr))
    {
        result.blockIncrSize =
            (size_t)cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX) * BLOCK_INCR_SIZE_FACTOR;
        result.blockIncrChecksumSize = (size_t)cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);
        result.blockIncrMapSize = cvtUInt64FromVarInt128((const uint8_t *)filePack, &bufferPos, UINT_MAX);
    }

    // Checksum page error
    result.checksumPageError = flag & (1 << manifestFilePackFlagChecksumPageError) ? true : false;

    if (flag & (1 << manifestFilePackFlagChecksumPageErrorList))
        result.checksumPageErrorList = (const String *)((const uint8_t *)filePack + bufferPos + ALIGN_OFFSET(StringPub, bufferPos));

    FUNCTION_TEST_RETURN_TYPE(ManifestFile, result);
}

// !!! In order of how common the flag is
typedef enum
{
    manifestStoreFlagCopy,
    manifestStoreFlagDelta,
    manifestStoreFlagChecksumPage,
    manifestStoreFlagResume,
    manifestStoreFlagChecksumPageError,
    manifestStoreFlagUserNull,
    manifestStoreFlagGroupNull,
} ManifestStoreFlag;

static uint64_t
manifestFileAddUpdate(Manifest *const this, const ManifestFile *const file, SqliteStmt *const stmt)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(MANIFEST_FILE, file);
        FUNCTION_TEST_PARAM(SQLITE_STMT, stmt);
    FUNCTION_TEST_END();

    uint64_t result = 0;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Insert path if it does not already exist
        sqliteStmtBindStr(this->dbPathInsertStmt, 1, strPath(file->name));
        sqliteStmtExec(this->dbPathInsertStmt);
        sqliteStmtReset(this->dbPathInsertStmt);

        // !!! THIS COULD BE IMPROVED BY CHECKING BACKUP TYPE
        unsigned int flag = 0;

        if (!file->copy)
            flag |= 1 << manifestStoreFlagCopy;

        if (file->delta)
            flag |= 1 << manifestStoreFlagDelta;

        if (file->checksumPage != this->checksumPageDefault)
            flag |= 1 << manifestStoreFlagChecksumPage;

        if (file->resume)
            flag |= 1 << manifestStoreFlagResume;

        if (file->checksumPageError)
            flag |= 1 << manifestStoreFlagChecksumPageError;

        if (file->user == NULL)
            flag |= 1 << manifestStoreFlagUserNull;

        if (file->group == NULL)
            flag |= 1 << manifestStoreFlagGroupNull;

        sqliteStmtBindUInt(stmt, 1, flag);

        if (file->checksumSha1 != NULL)
            sqliteStmtBindBuf(stmt, 2, BUF(file->checksumSha1, HASH_TYPE_SHA1_SIZE));

        if (file->checksumRepoSha1 != NULL)
            sqliteStmtBindBuf(stmt, 3, BUF(file->checksumRepoSha1, HASH_TYPE_SHA1_SIZE));

        if (file->mode != this->fileModeDefault)
            sqliteStmtBindUInt(stmt, 4, file->mode);

        if (file->user != NULL && !strEq(file->user, this->fileUserDefault))
            sqliteStmtBindStr(stmt, 5, file->user);

        if (file->group != NULL && !strEq(file->group, this->fileGroupDefault))
            sqliteStmtBindStr(stmt, 6, file->group);

        if (file->reference != NULL)
            sqliteStmtBindUInt(stmt, 7, strLstFindIdxP(this->pub.referenceList, file->reference, .required = true));

        if (file->bundleId != 0)
        {
            sqliteStmtBindU63P(stmt, 8, file->bundleId);

            if (file->bundleOffset != 0)
                sqliteStmtBindU63P(stmt, 9, file->bundleOffset);
        }

        if (file->blockIncrSize != 0)
        {
            ASSERT(file->blockIncrSize % BLOCK_INCR_SIZE_FACTOR == 0);

            sqliteStmtBindU63P(stmt, 10, file->blockIncrSize / 8192);

            if (file->blockIncrChecksumSize != BLOCK_INCR_CHECKSUM_SIZE_MIN)
                sqliteStmtBindU63P(stmt, 11, file->blockIncrChecksumSize);

            sqliteStmtBindU63P(stmt, 12, file->blockIncrMapSize);
        }

        if (file->size != 0)
            sqliteStmtBindU63P(stmt, 13, file->size);

        if (file->sizeOriginal != file->size)
            sqliteStmtBindU63P(stmt, 14, file->sizeOriginal);

        if (file->sizeRepo - file->blockIncrMapSize != file->size)
            sqliteStmtBindU63P(stmt, 15, file->sizeRepo - file->blockIncrMapSize);

        ASSERT(this->pub.data.backupTimestampStart != 0);
        sqliteStmtBindI64P(stmt, 16, (int64_t)(this->pub.data.backupTimestampStart - file->timestamp));

        // !!! MIGHT NOT NEED THE IF HERE
        if (file->checksumPageErrorList != NULL)
            sqliteStmtBindStr(stmt, 17, file->checksumPageErrorList);

        sqliteStmtBindStr(stmt, 18, strBase(file->name));
        sqliteStmtBindStr(stmt, 19, strPath(file->name));
        sqliteStmtNext(stmt);

        CHECK_FMT(
            DbQueryError, sqliteStmtChanged(stmt) == 1, "unable %s %s", stmt == this->dbFileInsertStmt ? "insert" : "update",
            strZ(file->name));

        sqliteStmtReset(stmt);

        // SqliteStmt *stmt = sqliteStmtNew(this->db, STRDEF("select * from path order by name"));
        // fprintf(stdout, "!!!FILE:\n%s\n", strZ(hrnSqliteStmtToStr(stmt)));
        // stmt = sqliteStmtNew(this->db, STRDEF("select * from file order by path_id, name"));
        // fprintf(stdout, "!!!FILE:\n%s\n", strZ(hrnSqliteStmtToStr(stmt)));
        // fflush(stdout);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_TEST_RETURN(UINT64, result);
}

FN_EXTERN ManifestFile
manifestFile(const Manifest *const this, const unsigned int fileIdx)
{
    // !!! THIS FUNCTION WILL NOT REMAIN
    ManifestFile file = manifestFileUnpack(this, manifestFilePackGet(this, fileIdx));

    MEM_CONTEXT_TEMP_BEGIN()
    {
        sqliteStmtBindStr(this->dbFileSelectStmt, 1, strBase(file.name));
        sqliteStmtBindStr(this->dbFileSelectStmt, 2, strPath(file.name));
        CHECK_FMT(AssertError, sqliteStmtNext(this->dbFileSelectStmt), "unable to find file '%s'", strZ(file.name));

        const String *const name = sqliteStmtStr(this->dbFileSelectStmt, 1);
        CHECK(AssertError, strEq(file.name, name), "file.name is not equal");
        const unsigned int flag = sqliteStmtUIntP(this->dbFileSelectStmt, 2);

        CHECK(
            AssertError, file.copy == !(flag & (1 << manifestStoreFlagCopy)),
            "file.copy does not equal db.copy");
        CHECK(
            AssertError, file.delta == (bool)(flag & (1 << manifestStoreFlagDelta)),
            "file.delta does not equal db.delta");
        CHECK(
            AssertError, file.resume == (bool)(flag & (1 << manifestStoreFlagResume)),
            "file.resume does not equal db.resume");
        CHECK(
            AssertError,
            file.checksumPage == (bool)(flag & (1 << manifestStoreFlagChecksumPage)) ?
                !this->checksumPageDefault : this->checksumPageDefault,
            "file.checksumPage does not equal db.checksumPage");
        CHECK(
            AssertError,
            (file.checksumSha1 == NULL && sqliteStmtBuf(this->dbFileSelectStmt, 3) == NULL) ||
            (file.checksumSha1 != NULL && sqliteStmtBuf(this->dbFileSelectStmt, 3) != NULL &&
             bufEq(BUF(file.checksumSha1, HASH_TYPE_SHA1_SIZE), sqliteStmtBuf(this->dbFileSelectStmt, 3))),
            "file.checksum does not equal db.checksum");
        CHECK(
            AssertError,
            (file.checksumRepoSha1 == NULL && sqliteStmtBuf(this->dbFileSelectStmt, 4) == NULL) ||
            (file.checksumRepoSha1 != NULL && sqliteStmtBuf(this->dbFileSelectStmt, 4) != NULL &&
             bufEq(BUF(file.checksumRepoSha1, HASH_TYPE_SHA1_SIZE), sqliteStmtBuf(this->dbFileSelectStmt, 4))),
            "file.checksum does not equal db.checksumRepo");

        const mode_t mode =
            sqliteStmtNull(this->dbFileSelectStmt, 5) ? this->fileModeDefault : sqliteStmtUIntP(this->dbFileSelectStmt, 5);
        CHECK(AssertError, file.mode == mode, "file.mode does not equal db.mode");

        const String *const user =
            (bool)(flag & (1 << manifestStoreFlagUserNull)) ?
                NULL :
                (sqliteStmtNull(this->dbFileSelectStmt, 6) ? this->fileUserDefault : sqliteStmtStr(this->dbFileSelectStmt, 6));
        CHECK(AssertError, strEq(file.user, user), "file.user not equal");

        const String *const group =
            (bool)(flag & (1 << manifestStoreFlagGroupNull)) ?
                NULL :
                (sqliteStmtNull(this->dbFileSelectStmt, 7) ? this->fileGroupDefault : sqliteStmtStr(this->dbFileSelectStmt, 7));
        CHECK(AssertError, strEq(file.group, group), "file.group not equal");

        const String *const reference =
            sqliteStmtNull(this->dbFileSelectStmt, 8) ?
                NULL : strLstGet(this->pub.referenceList, sqliteStmtUIntP(this->dbFileSelectStmt, 8));
        CHECK(AssertError, strEq(file.reference, reference), "file.reference not equal");

        CHECK(AssertError, file.bundleId == sqliteStmtU63P(this->dbFileSelectStmt, 9), "bundleId is not equal");
        CHECK(AssertError, file.bundleOffset == sqliteStmtU63P(this->dbFileSelectStmt, 10), "bundleOffset is not equal");
        CHECK(AssertError, file.blockIncrSize == sqliteStmtU63P(this->dbFileSelectStmt, 11) * 8192, "blockIncrSize is not equal");

        const uint64_t blockIncrChecksumSize =
            file.blockIncrSize == 0 ? 0 : sqliteStmtU63P(this->dbFileSelectStmt, 12, .defaultValue = BLOCK_INCR_CHECKSUM_SIZE_MIN);
        CHECK(AssertError, file.blockIncrChecksumSize == blockIncrChecksumSize, "blockIncrChecksumSize is not equal");
        const uint64_t blockIncrMapSize = file.blockIncrSize == 0 ? 0 : sqliteStmtU63P(this->dbFileSelectStmt, 13);
        CHECK(AssertError, file.blockIncrMapSize == blockIncrMapSize, "blockIncrMapSize is not equal");
        CHECK(AssertError, file.size == sqliteStmtU63P(this->dbFileSelectStmt, 14), "size is not equal");
        CHECK(
            AssertError,
            file.sizeOriginal == sqliteStmtU63P(
                this->dbFileSelectStmt, 15, .defaultValue = sqliteStmtU63P(this->dbFileSelectStmt, 14)),
            "sizeOriginal is not equal");
        CHECK(
            AssertError,
            file.sizeRepo == sqliteStmtU63P(
                this->dbFileSelectStmt, 16, .defaultValue = sqliteStmtU63P(this->dbFileSelectStmt, 14)) + blockIncrMapSize,
            "sizeRepo is not equal");
        CHECK(
            AssertError, file.timestamp == this->pub.data.backupTimestampStart - sqliteStmtI64P(this->dbFileSelectStmt, 17),
            "timestamp is not equal");

        CHECK(
            AssertError, file.checksumPageError == (bool)(flag & (1 << manifestStoreFlagChecksumPageError)),
            "checksumPageError is not equal");
        CHECK(
            AssertError, strEq(file.checksumPageErrorList, sqliteStmtStr(this->dbFileSelectStmt, 18)),
            "checksumPageErrorList is not equal");

        sqliteStmtReset(this->dbFileSelectStmt);
    }
    MEM_CONTEXT_TEMP_END();

    return file;
}

FN_EXTERN void
manifestFileAdd(Manifest *const this, ManifestFile *const file)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(MANIFEST_FILE, file);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(file->name != NULL);

    file->user = manifestOwnerCache(this, file->user);
    file->group = manifestOwnerCache(this, file->group);

    MEM_CONTEXT_BEGIN(lstMemContext(this->pub.fileList))
    {
        manifestFileAddUpdate(this, file, this->dbFileInsertStmt);

        const ManifestFilePack *const filePack = manifestFilePack(this, file);
        lstAdd(this->pub.fileList, &filePack);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

// Update file pack by creating a new one and then freeing the old one
static void
manifestFilePackUpdate(Manifest *const this, ManifestFilePack **const filePack, const ManifestFile *const file)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM_P(VOID, filePack);
        FUNCTION_TEST_PARAM(MANIFEST_FILE, file);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(filePack != NULL);
    ASSERT(file != NULL);

    MEM_CONTEXT_BEGIN(lstMemContext(this->pub.fileList))
    {
        ManifestFilePack *const filePackOld = *filePack;
        *filePack = manifestFilePack(this, file);

        manifestFileAddUpdate(this, file, this->dbFileUpdateStmt);
        memFree(filePackOld);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

static ManifestFilePack **
manifestFilePackFindInternal(const Manifest *const this, const String *const name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, name);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(name != NULL);

    ManifestFilePack **const filePack = lstFind(this->pub.fileList, &name);

    if (filePack == NULL)
        THROW_FMT(AssertError, "unable to find '%s' in manifest file list", strZ(name));

    FUNCTION_TEST_RETURN_TYPE_PP(ManifestFilePack, filePack);
}

const ManifestFilePack *
manifestFilePackFind(const Manifest *const this, const String *const name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, name);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(name != NULL);

    FUNCTION_TEST_RETURN_TYPE_P(ManifestFilePack, *manifestFilePackFindInternal(this, name));
}

FN_EXTERN void
manifestFileRemove(const Manifest *const this, const String *const name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, name);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(name != NULL);

    if (!lstRemove(this->pub.fileList, &name))
        THROW_FMT(AssertError, "unable to remove '%s' from manifest file list", strZ(name));

    FUNCTION_TEST_RETURN_VOID();
}

FN_EXTERN void
manifestFileUpdate(Manifest *const this, const ManifestFile *const file)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(MANIFEST_FILE, file);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(file != NULL);
    ASSERT(
        (!file->checksumPage && !file->checksumPageError && file->checksumPageErrorList == NULL) ||
        (file->checksumPage && !file->checksumPageError && file->checksumPageErrorList == NULL) ||
        (file->checksumPage && file->checksumPageError));
    ASSERT(file->size != 0 || (file->bundleId == 0 && file->bundleOffset == 0));

    ManifestFilePack **const filePack = manifestFilePackFindInternal(this, file->name);
    manifestFilePackUpdate(this, filePack, file);

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Link functions
***********************************************************************************************************************************/
FN_EXTERN void
manifestLinkAdd(Manifest *const this, const ManifestLink *const link)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(MANIFEST_LINK, link);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(link != NULL);
    ASSERT(link->name != NULL);
    ASSERT(link->destination != NULL);

    MEM_CONTEXT_BEGIN(lstMemContext(this->pub.linkList))
    {
        const ManifestLink linkAdd =
        {
            .destination = strDup(link->destination),
            .name = strDup(link->name),
            .group = manifestOwnerCache(this, link->group),
            .user = manifestOwnerCache(this, link->user),
        };

        lstAdd(this->pub.linkList, &linkAdd);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

FN_EXTERN ManifestLink *
manifestLinkFind(const Manifest *const this, const String *const name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, name);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(name != NULL);

    ManifestLink *const result = lstFind(this->pub.linkList, &name);

    if (result == NULL)
        THROW_FMT(AssertError, "unable to find '%s' in manifest link list", strZ(name));

    FUNCTION_TEST_RETURN(MANIFEST_LINK, result);
}

FN_EXTERN void
manifestLinkRemove(const Manifest *const this, const String *const name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, name);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(name != NULL);

    if (!lstRemove(this->pub.linkList, &name))
        THROW_FMT(AssertError, "unable to remove '%s' from manifest link list", strZ(name));

    FUNCTION_TEST_RETURN_VOID();
}

FN_EXTERN void
manifestLinkUpdate(const Manifest *const this, const String *const name, const String *const destination)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, name);
        FUNCTION_TEST_PARAM(STRING, destination);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(name != NULL);
    ASSERT(destination != NULL);

    ManifestLink *const link = (ManifestLink *const)manifestLinkFind(this, name);

    MEM_CONTEXT_BEGIN(lstMemContext(this->pub.linkList))
    {
        if (!strEq(link->destination, destination))
            link->destination = strDup(destination);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Path functions
***********************************************************************************************************************************/
static void
manifestPathAdd(Manifest *const this, const ManifestPath *const path)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(MANIFEST_PATH, path);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(path != NULL);
    ASSERT(path->name != NULL);

    MEM_CONTEXT_BEGIN(lstMemContext(this->pub.pathList))
    {
        const ManifestPath pathAdd =
        {
            .mode = path->mode,
            .name = strDup(path->name),
            .group = manifestOwnerCache(this, path->group),
            .user = manifestOwnerCache(this, path->user),
        };

        lstAdd(this->pub.pathList, &pathAdd);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

FN_EXTERN const ManifestPath *
manifestPathFind(const Manifest *const this, const String *const name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, name);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(name != NULL);

    const ManifestPath *const result = lstFind(this->pub.pathList, &name);

    if (result == NULL)
        THROW_FMT(AssertError, "unable to find '%s' in manifest path list", strZ(name));

    FUNCTION_TEST_RETURN_CONST(MANIFEST_PATH, result);
}

/***********************************************************************************************************************************
Manifest functions
***********************************************************************************************************************************/
FN_EXTERN void
manifestTargetAdd(Manifest *const this, const ManifestTarget *const target)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(MANIFEST_TARGET, target);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(target != NULL);
    ASSERT(target->path != NULL);
    ASSERT(target->name != NULL);

    MEM_CONTEXT_BEGIN(lstMemContext(this->pub.targetList))
    {
        const ManifestTarget targetAdd =
        {
            .file = strDup(target->file),
            .name = strDup(target->name),
            .path = strDup(target->path),
            .tablespaceId = target->tablespaceId,
            .tablespaceName = strDup(target->tablespaceName),
            .type = target->type,
        };

        lstAdd(this->pub.targetList, &targetAdd);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}

FN_EXTERN ManifestTarget *
manifestTargetFind(const Manifest *const this, const String *const name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, name);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(name != NULL);

    ManifestTarget *const result = lstFind(this->pub.targetList, &name);

    if (result == NULL)
        THROW_FMT(AssertError, "unable to find '%s' in manifest target list", strZ(name));

    FUNCTION_TEST_RETURN(MANIFEST_TARGET, result);
}

FN_EXTERN String *
manifestTargetPath(const Manifest *const this, const ManifestTarget *const target)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(MANIFEST_TARGET, target);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(target != NULL);

    // If the target path is already absolute then just return it
    if (strBeginsWith(target->path, FSLASH_STR))
        FUNCTION_TEST_RETURN(STRING, strDup(target->path));

    // Construct it from the base pg path and a relative path
    String *result;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        String *const pgPath = strCat(strNew(), strPath(manifestPathPg(target->name)));

        if (strSize(pgPath) != 0)
            strCatZ(pgPath, "/");

        strCat(pgPath, target->path);

        MEM_CONTEXT_PRIOR_BEGIN()
        {
            result = strPathAbsolute(pgPath, manifestTargetBase(this)->path);
        }
        MEM_CONTEXT_PRIOR_END();
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_TEST_RETURN(STRING, result);
}

FN_EXTERN void
manifestTargetRemove(const Manifest *const this, const String *const name)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, name);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(name != NULL);

    if (!lstRemove(this->pub.targetList, &name))
        THROW_FMT(AssertError, "unable to remove '%s' from manifest target list", strZ(name));

    FUNCTION_TEST_RETURN_VOID();
}

FN_EXTERN void
manifestTargetUpdate(const Manifest *const this, const String *const name, const String *const path, const String *const file)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(MANIFEST, this);
        FUNCTION_TEST_PARAM(STRING, name);
        FUNCTION_TEST_PARAM(STRING, path);
        FUNCTION_TEST_PARAM(STRING, file);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(name != NULL);
    ASSERT(path != NULL);

    ManifestTarget *const target = (ManifestTarget *)manifestTargetFind(this, name);

    ASSERT((target->file == NULL && file == NULL) || (target->file != NULL && file != NULL));

    MEM_CONTEXT_BEGIN(lstMemContext(this->pub.targetList))
    {
        if (!strEq(target->path, path))
            target->path = strDup(path);

        if (!strEq(target->file, file))
            target->file = strDup(file);
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN_VOID();
}
