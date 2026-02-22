/***********************************************************************************************************************************
Sqlite Wrapper
***********************************************************************************************************************************/
#include <build.h>

#include <sqlite3.h>

#include "common/debug.h"
#include "common/memContext.h"
#include "common/type/sqlite.h"

/***********************************************************************************************************************************
Sqlite type
***********************************************************************************************************************************/
struct Sqlite
{
    sqlite3 *db;                                                    // Sqlite db
};

/***********************************************************************************************************************************
Sqlite statement type
***********************************************************************************************************************************/
struct SqliteStmt
{
    Sqlite *sqlite;                                                 // Sqlite
    sqlite3_stmt *stmt;                                             // Prepared statement
};

/***********************************************************************************************************************************
Throw error
***********************************************************************************************************************************/
#define SQLITE_ERR(sqlite, error, message)                                                                                         \
    do                                                                                                                             \
    {                                                                                                                              \
        if (error)                                                                                                                 \
            sqliteErr(sqlite, message);                                                                                            \
    } while (0)

FN_NO_RETURN static void
sqliteErr(Sqlite *const this, const char *const message)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE, this);
        FUNCTION_TEST_PARAM(STRINGZ, message);
    FUNCTION_TEST_END();

    const String *const errorMsg = strNewZ(sqlite3_errmsg(this->db));
    THROW_FMT(DbConnectError, "%s: %s", message, strZ(errorMsg));
}

/***********************************************************************************************************************************
Close sqlite stmt
***********************************************************************************************************************************/
static void
sqliteStmtFreeResource(THIS_VOID)
{
    THIS(SqliteStmt);

    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    sqlite3_finalize(this->stmt);

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
static SqliteStmt *
sqliteStmtNewInternal(Sqlite *const sqlite, const String *const statement)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE, sqlite);
        FUNCTION_TEST_PARAM(STRING, statement);
    FUNCTION_TEST_END();

    OBJ_NEW_BEGIN(SqliteStmt, .childQty = MEM_CONTEXT_QTY_MAX, .callbackQty = 1)
    {
        *this = (SqliteStmt)
        {
            .sqlite = sqlite,
        };

        SQLITE_ERR(
            sqlite, sqlite3_prepare_v3(sqlite->db, strZ(statement), -1, SQLITE_PREPARE_PERSISTENT, &this->stmt, NULL) != SQLITE_OK,
            "unable to prepare statement");

        // Set free callback to ensure statement is finalized
        memContextCallbackSet(objMemContext(this), sqliteStmtFreeResource, this);
    }
    OBJ_NEW_END();

    FUNCTION_TEST_RETURN(SQLITE_STMT, this);
}

/**********************************************************************************************************************************/
FN_EXTERN void
sqliteStmtBindBuf(SqliteStmt *const this, const unsigned int column, const Buffer *const value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
        FUNCTION_TEST_PARAM(UINT, column);
        FUNCTION_TEST_PARAM(BUFFER, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(column > 0 && column <= INT_MAX);
    ASSERT((int)column <= sqlite3_bind_parameter_count(this->stmt));

    SQLITE_ERR(
        this->sqlite,
        sqlite3_bind_blob(this->stmt, (int)column, bufPtrConst(value), (int)bufUsed(value), SQLITE_STATIC) != SQLITE_OK,
        "unable to bind blob to column !!!");

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN void
sqliteStmtBindI64(SqliteStmt *const this, const unsigned int column, const int64_t value, const SqliteStmtBindI64Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
        FUNCTION_TEST_PARAM(UINT, column);
        FUNCTION_TEST_PARAM(INT64, value);
        FUNCTION_TEST_PARAM(INT64, param.defaultNull);
        FUNCTION_TEST_PARAM(INT64, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(column > 0 && column <= INT_MAX);
    ASSERT((int)column <= sqlite3_bind_parameter_count(this->stmt));

    if (param.defaultNull && value == param.defaultValue)
        sqliteStmtBindNull(this, column);
    else
    {
        SQLITE_ERR(
            this->sqlite, sqlite3_bind_int64(this->stmt, (int)column, value) != SQLITE_OK, "unable to bind int64 to column !!!");
    }

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN void
sqliteStmtBindNull(SqliteStmt *const this, const unsigned int column)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
        FUNCTION_TEST_PARAM(UINT, column);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(column > 0 && column <= INT_MAX);
    ASSERT((int)column <= sqlite3_bind_parameter_count(this->stmt));

    SQLITE_ERR(this->sqlite, sqlite3_bind_null(this->stmt, (int)column) != SQLITE_OK, "unable to bind null to column !!!");

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN void
sqliteStmtBindStr(SqliteStmt *const this, const unsigned int column, const String *const value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
        FUNCTION_TEST_PARAM(UINT, column);
        FUNCTION_TEST_PARAM(STRING, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(column > 0 && column <= INT_MAX);
    ASSERT((int)column <= sqlite3_bind_parameter_count(this->stmt));

    SQLITE_ERR(
        this->sqlite,
        sqlite3_bind_text(this->stmt, (int)column, strZ(value), (int)strSize(value), SQLITE_STATIC) != SQLITE_OK,
        "unable to bind int to column !!!");

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN void
sqliteStmtExec(SqliteStmt *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    sqliteStmtNext(this);
    sqliteStmtReset(this);

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN unsigned int
sqliteStmtChanged(SqliteStmt *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(UINT, (unsigned int)sqlite3_changes(this->sqlite->db));
}

/**********************************************************************************************************************************/
FN_EXTERN void
sqliteStmtReset(SqliteStmt *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    SQLITE_ERR(this->sqlite, sqlite3_reset(this->stmt) != SQLITE_OK, "could not reset");
    SQLITE_ERR(this->sqlite, sqlite3_clear_bindings(this->stmt) != SQLITE_OK, "could not clear bindings");

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN bool
sqliteStmtNext(SqliteStmt *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    const int rc = sqlite3_step(this->stmt);

    if (rc == SQLITE_ROW)
        FUNCTION_TEST_RETURN(BOOL, true);

    if (rc == SQLITE_DONE)
        FUNCTION_TEST_RETURN(BOOL, false);

    sqliteErr(this->sqlite, "could not get next");
}

/**********************************************************************************************************************************/
FN_EXTERN uint64_t
sqliteStmtInsertRowId(SqliteStmt *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(sqlite3_last_insert_rowid(this->sqlite->db) > 0);

    FUNCTION_TEST_RETURN(UINT64, (uint64_t)sqlite3_last_insert_rowid(this->sqlite->db));
}

/**********************************************************************************************************************************/
FN_EXTERN Buffer *
sqliteStmtBuf(SqliteStmt *const this, const unsigned int column)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
        FUNCTION_TEST_PARAM(UINT, column);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(column <= INT_MAX);
    ASSERT((int)column < sqlite3_column_count(this->stmt));
    ASSERT(
        sqlite3_column_type(this->stmt, (int)column) == SQLITE_BLOB ||
        sqlite3_column_type(this->stmt, (int)column) == SQLITE_TEXT ||
        sqlite3_column_type(this->stmt, (int)column) == SQLITE_NULL);

    if (sqliteStmtNull(this, column))
        FUNCTION_TEST_RETURN(BUFFER, NULL);

    const void *const ptr = sqlite3_column_blob(this->stmt, (int)column);

    FUNCTION_TEST_RETURN(BUFFER, bufNewC(ptr, (size_t)sqlite3_column_bytes(this->stmt, (int)column)));
}

/**********************************************************************************************************************************/
FN_EXTERN int64_t
sqliteStmtI64(SqliteStmt *const this, const unsigned int column, const SqliteStmtI64Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
        FUNCTION_TEST_PARAM(UINT, column);
        FUNCTION_TEST_PARAM(INT64, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(column <= INT_MAX);
    ASSERT((int)column < sqlite3_column_count(this->stmt));
    ASSERT(
        sqlite3_column_type(this->stmt, (int)column) == SQLITE_INTEGER ||
        sqlite3_column_type(this->stmt, (int)column) == SQLITE_NULL);

    if (sqliteStmtNull(this, column))
        FUNCTION_TEST_RETURN(INT64, param.defaultValue);

    FUNCTION_TEST_RETURN(INT64, sqlite3_column_int64(this->stmt, (int)column));
}

/**********************************************************************************************************************************/
FN_EXTERN bool
sqliteStmtNull(SqliteStmt *const this, const unsigned int column)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
        FUNCTION_TEST_PARAM(UINT, column);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(column <= INT_MAX);
    ASSERT((int)column < sqlite3_column_count(this->stmt));

    FUNCTION_TEST_RETURN(BOOL, sqlite3_column_type(this->stmt, (int)column) == SQLITE_NULL);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
sqliteStmtStr(SqliteStmt *const this, const unsigned int column)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE_STMT, this);
        FUNCTION_TEST_PARAM(UINT, column);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(column <= INT_MAX);
    ASSERT((int)column < sqlite3_column_count(this->stmt));
    ASSERT(sqlite3_column_type(this->stmt, (int)column) != SQLITE_BLOB);

    if (sqliteStmtNull(this, column))
        FUNCTION_TEST_RETURN(STRING, NULL);

    FUNCTION_TEST_RETURN(STRING, strNewZ((const char *)sqlite3_column_text(this->stmt, (int)column)));
}

/***********************************************************************************************************************************
Close sqlite
***********************************************************************************************************************************/
static void
sqliteFreeResource(THIS_VOID)
{
    THIS(Sqlite);

    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    sqlite3_close_v2(this->db);

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN Sqlite *
sqliteNew(void)
{
    FUNCTION_TEST_VOID();

    OBJ_NEW_BEGIN(Sqlite, .childQty = MEM_CONTEXT_QTY_MAX, .callbackQty = 1)
    {
        *this = (Sqlite){0};

        // Set free callback to ensure sqlite is closed
        memContextCallbackSet(objMemContext(this), sqliteFreeResource, this);

        SQLITE_ERR(
            this,
            sqlite3_open_v2(
                ":memory:", &this->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_MEMORY | SQLITE_OPEN_PRIVATECACHE, NULL) != SQLITE_OK,
            "unable to open sqlite memory db");
    }
    OBJ_NEW_END();

    FUNCTION_TEST_RETURN(SQLITE, this);
}

/**********************************************************************************************************************************/
FN_EXTERN void
sqliteExec(Sqlite *const this, const String *const statement)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE, this);
        FUNCTION_TEST_PARAM(STRING, statement);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(statement != NULL);

    SqliteStmt *const stmt = sqliteStmtNew(this, statement);
    sqliteStmtNext(stmt);
    sqliteStmtFree(stmt);

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN SqliteStmt *
sqliteStmtNew(Sqlite *const this, const String *const statement)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SQLITE, this);
        FUNCTION_TEST_PARAM(STRING, statement);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(statement != NULL);

    FUNCTION_TEST_RETURN(SQLITE_STMT, sqliteStmtNewInternal(this, statement));
}
