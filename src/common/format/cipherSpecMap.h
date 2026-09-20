/***********************************************************************************************************************************
Cipher Spec Map

The keys used to encrypt a repository, stored by id. A file identifies its key by id, either in the header or by where the file is
stored. The key is then looked up by that id rather than found by trial.

The meaning of an id is set by the caller. archive.info numbers its keys and backup.info uses the backup set label.
***********************************************************************************************************************************/
#ifndef COMMON_FORMAT_CIPHERSPECMAP_H
#define COMMON_FORMAT_CIPHERSPECMAP_H

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
typedef struct CipherSpecMap CipherSpecMap;

#include "common/crypto/spec.h"
#include "common/type/list.h"
#include "common/type/object.h"
#include "common/type/pack.h"

/***********************************************************************************************************************************
Id for the key used by a file that contains no key id. In archive.info this is the key preserved by migration, which is what files
written before key ids existed were encrypted with. In an info file it is the repository passphrase, which is the only key those
files are ever encrypted with.
***********************************************************************************************************************************/
#define CIPHER_SPEC_MAP_ID_DEFAULT                                 "0"
STRING_DECLARE(CIPHER_SPEC_MAP_ID_DEFAULT_STR);

/***********************************************************************************************************************************
Constructors
***********************************************************************************************************************************/
FN_EXTERN CipherSpecMap *cipherSpecMapNew(void);

// Create from a pack written by cipherSpecMapPack()
FN_EXTERN CipherSpecMap *cipherSpecMapNewPack(PackRead *packRead);

/***********************************************************************************************************************************
Getters/Setters
***********************************************************************************************************************************/
typedef struct CipherSpecMapItem
{
    const String *id;                                               // Id associated with the key
    const CipherSpec *cipherSpec;                                   // Key and the digest it derives with
} CipherSpecMapItem;

typedef struct CipherSpecMapPub
{
    List *list;                                                     // Keys sorted by id
} CipherSpecMapPub;

// Total keys
FN_INLINE_ALWAYS unsigned int
cipherSpecMapSize(const CipherSpecMap *const this)
{
    return lstSize(THIS_PUB(CipherSpecMap)->list);
}

// Key by index
FN_INLINE_ALWAYS const CipherSpecMapItem *
cipherSpecMapGetIdx(const CipherSpecMap *const this, const unsigned int idx)
{
    return lstGet(THIS_PUB(CipherSpecMap)->list, idx);
}

// Key by id. Errors when the id is not found.
FN_EXTERN const CipherSpec *cipherSpecMapGet(const CipherSpecMap *this, const String *id);

// Duplicate
FN_EXTERN CipherSpecMap *cipherSpecMapDup(const CipherSpecMap *this);

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
// Add a key. The id must be unique.
FN_EXTERN void cipherSpecMapAdd(CipherSpecMap *this, const String *id, const CipherSpec *cipherSpec);

// Write to a pack so it can be passed over a protocol
FN_EXTERN void cipherSpecMapPack(PackWrite *packWrite, const CipherSpecMap *this);

// Move to a new parent mem context
FN_INLINE_ALWAYS CipherSpecMap *
cipherSpecMapMove(CipherSpecMap *const this, MemContext *const parentNew)
{
    return objMove(this, parentNew);
}

/***********************************************************************************************************************************
Destructor
***********************************************************************************************************************************/
FN_INLINE_ALWAYS void
cipherSpecMapFree(CipherSpecMap *const this)
{
    objFree(this);
}

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
FN_EXTERN void cipherSpecMapToLog(const CipherSpecMap *this, StringStatic *debugLog);

#define FUNCTION_LOG_CIPHER_SPEC_MAP_TYPE                                                                                          \
    CipherSpecMap *
#define FUNCTION_LOG_CIPHER_SPEC_MAP_FORMAT(value, buffer, bufferSize)                                                             \
    FUNCTION_LOG_OBJECT_FORMAT(value, cipherSpecMapToLog, buffer, bufferSize)

#endif
