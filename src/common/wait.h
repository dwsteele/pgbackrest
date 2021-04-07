/***********************************************************************************************************************************
Wait Handler
***********************************************************************************************************************************/
#ifndef COMMON_WAIT_H
#define COMMON_WAIT_H

/***********************************************************************************************************************************
Wait object
***********************************************************************************************************************************/
#define WAIT_TYPE                                                   Wait
#define WAIT_PREFIX                                                 wait

typedef struct Wait Wait;

#include "common/time.h"

/***********************************************************************************************************************************
Constructors
***********************************************************************************************************************************/
Wait *waitNew(TimeMSec waitTime);

/***********************************************************************************************************************************
Getters/Setters
***********************************************************************************************************************************/
typedef struct WaitPub
{
    TimeMSec remainTime;                                            // Wait time remaining (in usec)
} WaitPub;

// How much time is remaining? Recalculated each time waitMore() is called.
__attribute__((always_inline)) static inline TimeMSec
waitRemaining(const Wait *this)
{
    ASSERT_INLINE(this != NULL);
    return ((const WaitPub *)this)->remainTime;
}

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
// Wait and return whether the caller has more time left
bool waitMore(Wait *this);

/***********************************************************************************************************************************
Destructor
***********************************************************************************************************************************/
void waitFree(Wait *this);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
#define FUNCTION_LOG_WAIT_TYPE                                                                                                     \
    Wait *
#define FUNCTION_LOG_WAIT_FORMAT(value, buffer, bufferSize)                                                                        \
    objToLog(value, "Wait", buffer, bufferSize)

#endif
