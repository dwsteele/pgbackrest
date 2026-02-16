/***********************************************************************************************************************************
Restore File
***********************************************************************************************************************************/
#include "build.auto.h"

#include <fcntl.h>
#include <unistd.h>
#include <utime.h>

#include "command/backup/blockIncr.h"
#include "command/backup/blockMap.h"
#include "command/restore/blockChecksum.h"
#include "command/restore/blockDelta.h"
#include "command/restore/file.h"
#include "common/crypto/cipherBlock.h"
#include "common/crypto/hash.h"
#include "common/debug.h"
#include "common/io/fdWrite.h"
#include "common/io/filter/group.h"
#include "common/io/filter/size.h"
#include "common/io/io.h"
#include "common/io/limitRead.h"
#include "common/log.h"
#include "config/config.h"
#include "info/manifest.h"
#include "storage/helper.h"

/***********************************************************************************************************************************
Structure used to collate block deltas by reference
***********************************************************************************************************************************/
typedef struct RestoreFileBlockDelta
{
    unsigned int reference;                                         // Reference id
    PackWrite *delta;                                               // Packed deltas
} RestoreFileBlockDelta;

/**********************************************************************************************************************************/
FN_EXTERN List *
restoreFile(
    const String *const repoFile, const unsigned int repoIdx, const CompressType repoFileCompressType, const time_t copyTimeBegin,
    const bool delta, const bool deltaForce, const bool bundleRaw, const String *const cipherPass,
    const StringList *const referenceList, List *const fileList)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(STRING, repoFile);
        FUNCTION_LOG_PARAM(UINT, repoIdx);
        FUNCTION_LOG_PARAM(ENUM, repoFileCompressType);
        FUNCTION_LOG_PARAM(TIME, copyTimeBegin);
        FUNCTION_LOG_PARAM(BOOL, delta);
        FUNCTION_LOG_PARAM(BOOL, deltaForce);
        FUNCTION_LOG_PARAM(BOOL, bundleRaw);
        FUNCTION_TEST_PARAM(STRING, cipherPass);
        FUNCTION_LOG_PARAM(STRING_LIST, referenceList);             // List of references (for block incremental)
        FUNCTION_LOG_PARAM(LIST, fileList);                         // List of files to restore
    FUNCTION_LOG_END();

    ASSERT(repoFile != NULL);

    // Restore file results
    List *const result = lstNewP(sizeof(RestoreFileResult));

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Check files to determine which ones need to be restored
        StorageReadMulti *const repoFileRead = storageNewReadMultiP(storageRepoIdx(repoIdx));

        for (unsigned int fileIdx = 0; fileIdx < lstSize(fileList); fileIdx++)
        {
            // Use a per-file mem context to reduce memory usage
            MEM_CONTEXT_TEMP_BEGIN()
            {
                RestoreFile *const file = lstGet(fileList, fileIdx);
                ASSERT(file->name != NULL);
                ASSERT(file->limit == NULL || varType(file->limit) == varTypeUInt64);

                RestoreFileResult *const fileResult = lstAdd(
                    result, &(RestoreFileResult){.manifestFile = file->manifestFile, .result = restoreResultCopy});

                // Perform delta if requested. Delta zero-length files to avoid overwriting the file if the timestamp is correct.
                if (delta && !file->zero)
                {
                    // Perform delta if the file exists
                    StorageInfo info = storageInfoP(storagePg(), file->name, .ignoreMissing = true, .followLink = true);

                    if (info.exists)
                    {
                        // If force then use size/timestamp delta
                        if (deltaForce)
                        {
                            // Make sure that timestamp/size are equal and the timestamp is before the copy start time of the backup
                            if (info.size == file->size && info.timeModified == file->timeModified &&
                                info.timeModified < copyTimeBegin)
                            {
                                fileResult->result = restoreResultPreserve;
                            }
                        }
                        // Else use size and checksum
                        else
                        {
                            // Only continue delta if the file size is as expected or larger (for normal files) or if block
                            // incremental and the file to delta is not zero-length. Block incremental can potentially use almost
                            // any portion of an existing file, but of course zero-length files do not have anything to reuse.
                            if (info.size >= file->size || (file->blockIncrMapSize != 0 && info.size != 0))
                            {
                                const char *const fileName = strZ(storagePathP(storagePg(), file->name));

                                // If the file was extended since the backup, then truncate it to the size it was during the backup
                                // as it might have only been appended to with the earlier portion being unchanged (we will verify
                                // this using the checksum below)
                                if (info.size > file->size)
                                {
                                    // Open file for write
                                    IoWrite *const pgWriteTruncate = storageWriteIo(
                                        storageNewWriteP(
                                            storagePgWrite(), file->name, .noAtomic = true, .noCreatePath = true,
                                            .noSyncPath = true, .noTruncate = true));
                                    ioWriteOpen(pgWriteTruncate);

                                    // Truncate to original size
                                    THROW_ON_SYS_ERROR_FMT(
                                        ftruncate(ioWriteFd(pgWriteTruncate), (off_t)file->size) == -1, FileWriteError,
                                        "unable to truncate file '%s'", fileName);

                                    // Close file
                                    ioWriteClose(pgWriteTruncate);

                                    // Update info
                                    info = storageInfoP(storagePg(), file->name, .followLink = true);

                                    // For block incremental it is very important that the file be exactly the expected size, so
                                    // make sure the truncate worked as expected
                                    CHECK_FMT(
                                        FileWriteError, info.size == file->size, "unable to truncate '%s' to %" PRIu64 " bytes",
                                        strZ(file->name), file->size);
                                }

                                // Generate checksum for the file if size is not zero
                                IoRead *read = NULL;

                                if (file->size != 0)
                                {
                                    read = storageReadIo(storageNewReadP(storagePg(), file->name));

                                    // Calculate checksum only when size matches
                                    if (info.size == file->size)
                                        ioFilterGroupAdd(ioReadFilterGroup(read), cryptoHashNew(hashTypeSha1));

                                    // Generate block checksum list if block incremental
                                    if (file->blockIncrMapSize != 0)
                                    {
                                        ioFilterGroupAdd(
                                            ioReadFilterGroup(read),
                                            blockChecksumNew(file->blockIncrSize, file->blockIncrChecksumSize));
                                    }

                                    ioReadDrain(read);
                                }

                                // If size/checksum is the same (or file is zero size) then no need to copy the file
                                if (file->size == 0 ||
                                    (info.size == file->size &&
                                     bufEq(
                                         file->checksum,
                                         pckReadBinP(ioFilterGroupResultP(ioReadFilterGroup(read), CRYPTO_HASH_FILTER_TYPE)))))
                                {
                                    // If the checksum/size are now the same but the time is not, then set the time back to the
                                    // backup time. This helps with unit testing, but also presents a pristine version of the
                                    // database after restore.
                                    if (info.timeModified != file->timeModified)
                                    {
                                        const struct utimbuf uTimeBuf =
                                        {
                                            .actime = file->timeModified,
                                            .modtime = file->timeModified,
                                        };

                                        THROW_ON_SYS_ERROR_FMT(
                                            utime(fileName, &uTimeBuf) == -1, FileInfoError, "unable to set time for '%s'",
                                            fileName);
                                    }

                                    fileResult->result = restoreResultPreserve;
                                }

                                // If block incremental and not preserving the file, store the block checksum list for later use in
                                // reconstructing the pg file
                                if (file->blockIncrMapSize != 0 && fileResult->result != restoreResultPreserve)
                                {
                                    PackRead *const blockChecksumResult = ioFilterGroupResultP(
                                        ioReadFilterGroup(read), BLOCK_CHECKSUM_FILTER_TYPE);

                                    MEM_CONTEXT_OBJ_BEGIN(fileList)
                                    {
                                        file->blockChecksum = pckReadBinP(blockChecksumResult);
                                    }
                                    MEM_CONTEXT_OBJ_END();
                                }
                            }
                        }
                    }
                }

                // Create zeroed and zero-length files
                if (fileResult->result == restoreResultCopy && (file->size == 0 || file->zero))
                {
                    // Create destination file
                    StorageWrite *const pgFileWrite = storageNewWriteP(
                        storagePgWrite(), file->name, .modeFile = file->mode, .user = file->user, .group = file->group,
                        .timeModified = file->timeModified, .noAtomic = true, .noCreatePath = true, .noSyncPath = true);

                    ioWriteOpen(storageWriteIo(pgFileWrite));

                    // Truncate the file to specified length (note in this case the file will grow, not shrink)
                    if (file->zero)
                    {
                        THROW_ON_SYS_ERROR_FMT(
                            ftruncate(ioWriteFd(storageWriteIo(pgFileWrite)), (off_t)file->size) == -1, FileWriteError,
                            "unable to truncate '%s'", strZ(file->name));
                    }

                    ioWriteClose(storageWriteIo(pgFileWrite));

                    // Report the file as zeroed or zero-length
                    fileResult->result = restoreResultZero;
                }

                // If the file will be copied add it to the read list
                if (fileResult->result == restoreResultCopy)
                {
                    storageReadMultiAddP(
                        repoFileRead, repoFile, .compressible = repoFileCompressType == compressTypeNone && cipherPass == NULL,
                        .offset = file->offset, .limit = file->limit);
                }
            }
            MEM_CONTEXT_TEMP_END();
        }

        // Copy whole files and construct block deltas
        List *const blockDeltaList = lstNewP(sizeof(RestoreFileBlockDelta), .comparator = lstComparatorUInt);

        LOG_DEBUG_FMT("!!!MULTI WHOLE/MAP");
        ioReadOpen(storageReadMultiIo(repoFileRead));

        for (unsigned int fileIdx = 0; fileIdx < lstSize(fileList); fileIdx++)
        {
            // Copy file from repository to database
            RestoreFileResult *const fileResult = lstGet(result, fileIdx);

            if (fileResult->result == restoreResultCopy)
            {
                // Use a per-file mem context to reduce memory usage
                MEM_CONTEXT_TEMP_BEGIN()
                {
                    const RestoreFile *const file = lstGet(fileList, fileIdx);

                    // If block incremental file
                    if (file->blockIncrMapSize != 0)
                    {
                        ASSERT(referenceList != NULL);

                        // Read block map. This will be compared to the block checksum list already created to determine which
                        // blocks need to be fetched from the repository. If we got here there must be at least one block to fetch.
                        IoRead *const blockMapRead = ioLimitReadNew(storageReadMultiIo(repoFileRead), varUInt64(file->limit));

                        if (cipherPass != NULL)
                        {
                            ioFilterGroupAdd(
                                ioReadFilterGroup(blockMapRead),
                                cipherBlockNewP(cipherModeDecrypt, cipherTypeAes256Cbc, BUFSTR(cipherPass), .raw = true));
                        }

                        ioReadOpen(blockMapRead);

                        const BlockMap *const blockMap = blockMapNewRead(
                            blockMapRead, file->blockIncrSize, file->blockIncrChecksumSize);

                        ioReadFree(blockMapRead);

                        // Generate a list of blocks that need to be fetched to process block deltas for this file. The block lists
                        // for all files are combined by reference so they can later be reordered to get the most efficient scans
                        // across bundles. A pack is used to store the data efficiently.
                        MEM_CONTEXT_OBJ_BEGIN(blockDeltaList)
                        {
                            BlockDelta *const blockDelta = blockDeltaNew(
                                blockMap, file->blockIncrSize, file->blockIncrChecksumSize, file->blockChecksum,
                                cipherPass == NULL ? cipherTypeNone : cipherTypeAes256Cbc, cipherPass, repoFileCompressType);
                            RestoreFileBlockDelta *reference = NULL;
                            RestoreFileBlockDelta *referencePrior = NULL;
                            bufFree(file->blockChecksum);

                            for (unsigned int readIdx = 0; readIdx < blockDeltaReadSize(blockDelta); readIdx++)
                            {
                                const BlockDeltaRead *const read = blockDeltaReadGet(blockDelta, readIdx);

                                reference = lstFind(blockDeltaList, &read->reference);

                                if (reference == NULL)
                                {
                                    RestoreFileBlockDelta referenceNew =
                                    {
                                        .reference = read->reference,
                                        .delta = pckWriteNewP(),
                                    };

                                    reference = lstAdd(blockDeltaList, &referenceNew);
                                }

                                // Write fileIdx and BlockDelta when the reference changes
                                if (reference != referencePrior) // {uncovered_branch - !!!}
                                {
                                    // Indicate that BlockDeltaRead list is complete
                                    if (referencePrior != NULL) // {uncovered_branch - !!!}
                                        pckWriteNullP(reference->delta); // {uncovered - !!!}

                                    // !!! MAYBE STORE THE BLOCK MAP INSTEAD?
                                    pckWriteU64P(reference->delta, (uintptr_t)blockDelta);
                                    pckWriteU32P(reference->delta, fileIdx);

                                    referencePrior = reference;
                                }

                                // Write BlockDeltaRead
                                pckWriteU64P(reference->delta, (uintptr_t)read);
                            }

                            // Indicate that BlockDeltaRead list is complete
                            ASSERT(reference != NULL);
                            pckWriteNullP(reference->delta);
                        }
                        MEM_CONTEXT_OBJ_END();
                    }
                    // Else normal file
                    else
                    {
                        // Create pg file
                        StorageWrite *const pgFileWrite = storageNewWriteP(
                            storagePgWrite(), file->name, .modeFile = file->mode, .user = file->user, .group = file->group,
                            .timeModified = file->timeModified, .noAtomic = true, .noCreatePath = true, .noSyncPath = true);

                        IoFilterGroup *const filterGroup = ioWriteFilterGroup(storageWriteIo(pgFileWrite));

                        // Add decryption filter
                        if (cipherPass != NULL)
                        {
                            ioFilterGroupAdd(
                                filterGroup,
                                cipherBlockNewP(cipherModeDecrypt, cipherTypeAes256Cbc, BUFSTR(cipherPass), .raw = bundleRaw));
                        }

                        // Add decompression filter
                        if (repoFileCompressType != compressTypeNone)
                            ioFilterGroupAdd(filterGroup, decompressFilterP(repoFileCompressType, .raw = bundleRaw));

                        // Add sha1 filter
                        ioFilterGroupAdd(filterGroup, cryptoHashNew(hashTypeSha1));

                        // Add size filter
                        ioFilterGroupAdd(filterGroup, ioSizeNew());

                        // Copy file
                        ioWriteOpen(storageWriteIo(pgFileWrite));
                        ioCopyP(storageReadMultiIo(repoFileRead), storageWriteIo(pgFileWrite), .limit = file->limit);
                        ioWriteClose(storageWriteIo(pgFileWrite));

                        // Get checksum result
                        const Buffer *checksum = pckReadBinP(ioFilterGroupResultP(filterGroup, CRYPTO_HASH_FILTER_TYPE));

                        // Validate checksum
                        if (!bufEq(file->checksum, checksum))
                        {
                            THROW_FMT(
                                ChecksumError,
                                "error restoring '%s': actual checksum '%s' does not match expected checksum '%s'",
                                strZ(file->name), strZ(strNewEncode(encodingHex, checksum)),
                                strZ(strNewEncode(encodingHex, file->checksum)));
                        }
                    }
                }
                MEM_CONTEXT_TEMP_END();
            }
        }

        // Free read of whole files and block maps
        ioReadClose(storageReadMultiIo(repoFileRead));
        storageReadMultiFree(repoFileRead);

        // Process block deltas
        if (!lstEmpty(blockDeltaList))
        {
            StorageReadMulti *const blockRead = storageNewReadMultiP(storageRepoIdx(repoIdx));

            // Collate block deltas in the order that they need to be read. The idea is to read sequentially across each bundle a
            // single time. There may be gaps but some of those can be read over.
            for (unsigned int blockDeltaIdx = 0; blockDeltaIdx < lstSize(blockDeltaList); blockDeltaIdx++)
            {
                const RestoreFileBlockDelta *const blockReference = lstGet(blockDeltaList, blockDeltaIdx);
                pckWriteEndP(blockReference->delta);

                MEM_CONTEXT_TEMP_BEGIN()
                {
                    PackRead *const delta = pckReadNew(pckWriteResult(blockReference->delta));

                    // Iterate deltas
                    while (!pckReadNullP(delta))
                    {
                        // BlockDelta is not needed here
                        pckReadConsume(delta);
                        const RestoreFile *const file = lstGet(fileList, pckReadU32P(delta));

                        // Iterate reads
                        while (!pckReadNullP(delta))
                        {
                            const BlockDeltaRead *const read = (BlockDeltaRead *)(uintptr_t)pckReadU64P(delta);
                            const String *const repoFileName = backupFileRepoPathP(
                                strLstGet(referenceList, blockReference->reference), .manifestName = file->manifestFile,
                                .bundleId = read->bundleId, .blockIncr = true);

                            // Add to block read
                            storageReadMultiAddP(blockRead, repoFileName, .offset = read->offset, .limit = VARUINT64(read->size));
                        }
                    }
                }
                MEM_CONTEXT_TEMP_END();
            }

            // Apply block deltas
            String *const pgFileName = strNew();
            StorageWrite *pgFileWrite = NULL;

            LOG_DEBUG_FMT("!!!MULTI BLOCK DELTA");
            ioReadOpen(storageReadMultiIo(blockRead));

            // Iterate references
            for (unsigned int blockDeltaIdx = 0; blockDeltaIdx < lstSize(blockDeltaList); blockDeltaIdx++)
            {
                const RestoreFileBlockDelta *const blockReference = lstGet(blockDeltaList, blockDeltaIdx);

                MEM_CONTEXT_TEMP_BEGIN()
                {
                    PackRead *const delta = pckReadNew(pckWriteResult(blockReference->delta));

                    // Iterate deltas
                    while (!pckReadNullP(delta))
                    {
                        BlockDelta *const blockDelta = (BlockDelta *)(uintptr_t)pckReadU64P(delta);
                        const unsigned int fileIdx = pckReadU32P(delta);
                        const RestoreFile *const file = lstGet(fileList, fileIdx);
                        RestoreFileResult *const fileResult = lstGet(result, fileIdx);

                        // If the pg file to write has changed
                        if (!strEq(file->name, pgFileName)) // {uncovered_branch - !!!}
                        {
                            // Close prior pg file to complete the write
                            if (pgFileWrite != NULL)
                            {
                                ioWriteClose(storageWriteIo(pgFileWrite));
                                storageWriteFree(pgFileWrite);
                            }

                            // Open new pg file for write in prior context since it may span references
                            MEM_CONTEXT_PRIOR_BEGIN()
                            {
                                pgFileWrite = storageNewWriteP(
                                    storagePgWrite(), file->name, .modeFile = file->mode, .user = file->user, .group = file->group,
                                    .timeModified = file->timeModified, .noAtomic = true, .noCreatePath = true, .noSyncPath = true,
                                    .noTruncate = true);
                                ioWriteOpen(storageWriteIo(pgFileWrite));
                            }
                            MEM_CONTEXT_PRIOR_END();

                            strCat(strTrunc(pgFileName),  file->name);
                        }

                        // Iterate reads
                        while (!pckReadNullP(delta))
                        {
                            // Write updated blocks to the file
                            const BlockDeltaRead *const read = (BlockDeltaRead *)(uintptr_t)pckReadU64P(delta);
                            const BlockDeltaWrite *deltaWrite = blockDeltaNext(blockDelta, read, storageReadMultiIo(blockRead));

                            while (deltaWrite != NULL)
                            {
                                // Seek to the block offset. It is possible we are already at the correct position but it is easier
                                // and safer to let lseek() figure this out.
                                THROW_ON_SYS_ERROR_FMT(
                                    lseek(ioWriteFd(storageWriteIo(pgFileWrite)), (off_t)deltaWrite->offset, SEEK_SET) == -1,
                                    FileOpenError, STORAGE_ERROR_READ_SEEK, deltaWrite->offset,
                                    strZ(storagePathP(storagePg(), file->name)));

                                // Write block
                                ioWrite(storageWriteIo(pgFileWrite), deltaWrite->block);
                                fileResult->blockIncrDeltaSize += bufUsed(deltaWrite->block);

                                // Flush writes since we may seek to a new location for the next block
                                ioWriteFlush(storageWriteIo(pgFileWrite));

                                deltaWrite = blockDeltaNext(blockDelta, read, storageReadMultiIo(blockRead));
                            }
                        }
                    }
                }
                MEM_CONTEXT_TEMP_END();
            }

            // Close the last file to complete the write
            ioWriteClose(storageWriteIo(pgFileWrite));
            storageWriteFree(pgFileWrite);

            // Free the block delta list
            lstFree(blockDeltaList);
            storageReadMultiFree(blockRead);

            // Verify checksums for files reconstructed with block deltas
            for (unsigned int fileIdx = 0; fileIdx < lstSize(fileList); fileIdx++)
            {
                RestoreFileResult *const fileResult = lstGet(result, fileIdx);
                const RestoreFile *const file = lstGet(fileList, fileIdx);

                if (fileResult->result == restoreResultCopy && file->blockIncrMapSize != 0)
                {
                    // Use a per-file mem context to reduce memory usage
                    MEM_CONTEXT_TEMP_BEGIN()
                    {
                        // Calculate checksum. In theory this is not needed because the file should always be reconstructed
                        // correctly. However, it seems better to check and the pages should still be buffered making the operation
                        // very fast.
                        IoRead *const read = storageReadIo(storageNewReadP(storagePg(), file->name));

                        ioFilterGroupAdd(ioReadFilterGroup(read), cryptoHashNew(hashTypeSha1));
                        ioReadDrain(read);

                        const Buffer *const checksum = pckReadBinP(
                            ioFilterGroupResultP(ioReadFilterGroup(read), CRYPTO_HASH_FILTER_TYPE));

                        // Validate checksum !!! CREATE A FUNCTION FOR THIS DUPLICATED CODE
                        if (!bufEq(file->checksum, checksum)) // {uncovered_branch - !!!}
                        {
                            THROW_FMT( // {uncovered - !!!}
                                ChecksumError,
                                "error restoring '%s': actual checksum '%s' does not match expected checksum '%s'",
                                strZ(file->name), strZ(strNewEncode(encodingHex, checksum)),
                                strZ(strNewEncode(encodingHex, file->checksum)));
                        }
                    }
                    MEM_CONTEXT_TEMP_END();
                }
            }
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(LIST, result);
}
