/***********************************************************************************************************************************
Vm Build Command
***********************************************************************************************************************************/
#include "build.auto.h"

#include "command/vm/build.h"
#include "common/debug.h"
#include "common/log.h"

/**********************************************************************************************************************************/
void
cmdVmBuild(const String *const vm)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(STRING, vm);
    FUNCTION_LOG_END();

    MEM_CONTEXT_TEMP_BEGIN()
    {
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}
