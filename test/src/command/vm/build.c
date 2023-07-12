/***********************************************************************************************************************************
Vm Build Command
***********************************************************************************************************************************/
#include "build.auto.h"

#include "command/vm/build.h"
#include "common/debug.h"
#include "common/log.h"
#include "common/type/stringList.h"

/***********************************************************************************************************************************
Vm Definitions
***********************************************************************************************************************************/
typedef enum
{
    osTypeDebian = STRID5("debian", 0x1c1488a40),
    osTypeRhel = STRID5("rhel", 0x615120),
} OsType;

typedef struct VmDefinition
{
    const String *const id;
    const OsType osType;
    const String *const osImage;
} VmDefinition;

VmDefinition vmDefinitionList[] =
{
    {
        .id = STRDEF("rh7"),
        .osType = osTypeRhel,
        .osImage = STRDEF("centos:7"),
    },
    {
        .id = STRDEF("u22"),
        .osType = osTypeDebian,
        .osImage = STRDEF("ubuntu:22.04"),
    },
};

/***********************************************************************************************************************************
Generate a list of packages by os type
***********************************************************************************************************************************/
StringList *
vmPackageList(const OsType osType)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(STRING_ID, osType);
    FUNCTION_LOG_END();

    StringList *const result = strLstNew();

    MEM_CONTEXT_TEMP_BEGIN()
    {
        strLstAddZ(result, "libbz2-dev");
        strLstAddZ(result, "ccache");
        strLstAddZ(result, "gcc");
        strLstAddZ(result, "git");
        strLstAddZ(result, "lcov");
        strLstAddZ(result, "lz4");
        strLstAddZ(result, "make");
        strLstAddZ(result, "meson");
        strLstAddZ(result, "openssh-server");
        strLstAddZ(result, "openssl");
        strLstAddZ(result, "perl");
        strLstAddZ(result, "sudo");
        strLstAddZ(result, "valgrind");
        strLstAddZ(result, "wget");
        strLstAddZ(result, "libssh2-1-dev");

        switch (osType)
        {
            case osTypeDebian:
            {
                strLstAddZ(result, "debhelper");
                strLstAddZ(result, "devscripts");
                strLstAddZ(result, "gnupg");
                strLstAddZ(result, "libbz2-dev");
                strLstAddZ(result, "libdbd-pg-perl");
                strLstAddZ(result, "libhtml-parser-perl");
                strLstAddZ(result, "libjson-pp-perl");
                strLstAddZ(result, "liblz4-dev");
                strLstAddZ(result, "liblz4-tool");
                strLstAddZ(result, "libperl-dev"); // !!! Still need this?
                strLstAddZ(result, "libppi-html-perl");
                strLstAddZ(result, "libssh2-1-dev");
                strLstAddZ(result, "libssl-dev");
                strLstAddZ(result, "libtemplate-perl");
                strLstAddZ(result, "libtest-differences-perl");
                strLstAddZ(result, "libxml-checker-perl");
                strLstAddZ(result, "libyaml-dev");
                strLstAddZ(result, "libyaml-libyaml-perl");
                strLstAddZ(result, "libzstd-dev");
                strLstAddZ(result, "lintian");
                strLstAddZ(result, "lsb-release");
                strLstAddZ(result, "pkg-config");
                strLstAddZ(result, "txt2man");
                strLstAddZ(result, "tzdata");
                strLstAddZ(result, "zlib1g-dev");

                break;
            }

            default:
            {
                ASSERT(osType == osTypeRhel);

                strLstAddZ(result, "bzip2-devel");
                strLstAddZ(result, "openssh-clients");
                strLstAddZ(result, "openssl-devel");
                strLstAddZ(result, "perl-JSON-PP");
                strLstAddZ(result, "perl-DBD-Pg");
                strLstAddZ(result, "perl-Digest-SHA");
                strLstAddZ(result, "perl-ExtUtils-Embed");
                strLstAddZ(result, "perl-ExtUtils-MakeMaker");
                strLstAddZ(result, "perl-Test-Simple");
                strLstAddZ(result, "perl-YAML-LibYAML");
                strLstAddZ(result, "libssh2-devel");
                strLstAddZ(result, "libxml2-devel");
                strLstAddZ(result, "libyaml-devel");
                strLstAddZ(result, "libzstd-devel");
                strLstAddZ(result, "lz4-devel");
                strLstAddZ(result, "rpm-build");
                strLstAddZ(result, "zlib-devel");

                break;
            }
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(STRING_LIST, result);
}

/***********************************************************************************************************************************
Render vm script
***********************************************************************************************************************************/
typedef struct RenderData
{
    bool debug;                                                     // In debug mode?
    bool commandLast;                                               // Last added was a command
    String *script;                                                 // Script
} RenderData;

// Helper to render a title
static void
vmRenderTitle(RenderData *const data, const char *const title)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM_P(VOID, data);
        FUNCTION_TEST_PARAM(STRINGZ, title);
    FUNCTION_TEST_END();

    if (data->commandLast)
    {
        strCatChr(data->script, '\n');
        data->commandLast = false;
    }

    if (!strEmpty(data->script))
        strCatChr(data->script, '\n');

    strCatFmt(data->script, "# %s\n", title);

    FUNCTION_TEST_RETURN_VOID();
}

// Helper to render a command
static void
vmRenderCommand(RenderData *const data, const char *const command)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM_P(VOID, data);
        FUNCTION_TEST_PARAM(STRINGZ, command);
    FUNCTION_TEST_END();

    if (data->commandLast)
    {
        strCatChr(data->script, '\n');
    }

    strCatFmt(data->script, "RUN %s", command);
    data->commandLast = true;

    FUNCTION_TEST_RETURN_VOID();
}

String *
vmRender(
    const VmDefinition *const vmDef, const bool debug, const StringList *const packageList, const StringList *const pgList,
    const String *const pgBeta)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(STRING, vmDef->id);
        FUNCTION_LOG_PARAM(BOOL, debug);
        FUNCTION_LOG_PARAM(STRING_LIST, packageList);
        FUNCTION_LOG_PARAM(STRING_LIST, pgList);
        FUNCTION_LOG_PARAM(STRING, pgBeta);
    FUNCTION_LOG_END();

    RenderData result = {.script = strNew(), .debug = debug};

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Base container
        vmRenderTitle(&result, "Base container");
        strCatFmt(result.script, "FROM %s\n", strZ(vmDef->osImage));

        // Install packages
        String *const packagePre = strNew();

        vmRenderTitle(&result, "Install packages");

        switch (vmDef->osType)
        {
            case osTypeDebian:
            {
                vmRenderCommand(&result, "apt-get update");
                strCatZ(
                    packagePre,
                    "export DEBCONF_NONINTERACTIVE_SEEN=true DEBIAN_FRONTEND=noninteractive apt-get install -y"
                    " --no-install-recommends");

                break;
            }

            default:
            {
                ASSERT(vmDef->osType == osTypeRhel);

                break;
            }
        }

        for (unsigned int packageIdx = 0; packageIdx < strLstSize(packageList); packageIdx++)
            vmRenderCommand(&result, zNewFmt("%s %s", strZ(packagePre), strZ(strLstGet(packageList, packageIdx))));

        // Install PostgreSQL packages
        if (!strLstEmpty(pgList))
        {
            vmRenderTitle(&result, "Install PostgreSQL packages");

            switch (vmDef->osType)
            {
                case osTypeDebian:
                {
                    vmRenderCommand(
                        &result,
                        strZ(
                            strNewFmt(
                                "echo \"deb http://apt.postgresql.org/pub/repos/apt/ $(lsb_release -s -c)-pgdg main%s\" >>"
                                " /etc/apt/sources.list.d/pgdg.list",
                                pgBeta == NULL ? "" : zNewFmt(" %s", strZ(pgBeta)))));
                    vmRenderCommand(
                        &result, "wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -");
                    vmRenderCommand(&result, "apt-get update");
                    vmRenderCommand(&result, zNewFmt("%s postgresql-common libpq-dev", strZ(packagePre)));
                    vmRenderCommand(
                        &result,
                        "sed -i 's/^\\#create\\_main\\_cluster.*$/create\\_main\\_cluster \\= false/'"
                        " /etc/postgresql-common/createcluster.conf");

                    for (unsigned int pgIdx = 0; pgIdx < strLstSize(pgList); pgIdx++)
                        vmRenderCommand(&result, zNewFmt("%s postgresql-%s", strZ(packagePre), strZ(strLstGet(pgList, pgIdx))));

                    break;
                }

                default:
                {
                    ASSERT(vmDef->osType == osTypeRhel);

                    break;
                }
            }
        }
    }
    MEM_CONTEXT_TEMP_END();

    // Line feed after last command
    strCatChr(result.script, '\n');

    FUNCTION_LOG_RETURN(STRING, result.script);
}

/**********************************************************************************************************************************/
// Helper to find vm definitions
static const VmDefinition *
vmDefinition(const String *const vm)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(STRING, vm);
    FUNCTION_LOG_END();

    const VmDefinition *result = NULL;

    for (unsigned int vmDefIdx = 0; vmDefIdx < LENGTH_OF(vmDefinitionList); vmDefIdx++)
    {
        if (strEq(vm, vmDefinitionList[vmDefIdx].id))
        {
            result = &vmDefinitionList[vmDefIdx];
            break;
        }
    }

    if (result == NULL)
        THROW_FMT(OptionInvalidError, "'%s' is not valid vm definition", strZ(vm));

    FUNCTION_LOG_RETURN_CONST_P(VOID, result);
}

void
cmdVmBuild(const String *const vm)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(STRING, vm);
    FUNCTION_LOG_END();

    MEM_CONTEXT_TEMP_BEGIN()
    {
        vmRender(vmDefinition(vm), false, vmPackageList(vmDefinition(vm)->osType), strLstNew(), NULL);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}
