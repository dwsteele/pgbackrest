/***********************************************************************************************************************************
Build Common
***********************************************************************************************************************************/
#ifndef BUILD_COMMON_COMMON_H
#define BUILD_COMMON_COMMON_H

#include <inttypes.h>

#include "build/common/string.h"
#include "storage/storage.h"

/***********************************************************************************************************************************
Block comments
***********************************************************************************************************************************/
// {uncrustify_off - comment inside string}
#define COMMENT_BLOCK_BEGIN                                                                                                        \
    "/***************************************************************************************************************************" \
    "********"

#define COMMENT_BLOCK_END                                                                                                          \
    "****************************************************************************************************************************" \
    "*******/"

#define COMMENT_SEPARATOR                                                                                                          \
    "    // ---------------------------------------------------------------------------------------------------------------------" \
    "--------"
// {uncrustify_on}

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
// Format a #define with the value aligned at column 69
FN_INLINE_ALWAYS String *
bldDefineRender(const String *const define, const String *const value)
{
    return strNewFmt("#define %s%*s%s", strZ(define), (int)(60 - strSize(define)), "", strZ(value));
}

// Convert identifiers like test-id to testId and add an optional prefix
String *bldEnum(const char *const prefix, const String *const value);

// Format file header
FN_INLINE_ALWAYS String *
bldHeader(const char *const module, const char *const description)
{
    return strCatFmt(
        strNew(),
        COMMENT_BLOCK_BEGIN "\n"
        "%s\n"
        "\n"
        "Automatically generated by 'build-code %s' -- do not modify directly.\n"
        COMMENT_BLOCK_END "\n",
        description, module);
}

// Put the file if it is different than the existing file
FN_INLINE_ALWAYS void
bldPut(const Storage *const storage, const char *const file, const Buffer *const contentNew)
{
    const Buffer *const contentOld = storageGetP(storageNewReadP(storage, STR(file), .ignoreMissing = true));

    if (contentOld == NULL || !bufEq(contentOld, contentNew))
        storagePutP(storageNewWriteP(storage, STR(file), .noSyncPath = true), contentNew);
}

/***********************************************************************************************************************************
Generate constant StringIds

To generate a constant StringId call bldStrId(). It will return a String with the generated StringId macro.

For example:

bldStrId("test");

will return the following:

STRID5("test", 0xa4cb40)

which can be used in a function, switch, or #define, e.g.:

#define TEST_STRID                                                  STRID5("test", 0xa4cb40)

DO NOT MODIFY either parameter in the macro -- ALWAYS use bldStrId() to create a new constant StringId.
***********************************************************************************************************************************/
String *bldStrId(const char *const buffer);

#endif
