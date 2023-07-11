/***********************************************************************************************************************************
Test Vm Commands
***********************************************************************************************************************************/

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
static void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // *****************************************************************************************************************************
    if (testBegin("vmRender()"))
    {
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("render debian");

        #define TEST_DEBIAN_PACKAGE_INSTALL                                                                                        \
            "export DEBCONF_NONINTERACTIVE_SEEN=true DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends"

        StringList *packageList = strLstNew();
        strLstAddZ(packageList, "package1");
        strLstAddZ(packageList, "package2");

        StringList *pgList = strLstNew();
        strLstAddZ(pgList, "9.6");
        strLstAddZ(pgList, "10");

        TEST_RESULT_STR_Z(
            vmRender(vmDefinition(STRDEF("u22")), false, packageList, pgList, STRDEF("16")),
            "# Base container\n"
            "FROM ubuntu:22.04\n"
            "\n"
            "# Install packages\n"
            "RUN apt-get update\n"
            "RUN " TEST_DEBIAN_PACKAGE_INSTALL " package1\n"
            "RUN " TEST_DEBIAN_PACKAGE_INSTALL " package2\n"
            "\n"
            "# Install PostgreSQL packages\n"
            "RUN echo \"deb http://apt.postgresql.org/pub/repos/apt/ $(lsb_release -s -c)-pgdg main 16\""
            " >> /etc/apt/sources.list.d/pgdg.list\n"
            "RUN wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -\n"
            "RUN apt-get update\n"
            "RUN " TEST_DEBIAN_PACKAGE_INSTALL " postgresql-common libpq-dev\n"
            "RUN sed -i 's/^\\#create\\_main\\_cluster.*$/create\\_main\\_cluster \\= false/'"
            " /etc/postgresql-common/createcluster.conf\n",
            "render");
    }

    // *****************************************************************************************************************************
    if (testBegin("cmdVmBuild()"))
    {
        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("invalid options");

        TEST_ERROR(cmdVmBuild(STRDEF("bogus")), OptionInvalidError, "'bogus' is not valid vm definition");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("valid vm");

        TEST_RESULT_VOID(cmdVmBuild(STRDEF("u22")), "build u22");
        TEST_RESULT_VOID(cmdVmBuild(STRDEF("rh7")), "build rh7");
    }

    FUNCTION_HARNESS_RETURN_VOID();
}
