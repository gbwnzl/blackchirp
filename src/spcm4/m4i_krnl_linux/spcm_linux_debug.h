#ifndef SPCM_LINUX_DEBUG_H
#define SPCM_LINUX_DEBUG_H

/*
**************************************************************************

spcm_linux_debug.h                             (c) Spectrum GmbH,  07/2006

**************************************************************************

debug interface for kernel debug message handling

**************************************************************************
*/


// ----- level of debug messages -----
#define DBG_ALL         99      // every debug message is printed
#define DBG_TRACEALL    6       // trace everything
#define DBG_TRACESIR    5       // trace all communication + ISR (except single read/write)
#define DBG_TRACE       4       // trace all communication (except single read/write, ISR)
#define DBG_WARN        3       // errors and warnings are logged
#define DBG_ERROR       2       // only errors are logged
#define DBG_BOOT        1       // only boot messages are logged
#define DBG_NONE        0       // no logging whatever happens

// ----- this is the currently used debug level -----
#define DEBUGLEVEL DBG_ERROR

// ----- debug macros -----
//#define DEBUGLOG(level,message) if (level<=DEBUGLEVEL) {printk ("spcm4: "); printk message;}
#define DEBUGLOG(level,...) if (level<=DEBUGLEVEL) {printk ("spcm4: " __VA_ARGS__);}

#define TraceEvent(Level, Dbg, ...)
#define SPCM4Print(...) {printk(__VA_ARGS__);}
#define SPCM4DRV_PrintList(...) {printk(__VA_ARGS__);}

#define WITHDEBUGPRINT
#ifdef WITHDEBUGPRINT
void SPCM4DRV_DebugPrint (unsigned long dwDebugPrintLevel, unsigned long dwBrdNr, char* szDebugMessage, ...);
void SPCM4DRV_DebugPrintList (unsigned long dwDebugPrintLevel, unsigned long dwBrdNr, char* szDebugMessage, ...);
#else
#define SPCM4DRV_DebugPrint(dwDebugPrintLevel, dwBrdNr, szDebugMessage, ...)
#define SPCM4DRV_DebugPrintList(dwDebugPrintLevel, dwBrdNr, szDebugMessage, ...)

#endif

#endif // SPCM_LINUX_DEBUG_H

