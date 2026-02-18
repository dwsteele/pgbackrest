/***********************************************************************************************************************************
Test Sqlite Wrapper
***********************************************************************************************************************************/

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
static void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // *****************************************************************************************************************************
    if (testBegin("sqlite*()"))
    {
        Sqlite *sqlite;
        TEST_ASSIGN(sqlite, sqliteNewP(), "new sqlite");
        TEST_RESULT_VOID(sqliteExec(sqlite, STRDEF("create table test (id int primary key not null)")), "create table");

        SqliteStmt *stmt;
        TEST_ASSIGN(stmt, sqliteStmtNew(sqlite, STRDEF("insert into test (id) values (?)")), "new insert stmt");
        TEST_ERROR(sqliteStmtExec(stmt), DbConnectError, "could not get next: NOT NULL constraint failed: test.id");
        TEST_RESULT_VOID(sqliteStmtFree(stmt), "free stmt");

        TEST_ASSIGN(stmt, sqliteStmtNew(sqlite, STRDEF("insert into test (id) values (?)")), "new insert stmt");
        TEST_RESULT_VOID(sqliteStmtBindInt(stmt, 1, 1), "bind int");
        TEST_RESULT_VOID(sqliteStmtExec(stmt), "exec stmt");
        TEST_RESULT_VOID(sqliteStmtBindInt(stmt, 1, 2), "bind int");
        TEST_RESULT_VOID(sqliteStmtExec(stmt), "exec stmt");
        TEST_RESULT_VOID(sqliteStmtFree(stmt), "free stmt");

        TEST_ASSIGN(stmt, sqliteStmtNew(sqlite, STRDEF("select id from test order by id")), "new select stmt");
        TEST_RESULT_BOOL(sqliteStmtNext(stmt), true, "next row");
        TEST_RESULT_INT(sqliteStmtInt(stmt, 0), 1, "next");
        TEST_RESULT_BOOL(sqliteStmtNext(stmt), true, "next row");
        TEST_RESULT_INT(sqliteStmtInt(stmt, 0), 2, "next");
        TEST_RESULT_BOOL(sqliteStmtNext(stmt), false, "no more rows");
        TEST_RESULT_VOID(sqliteStmtFree(stmt), "free stmt");

        TEST_RESULT_VOID(sqliteFree(sqlite), "free sqlite");
    }

    FUNCTION_HARNESS_RETURN_VOID();
}
