/***********************************************************************************************************************************
Backup Protocol Handler
***********************************************************************************************************************************/
#include "build.auto.h"

#include "command/backup/file.h"
#include "command/backup/protocol.h"
#include "common/crypto/hash.h"
#include "common/debug.h"
#include "common/io/io.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/type/json.h"
#include "config/config.h"
#include "storage/helper.h"

/***********************************************************************************************************************************
Sort files for efficient processing
***********************************************************************************************************************************/
static int
backupFileComparator(const void *const item1, const void *const item2)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM_P(VOID, item1);
        FUNCTION_TEST_PARAM_P(VOID, item2);
    FUNCTION_TEST_END();

    ASSERT(item1 != NULL);
    ASSERT(item2 != NULL);

    const BackupFile *const file1 = item1;
    const BackupFile *const file2 = item2;

    // Order pg_control at the end in debug builds for reproducibility. Since pg_control varies by architecture the compressed size
    // may be different and cause bundle offsets to vary.
#ifdef DEBUG
    if (strEqZ(file1->pgFile, PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL))
        FUNCTION_TEST_RETURN(INT, 1);
    else if (strEqZ(file2->pgFile, PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL))
        FUNCTION_TEST_RETURN(INT, -1);
#endif

    // First order block incremental files before whole files. We want the whole files to be next to the block maps at the end of
    // the bundle so they can be read out together during restore. This means for restore of the full backup the whole/block map
    // and block list scans will both be sequential with no gaps.
    if (file1->blockIncrSize != 0 && file2->blockIncrSize == 0)
        FUNCTION_TEST_RETURN(INT, -1);
    else if (file1->blockIncrSize == 0 && file2->blockIncrSize != 0)
        FUNCTION_TEST_RETURN(INT, 1);

    // Next order by size desc so small files are stored together and near the block maps (also small) which makes reads more likely
    // to be efficient with read over
    if (file1->pgFileSize < file2->pgFileSize)
        FUNCTION_TEST_RETURN(INT, 1);
    else if (file1->pgFileSize > file2->pgFileSize)
        FUNCTION_TEST_RETURN(INT, -1);

    // If block incremental/size are the same then use name desc to generate a deterministic ordering (names must be unique)
    FUNCTION_TEST_RETURN(INT, strCmp(file2->pgFile, file1->pgFile));
}

/**********************************************************************************************************************************/
FN_EXTERN ProtocolServerResult *
backupFileProtocol(PackRead *const param)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(PACK_READ, param);
    FUNCTION_LOG_END();

    ASSERT(param != NULL);

    ProtocolServerResult *const result = protocolServerResultNewP();

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Backup options that apply to all files
        const String *const repoFile = pckReadStrP(param);
        const uint64_t bundleId = pckReadU64P(param);
        const bool bundleRaw = bundleId != 0 ? pckReadBoolP(param) : false;
        const unsigned int blockIncrReference = (unsigned int)pckReadU64P(param);
        const CompressType repoFileCompressType = (CompressType)pckReadU32P(param);
        const int repoFileCompressLevel = pckReadI32P(param);
        const CipherType cipherType = (CipherType)pckReadU64P(param);
        const String *const cipherPass = pckReadStrP(param);
        const PgPageSize pageSize = pckReadU32P(param);
        const String *const pgVersionForce = pckReadStrP(param);
        const BlockIncrMapPosition blockIncrMapPos = (BlockIncrMapPosition)pckReadU32P(param);

        // Build the file list
        List *const fileList = lstNewP(sizeof(BackupFile), .comparator = backupFileComparator);

        while (!pckReadNullP(param))
        {
            BackupFile file = {.pgFile = pckReadStrP(param)};
            file.pgFileDelta = pckReadBoolP(param);
            file.pgFileIgnoreMissing = pckReadBoolP(param);
            file.pgFileSize = pckReadU64P(param);
            file.pgFileSizeOriginal = pckReadU64P(param);
            file.pgFileCopyExactSize = pckReadBoolP(param);
            file.pgFileChecksum = pckReadBinP(param);
            file.pgFileChecksumPage = pckReadBoolP(param);
            file.pgFilePageHeaderCheck = pckReadBoolP(param);
            file.blockIncrSize = (size_t)pckReadU64P(param);

            if (file.blockIncrSize > 0)
            {
                file.blockIncrChecksumSize = (size_t)pckReadU64P(param);
                file.blockIncrSuperSize = pckReadU64P(param);
                file.blockIncrMapPriorFile = pckReadStrP(param);

                if (file.blockIncrMapPriorFile != NULL)
                {
                    file.blockIncrMapPriorOffset = pckReadU64P(param);
                    file.blockIncrMapPriorSize = pckReadU64P(param);
                }
            }

            file.manifestFile = pckReadStrP(param);
            file.repoFileChecksum = pckReadBinP(param);
            file.repoFileSize = pckReadU64P(param);
            file.manifestFileResume = pckReadBoolP(param);
            file.manifestFileHasReference = pckReadBoolP(param);

            lstAdd(fileList, &file);
        }

        // Sort files for efficient processing
        lstSort(fileList, sortOrderAsc);

        // Backup file
        const List *const resultList = backupFile(
            repoFile, bundleId, bundleRaw, blockIncrMapPos, blockIncrReference, repoFileCompressType, repoFileCompressLevel,
            cipherType, cipherPass, pgVersionForce, pageSize, fileList);

        // Return result
        PackWrite *const data = protocolServerResultData(result);

        for (unsigned int resultIdx = 0; resultIdx < lstSize(resultList); resultIdx++)
        {
            const BackupFileResult *const fileResult = lstGet(resultList, resultIdx);

            ASSERT(
                fileResult->backupCopyResult == backupCopyResultSkip || fileResult->copySize != 0 ||
                bufEq(fileResult->copyChecksum, HASH_TYPE_SHA1_ZERO_BUF));

            pckWriteStrP(data, fileResult->manifestFile);
            pckWriteU32P(data, fileResult->backupCopyResult);
            pckWriteBoolP(data, fileResult->repoInvalid);
            pckWriteU64P(data, fileResult->copySize);
            pckWriteU64P(data, fileResult->bundleOffset);
            pckWriteU64P(data, fileResult->blockIncrMapOffset);
            pckWriteU64P(data, fileResult->blockIncrMapSize);
            pckWriteU64P(data, fileResult->repoSize);
            pckWriteBinP(data, fileResult->copyChecksum);
            pckWriteBinP(data, fileResult->repoChecksum);
            pckWritePackP(data, fileResult->pageChecksumResult);
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(PROTOCOL_SERVER_RESULT, result);
}
