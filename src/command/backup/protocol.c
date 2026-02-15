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
    // !!! THIS SHOULD BE PUT INTO A SHIM AND ONLY ENABLED FOR THE TESTS THAT NEED IT
#ifdef DEBUG
    if (strEqZ(file1->pgFile, PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL))
        FUNCTION_TEST_RETURN(INT, 1);
    else if (strEqZ(file2->pgFile, PG_PATH_GLOBAL "/" PG_FILE_PGCONTROL))
        FUNCTION_TEST_RETURN(INT, -1);
#endif

    // Order block incremental files before whole files. This produces slightly smaller maps since the offsets are smaller. Also
    // whole files can have reads combined and read over more often without block maps/lists in between them.
    if (file1->blockIncrSize != 0 && file2->blockIncrSize == 0)
        FUNCTION_TEST_RETURN(INT, -1);
    else if (file1->blockIncrSize == 0 && file2->blockIncrSize != 0)
        FUNCTION_TEST_RETURN(INT, 1);

    // Order by reference so bundles with the same id are ordered separately. Bundles ids are assigned per backup so may repeat.
    const int compare = strCmp(file1->reference, file2->reference);

    if (compare != 0)
        FUNCTION_TEST_RETURN(INT, compare);

    // Order by bundle so reads are grouped by file
    if (file1->blockIncrMapPriorBundleId < file2->blockIncrMapPriorBundleId) // {uncovered_branch - !!!}
        FUNCTION_TEST_RETURN(INT, -1); // {uncovered - !!!}
    else if (file1->blockIncrMapPriorBundleId > file2->blockIncrMapPriorBundleId) // {uncovered_branch - !!!}
        FUNCTION_TEST_RETURN(INT, 1); // {uncovered - !!!}

    // Order by map offset so reads in the repo are ordered and more likely to be combined and benefit from read over
    if (file1->blockIncrMapPriorOffset < file2->blockIncrMapPriorOffset)
        FUNCTION_TEST_RETURN(INT, -1);
    else if (file1->blockIncrMapPriorOffset > file2->blockIncrMapPriorOffset) // {uncovered_branch - !!!}
        FUNCTION_TEST_RETURN(INT, 1); // {uncovered - !!!}

    // Order by size descending. This is an arbitrary choice here but will be more efficient when block maps are stored at the end
    // of the bundle due to read over.
    if (file1->pgFileSize < file2->pgFileSize)
        FUNCTION_TEST_RETURN(INT, 1);
    else if (file1->pgFileSize > file2->pgFileSize)
        FUNCTION_TEST_RETURN(INT, -1);

    // If all the above are the same then use name asc to generate a deterministic ordering (names must be unique)
    ASSERT(!strEq(file1->pgFile, file2->pgFile));
    FUNCTION_TEST_RETURN(INT, strCmp(file1->pgFile, file2->pgFile));
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
                    file.blockIncrMapPriorBundleId = pckReadU64P(param);
                    file.blockIncrMapPriorOffset = pckReadU64P(param);
                    file.blockIncrMapPriorSize = pckReadU64P(param);
                }
            }

            file.manifestFile = pckReadStrP(param);
            file.repoFileChecksum = pckReadBinP(param);
            file.repoFileSize = pckReadU64P(param);
            file.manifestFileResume = pckReadBoolP(param);
            file.reference = pckReadStrP(param);
            file.manifestFileHasReference = file.reference != NULL;

            lstAdd(fileList, &file);
        }

        // Sort files for efficient processing
        lstSort(fileList, sortOrderAsc);

        // !!! DEBUG LOGGING
        if (bundleId != 0)
        {
            LOG_DEBUG_FMT("XXX!!!BUNDLE %" PRIu64 " SIZE %u", bundleId, lstSize(fileList));

            for (unsigned int fileIdx = 0; fileIdx < lstSize(fileList); fileIdx++)
            {
                const BackupFile *const file = lstGet(fileList, fileIdx);

                LOG_DEBUG_FMT(
                    "XXX!!!  BI %s REF %-33s REFBND %" PRIu64 " REFOFF %8" PRIu64 " SZ %8" PRIu64 " NAME %s",
                    file->blockIncrSize == 0 ? "N" : "Y", file->reference == NULL ? "NULL" : strZ(file->reference),
                    file->blockIncrMapPriorBundleId, file->blockIncrMapPriorOffset, file->pgFileSize, strZ(file->pgFile));
            }
        }

        // Backup file
        const List *const resultList = backupFile(
            repoFile, bundleId, bundleRaw, blockIncrReference, repoFileCompressType, repoFileCompressLevel, cipherType, cipherPass,
            pgVersionForce, pageSize, fileList);

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
