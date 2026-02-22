/***********************************************************************************************************************************
Sqlite Wrapper
***********************************************************************************************************************************/
#ifndef COMMON_TYPE_SQLITE_H
#define COMMON_TYPE_SQLITE_H

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
typedef struct Sqlite Sqlite;
typedef struct SqliteStmt SqliteStmt;

#include "common/type/object.h"
#include "common/type/string.h"

/***********************************************************************************************************************************
Sqlite Constructor
***********************************************************************************************************************************/
#define sqliteNewP()                                                sqliteNew()

FN_EXTERN Sqlite *sqliteNew(void);

/***********************************************************************************************************************************
Sqlite Functions
***********************************************************************************************************************************/
// Execute a statement with no results and no binds
FN_EXTERN void sqliteExec(Sqlite *this, const String *statement);

/***********************************************************************************************************************************
Sqlite Destructor
***********************************************************************************************************************************/
FN_INLINE_ALWAYS void
sqliteFree(Sqlite *const this)
{
    objFree(this);
}

/***********************************************************************************************************************************
SqliteStmt Constructor
***********************************************************************************************************************************/
// Create a statement to query and/or be executed with binds
FN_EXTERN SqliteStmt *sqliteStmtNew(Sqlite *this, const String *statement);

/***********************************************************************************************************************************
SqliteStmt Functions
***********************************************************************************************************************************/
// Bind values to statement
typedef struct SqliteStmtBindI64Param
{
    VAR_PARAM_HEADER;
    bool defaultNull;                                               // Write default as NULL
    int64_t defaultValue;                                           // Default value
} SqliteStmtBindI64Param;

#define sqliteStmtBindI64P(this, column, value, ...)                                                                               \
    sqliteStmtBindI64(this, column, value, (SqliteStmtBindI64Param){VAR_PARAM_INIT, __VA_ARGS__})

FN_EXTERN void sqliteStmtBindI64(SqliteStmt *this, unsigned int column, int64_t value, SqliteStmtBindI64Param param);

typedef struct SqliteStmtBindBoolParam
{
    VAR_PARAM_HEADER;
    bool defaultNull;                                               // Write default as NULL
    bool defaultValue;                                              // Default value
} SqliteStmtBindBoolParam;

#define sqliteStmtBindBoolP(this, column, value, ...)                                                                              \
    sqliteStmtBindBool(this, column, value, (SqliteStmtBindBoolParam){VAR_PARAM_INIT, __VA_ARGS__})

FN_INLINE_ALWAYS void
sqliteStmtBindBool(SqliteStmt *const this, const unsigned int column, const bool value, const SqliteStmtBindBoolParam param)
{
    sqliteStmtBindI64P(this, column, (int64_t)value, .defaultNull = param.defaultNull, .defaultValue = (int64_t)param.defaultValue);
}

FN_EXTERN void sqliteStmtBindBuf(SqliteStmt *this, unsigned int column, const Buffer *value);
FN_EXTERN void sqliteStmtBindNull(SqliteStmt *this, unsigned int column);
FN_EXTERN void sqliteStmtBindStr(SqliteStmt *this, unsigned int column, const String *value);

FN_INLINE_ALWAYS void
sqliteStmtBindUInt(SqliteStmt *const this, const unsigned int column, const unsigned int value)
{
    sqliteStmtBindI64P(this, column, (int64_t)value);
}

typedef struct SqliteStmtBindU63Param
{
    VAR_PARAM_HEADER;
    bool defaultNull;                                               // Write default as NULL
    bool defaultValue;                                              // Default value
} SqliteStmtBindU63Param;

#define sqliteStmtBindU63P(this, column, value, ...)                                                                               \
    sqliteStmtBindU63(this, column, value, (SqliteStmtBindU63Param){VAR_PARAM_INIT, __VA_ARGS__})

FN_INLINE_ALWAYS void
sqliteStmtBindU63(SqliteStmt *const this, const unsigned int column, const uint64_t value, const SqliteStmtBindU63Param param)
{
    ASSERT_INLINE(value <= INT64_MAX);
    sqliteStmtBindI64P(this, column, (int64_t)value, .defaultNull = param.defaultNull, .defaultValue = (int64_t)param.defaultValue);
}

// Execute statement
FN_EXTERN void sqliteStmtExec(SqliteStmt *this);

// Rows changed by and insert, update, or delete statement
FN_EXTERN unsigned int sqliteStmtChanged(SqliteStmt *const this);

// Get next result
FN_EXTERN bool sqliteStmtNext(SqliteStmt *this);

// Reset statement
FN_EXTERN void sqliteStmtReset(SqliteStmt *this);

// Get values returned by statement after sqliteStmtNext()
typedef struct SqliteStmtI64Param
{
    VAR_PARAM_HEADER;
    int64_t defaultValue;                                           // Default value when NULL
} SqliteStmtI64Param;

#define sqliteStmtI64P(this, column, ...)                                                                                          \
    sqliteStmtI64(this, column, (SqliteStmtI64Param){VAR_PARAM_INIT, __VA_ARGS__})

FN_EXTERN int64_t sqliteStmtI64(SqliteStmt *this, unsigned int column, SqliteStmtI64Param param);

typedef struct SqliteStmtBoolParam
{
    VAR_PARAM_HEADER;
    bool defaultValue;                                              // Default value when NULL
} SqliteStmtBoolParam;

#define sqliteStmtBoolP(this, column, ...)                                                                                         \
    sqliteStmtBool(this, column, (SqliteStmtBoolParam){VAR_PARAM_INIT, __VA_ARGS__})

FN_INLINE_ALWAYS bool
sqliteStmtBool(SqliteStmt *const this, const unsigned int column, const SqliteStmtBoolParam param)
{
    ASSERT_INLINE(sqliteStmtI64P(this, column, .defaultValue = (int64_t)param.defaultValue) <= 1);
    return (unsigned int)sqliteStmtI64P(this, column, .defaultValue = (int64_t)param.defaultValue);
}

FN_EXTERN Buffer *sqliteStmtBuf(SqliteStmt *this, unsigned int column);
FN_EXTERN bool sqliteStmtNull(SqliteStmt *this, unsigned int column);
FN_EXTERN String *sqliteStmtStr(SqliteStmt *this, unsigned int column);

typedef struct SqliteStmtUIntParam
{
    VAR_PARAM_HEADER;
    unsigned int defaultValue;                                      // Default value when NULL
} SqliteStmtUIntParam;

#define sqliteStmtUIntP(this, column, ...)                                                                                         \
    sqliteStmtUInt(this, column, (SqliteStmtUIntParam){VAR_PARAM_INIT, __VA_ARGS__})

FN_INLINE_ALWAYS unsigned int
sqliteStmtUInt(SqliteStmt *const this, const unsigned int column, const SqliteStmtUIntParam param)
{
    ASSERT_INLINE(sqliteStmtI64P(this, column, .defaultValue = (int64_t)param.defaultValue) <= UINT_MAX);
    return (unsigned int)sqliteStmtI64P(this, column, .defaultValue = (int64_t)param.defaultValue);
}

typedef struct SqliteStmtU63Param
{
    VAR_PARAM_HEADER;
    uint64_t defaultValue;                                          // Default value when NULL
} SqliteStmtU63Param;

#define sqliteStmtU63P(this, column, ...)                                                                                          \
    sqliteStmtU63(this, column, (SqliteStmtU63Param){VAR_PARAM_INIT, __VA_ARGS__})

FN_INLINE_ALWAYS uint64_t
sqliteStmtU63(SqliteStmt *const this, const unsigned int column, const SqliteStmtU63Param param)
{
    ASSERT_INLINE(param.defaultValue <= INT64_MAX);
    return (uint64_t)sqliteStmtI64P(this, column, .defaultValue = (int64_t)param.defaultValue);
}

/***********************************************************************************************************************************
SqliteStmt Destructor
***********************************************************************************************************************************/
FN_INLINE_ALWAYS void
sqliteStmtFree(SqliteStmt *const this)
{
    objFree(this);
}

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
#define FUNCTION_LOG_SQLITE_TYPE                                                                                                   \
    Sqlite *
#define FUNCTION_LOG_SQLITE_FORMAT(value, buffer, bufferSize)                                                                      \
    objNameToLog(value, "Sqlite", buffer, bufferSize)
#define FUNCTION_LOG_SQLITE_STMT_TYPE                                                                                              \
    SqliteStmt *
#define FUNCTION_LOG_SQLITE_STMT_FORMAT(value, buffer, bufferSize)                                                                 \
    objNameToLog(value, "SqliteSmt", buffer, bufferSize)

#endif
