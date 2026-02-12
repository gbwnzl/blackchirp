/*
**************************************************************************

spcm_linux_ioctl.h                             (c) Spectrum GmbH,  07/2006

**************************************************************************

Ioctl codes for the Spectrum linux driver

**************************************************************************
*/

#ifndef SPCM_LINUX_IOCTL_H
#define SPCM_LINUX_IOCTL_H

#include "../m2i_krnl/spcm2_krnl_general.h"

// get version
typedef struct
    {
    uint32 dwKernelVersion;
    uint32 dwInterfaceVersion;
    } SPCM_IOCTL_GETVERSION;

// read/write single access
typedef struct
    {
    SPCM2_LISTCMD   dwCmd;
    uint32          dwOffset;
    uint32          dwValue;
    } SPCM_IOCTL_RWSINGLE;

// read/write list
typedef struct
    {
    SPCM2_LISTCMD*  pdwCmdList;
    uint32*         pdwOffsetList;
    uint32*         pdwDataList;
    uint32          dwListLen;
    } SPCM_IOCTL_RWLIST;

// read card status
typedef struct
    {
    uint32          dwCardStatus;
    uint64          qwFIFOStatus;
    uint64          qwDMA0UsedBuf;
    uint64          qwDMA1UsedBuf;
    } SPCM_IOCTL_CARDSTATUS;


// DMA config
typedef struct
    {
    uint32          dwDMAChannel;
    uint32          dwBusWidth;
    } SPCM_IOCTL_DMACONFIG;

// DMA SetBuffer
typedef struct
    {
    uint32          dwDMAChannel;
    uint32          bDirWrite_64BitUsed;
    void*           pvUserBuffer;
    uint64          qwBufLen;
    uint32          dwDMAPageSize;
    } SPCM_IOCTL_DMASETBUFFER;


// DMA start/stop/avail bytes
typedef struct
    {
    uint32          dwDMAChannel;
    uint64          qwByteLen;
    uint64          qwByteOffset;
    uint32          dwDMAPageSize;
    } SPCM_IOCTL_DMASTARTSTOP;

// Event wait/signal
typedef struct
    {
    uint32          dwReserved;    // reserved for future use
    } SPCM_IOCTL_EVENTWAITSIGNAL;

// ContMem read out
typedef struct
    {
    void*           pvContMemAdr;
    uint64          qwContMemLen;
    } SPCM_IOCTL_DMACONTMEM;

// Kernel Infos
#define SPCM_KERNELTYPE_M2M3 1
#define SPCM_KERNELTYPE_M4   2
typedef struct
    {
    uint32 dwKernelDriverType;
    uint32 dwNumDevices;
    } SPCM_IOCTL_GETINFO;

// device properties
typedef struct
    {
    uint32 dwPCIBus;
    uint32 dwPCISlot;
    uint32 dwPCIDev;
    uint32 dwPCIFunc;
    } SPCM_IOCTL_GETPROPERTIES;

// global defines
#define SPCM_IOC_MAGIC 0xD0
#define SPCM_IOC_MAXNR 16



// kernel ioctl commands
#define SPCM_IOC_GETVERSION     _IOR  (SPCM_IOC_MAGIC, 1, SPCM_IOCTL_GETVERSION)
#define SPCM_IOC_READSINGLE     _IOR  (SPCM_IOC_MAGIC, 2, SPCM_IOCTL_RWSINGLE)
#define SPCM_IOC_WRITESINGLE    _IOW  (SPCM_IOC_MAGIC, 3, SPCM_IOCTL_RWSINGLE)
#define SPCM_IOC_RWLIST         _IOWR (SPCM_IOC_MAGIC, 4, SPCM_IOCTL_RWLIST)
#define SPCM_IOC_CARDSTATUS     _IOR  (SPCM_IOC_MAGIC, 5, SPCM_IOCTL_CARDSTATUS)
#define SPCM_IOC_DMACONFIG      _IOW  (SPCM_IOC_MAGIC, 6, SPCM_IOCTL_DMACONFIG)
#define SPCM_IOC_DMASETBUF      _IOWR (SPCM_IOC_MAGIC, 7, SPCM_IOCTL_DMASETBUFFER)
#define SPCM_IOC_DMASTART       _IOW  (SPCM_IOC_MAGIC, 8, SPCM_IOCTL_DMASTARTSTOP)
#define SPCM_IOC_DMASTOP        _IOW  (SPCM_IOC_MAGIC, 9, SPCM_IOCTL_DMASTARTSTOP)
#define SPCM_IOC_DMAAVAIL       _IOW  (SPCM_IOC_MAGIC,10, SPCM_IOCTL_DMASTARTSTOP)
#define SPCM_IOC_DMACLEAR       _IOW  (SPCM_IOC_MAGIC,11, SPCM_IOCTL_DMASTARTSTOP)
#define SPCM_IOC_EVENTWAIT      _IOW  (SPCM_IOC_MAGIC,12, SPCM_IOCTL_EVENTWAITSIGNAL)
#define SPCM_IOC_EVENTSIGNAL    _IOW  (SPCM_IOC_MAGIC,13, SPCM_IOCTL_EVENTWAITSIGNAL)
#define SPCM_IOC_GETCONTMEM     _IOR  (SPCM_IOC_MAGIC,14, SPCM_IOCTL_DMACONTMEM)
#define SPCM_IOC_GETINFO        _IOR  (SPCM_IOC_MAGIC,15, SPCM_IOCTL_GETINFO)
#define SPCM_IOC_GETPROPERTIES  _IOR  (SPCM_IOC_MAGIC,16, SPCM_IOCTL_GETPROPERTIES)


#endif // SPCM_LINUX_IOCTL_H

