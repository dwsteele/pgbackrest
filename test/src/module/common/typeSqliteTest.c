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
        TEST_RESULT_VOID(
            sqliteExec(
                sqlite,
                STRDEF(
                    "create table test ("
                        "id int primary key not null,"
                        "name text,"
                        "bin blob,"
                        "more int,"
                        "all_null text)")),
            "create table");

        SqliteStmt *stmt;
        TEST_ASSIGN(stmt, sqliteStmtNew(sqlite, STRDEF("insert into test (id) values (?)")), "new insert stmt");
        TEST_ERROR(sqliteStmtExec(stmt), DbConnectError, "could not get next: NOT NULL constraint failed: test.id");
        TEST_RESULT_VOID(sqliteStmtFree(stmt), "free stmt");

        const uint8_t bufShort[] = {0x01, 0x02, 0x03};
        const uint8_t bufLong[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB};

        TEST_ASSIGN(
            stmt, sqliteStmtNew(sqlite, STRDEF("insert into test (id, name, bin, more) values (?, ?, ?, ?)")), "new insert stmt");
        TEST_RESULT_VOID(sqliteStmtBindI64P(stmt, 1, 1), "bind int");
        TEST_RESULT_VOID(sqliteStmtBindU63P(stmt, 2, 1, .defaultNull = true, .defaultValue = 1), "bind int default");
        TEST_RESULT_VOID(sqliteStmtBindBuf(stmt, 3, BUF(bufShort, sizeof(bufShort))), "bind buf");
        TEST_RESULT_VOID(sqliteStmtBindBoolP(stmt, 4, 1), "bind bool");
        TEST_RESULT_VOID(sqliteStmtExec(stmt), "exec stmt");
        TEST_RESULT_VOID(sqliteStmtBindUInt(stmt, 1, 2), "bind int");
        TEST_RESULT_VOID(sqliteStmtBindStr(stmt, 2, STRDEF("ABCDEFGH")), "bind text");
        TEST_RESULT_VOID(sqliteStmtBindBuf(stmt, 3, BUF(bufLong, sizeof(bufLong))), "bind buf");
        TEST_RESULT_VOID(sqliteStmtBindNull(stmt, 4), "bind null");
        TEST_RESULT_VOID(sqliteStmtExec(stmt), "exec stmt");
        TEST_RESULT_VOID(sqliteStmtBindU63P(stmt, 1, 999, .defaultNull = true), "bind int");
        TEST_RESULT_VOID(sqliteStmtExec(stmt), "exec stmt");
        TEST_RESULT_VOID(sqliteStmtFree(stmt), "free stmt");

        TEST_ASSIGN(
            stmt, sqliteStmtNew(sqlite, STRDEF("select id, name, bin, more, all_null from test order by id")), "new select stmt");
        TEST_RESULT_BOOL(sqliteStmtNext(stmt), true, "next row");
        TEST_RESULT_BOOL(sqliteStmtBoolP(stmt, 0), true, "bool");
        TEST_RESULT_BOOL(sqliteStmtNull(stmt, 1), true, "null");
        TEST_RESULT_BOOL(bufEq(sqliteStmtBuf(stmt, 2), BUF(bufShort, sizeof(bufShort))), true, "buf");
        TEST_RESULT_BOOL(sqliteStmtNext(stmt), true, "next row");
        TEST_RESULT_INT(sqliteStmtI64P(stmt, 0), 2, "i64");
        TEST_RESULT_STR_Z(sqliteStmtStr(stmt, 1), "ABCDEFGH", "str");
        TEST_RESULT_INT(sqliteStmtI64P(stmt, 3, .defaultValue = 2), 2, "i64 default");
        TEST_RESULT_PTR(sqliteStmtBuf(stmt, 3), NULL, "buf null");
        TEST_RESULT_PTR(sqliteStmtStr(stmt, 3), NULL, "str null");
        TEST_RESULT_BOOL(sqliteStmtNext(stmt), true, "next row");
        TEST_RESULT_UINT(sqliteStmtU63P(stmt, 0), 999, "next");
        TEST_RESULT_BOOL(sqliteStmtNext(stmt), false, "no more rows");

        TEST_RESULT_VOID(sqliteStmtReset(stmt), "reset stmt");
        TEST_RESULT_STR_Z(
            hrnSqliteStmtToStr(stmt),
            "id |  name  |   bin   |more\n"
            "---|--------|---------|----\n"
            "  1|        |010203   |   1\n"
            "  2|ABCDEFGH|fffefdfc+|    \n"
            "999|        |         |    ",
            "compare");

        TEST_RESULT_VOID(sqliteStmtFree(stmt), "free stmt");

        TEST_RESULT_VOID(sqliteFree(sqlite), "free sqlite");
    }

    FUNCTION_HARNESS_RETURN_VOID();
}
