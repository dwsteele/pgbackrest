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
FN_EXTERN void sqliteStmtBindBuf(SqliteStmt *this, unsigned int column, const Buffer *value);
FN_EXTERN void sqliteStmtBindInt(SqliteStmt *this, unsigned int column, int value);
FN_EXTERN void sqliteStmtBindI64(SqliteStmt *this, unsigned int column, int64_t value);
FN_EXTERN void sqliteStmtBindNull(SqliteStmt *this, unsigned int column);
FN_EXTERN void sqliteStmtBindStr(SqliteStmt *this, unsigned int column, const String *value);

// Execute the statement
FN_EXTERN void sqliteStmtExec(SqliteStmt *this);

// Get next result
FN_EXTERN bool sqliteStmtNext(SqliteStmt *this);

// Reset statement
FN_EXTERN void sqliteStmtReset(SqliteStmt *this);

// Get values returned by statement after sqliteStmtNext()
FN_EXTERN int sqliteStmtInt(SqliteStmt *this, unsigned int column);

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
