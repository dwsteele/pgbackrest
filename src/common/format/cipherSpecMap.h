/***********************************************************************************************************************************
Cipher Spec Map

The keys used to encrypt a repository, stored by id. A file identifies its key by id, either in the header or by where the file is
stored. The meaning of an id is set by the caller. archive.info numbers its keys and backup.info uses the backup set label.

One key is current and new files are encrypted with it. Adding a key makes it current, except for the key stored under
CIPHER_SPEC_MAP_ID_DEFAULT.
***********************************************************************************************************************************/
#ifndef COMMON_FORMAT_CIPHERSPECMAP_H
#define COMMON_FORMAT_CIPHERSPECMAP_H

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
typedef struct CipherSpecMap CipherSpecMap;

#include <time.h>

#include "common/crypto/spec.h"
#include "common/type/list.h"
#include "common/type/object.h"
#include "common/type/pack.h"

/***********************************************************************************************************************************
Id for the key required by a file that contains no key id. In archive.info this is the key preserved by migration, which is what
files written before key ids existed were encrypted with.
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
    time_t time;                                                    // Time the key was added, zero when not recorded
} CipherSpecMapItem;

typedef struct CipherSpecMapPub
{
    List *list;                                                     // Keys sorted by id
    const String *idCurrent;                                        // Current key id
} CipherSpecMapPub;

// Total keys
FN_INLINE_ALWAYS unsigned int
cipherSpecMapSize(const CipherSpecMap *const this)
{
    return lstSize(THIS_PUB(CipherSpecMap)->list);
}

// Is the map empty?
FN_INLINE_ALWAYS bool
cipherSpecMapEmpty(const CipherSpecMap *const this)
{
    return cipherSpecMapSize(this) == 0;
}

// Key by index
FN_INLINE_ALWAYS const CipherSpecMapItem *
cipherSpecMapGetIdx(const CipherSpecMap *const this, const unsigned int idx)
{
    return lstGet(THIS_PUB(CipherSpecMap)->list, idx);
}

// Key by id, NULL when the id is not found
FN_EXTERN const CipherSpec *cipherSpecMapFind(const CipherSpecMap *this, const String *id);

// Key by id. Errors when the id is not found.
FN_EXTERN const CipherSpec *cipherSpecMapGet(const CipherSpecMap *this, const String *id);

// Current key id, NULL when the map has only the default key with no id
FN_INLINE_ALWAYS const String *
cipherSpecMapIdCurrent(const CipherSpecMap *const this)
{
    return THIS_PUB(CipherSpecMap)->idCurrent;
}

// Set the current key id. The id must already be in the map.
FN_EXTERN void cipherSpecMapIdCurrentSet(CipherSpecMap *this, const String *id);

// Time a key was added, zero when not recorded. The id must already be in the map.
FN_INLINE_ALWAYS time_t
cipherSpecMapTime(const CipherSpecMap *const this, const String *const id)
{
    const CipherSpecMapItem *const item = lstFind(THIS_PUB(CipherSpecMap)->list, &id);
    ASSERT_INLINE(item != NULL);

    return item->time;
}

// Duplicate
FN_EXTERN CipherSpecMap *cipherSpecMapDup(const CipherSpecMap *this);

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
// Add a key and make it current, except for a key stored under CIPHER_SPEC_MAP_ID_DEFAULT. The id must be unique.
typedef struct CipherSpecMapAddParam
{
    VAR_PARAM_HEADER;
    time_t time;                                                    // Time the key was added, zero when not recorded
} CipherSpecMapAddParam;

#define cipherSpecMapAddP(this, id, cipherSpec, ...)                                                                               \
    cipherSpecMapAdd(this, id, cipherSpec, (CipherSpecMapAddParam){VAR_PARAM_INIT, __VA_ARGS__})

FN_EXTERN void cipherSpecMapAdd(CipherSpecMap *this, const String *id, const CipherSpec *cipherSpec, CipherSpecMapAddParam param);

// Remove a key. The current key cannot be removed.
FN_INLINE_ALWAYS void
cipherSpecMapRemove(CipherSpecMap *const this, const String *const id)
{
    ASSERT_INLINE(!strEq(id, cipherSpecMapIdCurrent(this)));
    ASSERT_INLINE(lstFind(THIS_PUB(CipherSpecMap)->list, &id) != NULL);

    lstRemove(THIS_PUB(CipherSpecMap)->list, &id);
}

// Write to a pack so it can be passed over a protocol. Key add times are not included.
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
