#include "spcm_linux_debug.h"

#include <linux/kernel.h>

#ifdef WITHDEBUGPRINT
void SPCM4DRV_DebugPrint (unsigned long dwDebugPrintLevel, unsigned long dwBrdNr, char* szDebugMessage, ...)
    {
    if (dwDebugPrintLevel <= DEBUGLEVEL)
        {
        if (szDebugMessage != NULL)
            {
            va_list vaList;
            va_start (vaList, szDebugMessage);
            vprintk (szDebugMessage, vaList);
            va_end (vaList);
            }
        }
    }

void SPCM4DRV_DebugPrintList (unsigned long dwDebugPrintLevel, unsigned long dwBrdNr, char* szDebugMessage, ...)
    {
    if (dwDebugPrintLevel <= DEBUGLEVEL)
        {
        if (szDebugMessage != NULL)
            {
            va_list vaList;
            va_start (vaList, szDebugMessage);
            vprintk (szDebugMessage, vaList);
            va_end (vaList);
            }
        }
    }
#endif


