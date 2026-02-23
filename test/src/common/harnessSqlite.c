/***********************************************************************************************************************************
Harness for Testing SQLite
***********************************************************************************************************************************/
#include <build.h>

#include "common/type/list.h"
#include "common/type/stringList.h"

#include "common/harnessDebug.h"
#include "common/harnessSqlite.h"

/***********************************************************************************************************************************
Include shimmed C modules
***********************************************************************************************************************************/
{[SHIM_MODULE]}

/**********************************************************************************************************************************/
typedef struct HrnSqliteStmtColumn
{
    const char *name;                                               // Column name
    size_t width;                                                   // Column width
    int type;                                                       // Column type
} HrnSqliteStmtColumn;

String *
hrnSqliteStmtToStr(SqliteStmt *const stmt)
{
    FUNCTION_HARNESS_BEGIN();
        FUNCTION_HARNESS_PARAM(SQLITE_STMT, stmt);
    FUNCTION_HARNESS_END();

    String *const result = strNew();

    MEM_CONTEXT_TEMP_BEGIN()
    {
        List *const colLst = lstNewP(sizeof(HrnSqliteStmtColumn));
        List *const rowLst = lstNewP(sizeof(StringList *));

        sqliteStmtExec(stmt);

        // Gather columns
        for (int colIdx = 0; colIdx < sqlite3_column_count(stmt->stmt); colIdx++)
        {
            HrnSqliteStmtColumn col = {.name = sqlite3_column_name(stmt->stmt, colIdx)};
            col.width = strlen(col.name);
            col.type = SQLITE_NULL;

            lstAdd(colLst, &col);
        }

        // Gather rows
        while (sqliteStmtNext(stmt))
        {
            StringList *const rowColLst = strLstNew();

            for (int colIdx = 0; colIdx < sqlite3_column_count(stmt->stmt); colIdx++)
            {
                HrnSqliteStmtColumn *const col = lstGet(colLst, (unsigned int)colIdx);

                if (col->type == SQLITE_NULL)
                    col->type = sqlite3_column_type(stmt->stmt, colIdx);

                const char *value = "";
                size_t width = 0;

                if (sqlite3_column_type(stmt->stmt, colIdx) != SQLITE_NULL)
                {
                    if (sqlite3_column_type(stmt->stmt, colIdx) == SQLITE_BLOB)
                    {
                        width = (size_t)sqlite3_column_bytes(stmt->stmt, colIdx);
                        bool extra = false;

                        if (width > 4)
                        {
                            width = 4;
                            extra = true;
                        }

                        value = strZ(strNewEncode(encodingHex, BUF(sqlite3_column_blob(stmt->stmt, colIdx), width)));
                        width *= 2;

                        if (extra)
                        {
                            value = zNewFmt("%s+", value);
                            width += 1;
                        }
                    }
                    else
                    {
                        value = (const char *)sqlite3_column_text(stmt->stmt, colIdx);
                        width = strlen(value);
                    }
                }

                if (col->width < width)
                    col->width = width;

                strLstAddZ(rowColLst, value);
            }

            lstAdd(rowLst, &rowColLst);
        }

        // Output as table
        for (unsigned int colIdx = 0; colIdx < lstSize(colLst); colIdx++)
        {
            const HrnSqliteStmtColumn *const col = lstGet(colLst, (unsigned int)colIdx);

            if (col->type == SQLITE_NULL)
                continue;

            if (colIdx != 0)
                strCatChr(result, '|');

            size_t width = ((col->width - strlen(col->name)) / 2) + strlen(col->name);

            strCatFmt(result, "%*s", (int)width, col->name);

            for (unsigned int spaceIdx = 0; spaceIdx < col->width - width; spaceIdx++)
                strCatChr(result, ' ');
        }

        strCatChr(result, '\n');

        for (unsigned int colIdx = 0; colIdx < lstSize(colLst); colIdx++)
        {
            const HrnSqliteStmtColumn *const col = lstGet(colLst, (unsigned int)colIdx);

            if (col->type == SQLITE_NULL)
                continue;

            if (colIdx != 0)
                strCatChr(result, '|');

            for (unsigned int dashIdx = 0; dashIdx < col->width; dashIdx++)
                strCatChr(result, '-');
        }

        for (unsigned int rowIdx = 0; rowIdx < lstSize(rowLst); rowIdx++)
        {
            const StringList *const rowColLst = *(StringList **)lstGet(rowLst, rowIdx);

            strCatChr(result, '\n');

            for (unsigned int colIdx = 0; colIdx < strLstSize(rowColLst); colIdx++)
            {
                const HrnSqliteStmtColumn *const col = lstGet(colLst, (unsigned int)colIdx);

                if (col->type == SQLITE_NULL)
                    continue;

                if (colIdx != 0)
                    strCatChr(result, '|');

                if (col->type == SQLITE_INTEGER)
                    strCatFmt(result, "%*s", (int)col->width, strZ(strLstGet(rowColLst, colIdx)));
                else
                    strCatFmt(result, "%-*s", (int)col->width, strZ(strLstGet(rowColLst, colIdx)));
            }
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_HARNESS_RETURN(STRING, result);
}
