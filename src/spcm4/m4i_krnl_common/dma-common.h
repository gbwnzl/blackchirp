#ifndef DMA_COMMON_H
#define DMA_COMMON_H

//######################################################################
//
//  Module:     dma.h
//
//  Descript.:  definition of the driver's dma constants, structures and functions
//
//######################################################################

//#pragma once

//----------------------------------------------------------------------
// DMA
//----------------------------------------------------------------------

//----------------------------------------------------------------------
// contants
//----------------------------------------------------------------------

#define _KB_				 			        (1024)
#define _MB_				 			        (1024*1024)
#define _GB_				 			        (1024*1024*1024)

// ----- WINDOWS -----
#ifdef WINVER

// ----- LINUX -----
#else
#   include <linux/version.h>
#   if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
#       include <asm/semaphore.h>
#   else
#       include <linux/semaphore.h>
#   endif
#   include "../c_header/dlltyp.h"
#   include "../m2i_krnl/spcm2_krnl_general.h"

// to avoid lots of ifdefs in code
typedef uint32 ULONG;
typedef uint32 UINT32;
typedef uint64 ULONG64;
typedef uint64 UINT64;
typedef bool BOOLEAN;

typedef struct _PHYSICAL_ADDRESS
    {
    uint32 LowPart;
    uint32 HighPart;
    } PHYSICAL_ADDRESS, *PPHYSICAL_ADDRESS;

typedef struct _USER_PAGE_LIST
    {
    struct _USER_PAGE_LIST* pstNext;
    struct page* pstUserPage;
    PHYSICAL_ADDRESS Address;
    uint32 Length;
    } USER_PAGE_LIST, *PUSER_PAGE_LIST;
typedef struct _SCATTER_GATHER_ELEMENT
    {
    void* pvUserPageOrPageList; // either a "struct page*" (NWC) or a "struct USER_PAGE_LIST*" (XDMA)
    PHYSICAL_ADDRESS Address;
    uint32 Length;
    } SCATTER_GATHER_ELEMENT, *PSCATTER_GATHER_ELEMENT;

typedef struct _SCATTER_GATHER_LIST
    {
    uint32 NumberOfElements;
    int32  lOrder;
    SCATTER_GATHER_ELEMENT Elements[];
    } SCATTER_GATHER_LIST, *PSCATTER_GATHER_LIST;

#endif

//----------------------------------------------------------------------
// structures
//----------------------------------------------------------------------

typedef struct _COMMON_DMA_PARAMS
{
    // max transfer data
    size_t dmaMaximumTransferLength;
    ULONG dmaMaxMapRegisters;

#ifdef WINVER
    WDFINTERRUPT interruptHandle;

    // two dma enabler 
    WDFDMAENABLER dmaEnablerDesc;
    WDFDMAENABLER dmaEnablerData;

    // the (old WDM) dma adapter object used to build sglists
    PDMA_ADAPTER pDmaAdapter;

    // scatter gather list data
    ULONG dwDmaSGListBufferSize;
    WDFCOMMONBUFFER dmaSGListBuffer;
    PHYSICAL_ADDRESS dmaSGListPhysicalAddress;
    PVOID dmaSGListVirtualAddress;

    WDFDMATRANSACTION dmaTransaction;

#else
    int lIRQ;

    dma_addr_t*  aqwDmaHandles;             // physical addresses of the pages in ppstSGListPages
    ULONG dwDmaSGListBufferSize; 
    PHYSICAL_ADDRESS dmaSGListPhysicalAddress;
    void* dmaSGListVirtualAddress;

    ULONG dwNumOfSGListPages;
    ULONG dwSGListMaxLen;
    size_t qwMaxMappedBufferSize;
    bool bPageAlignedBuffer;

    bool bContMemUsed;
    bool bGPUMemUsed;
    uint64_t qwGPUVirtualAddress;
#endif

    ULONG dmaSGListElements;

    // Gesamtgröße des Transfers in Bytes
    ULONG64 qwBufferSize;

    // Zahl der Datenpuffer des aktuellen Transfers (max. 256)
    // Laut Weidlich: User-Buffer muß in den Kernel-Space gemappt und dort gelockt werden (Auslagerung verboten). 
    // Dieses Mappen kann nur in 30 bzw. 60 MB Happen (je nach OS Breite) erfolgen. Und wird durch die Konstante
    // SPCM4DRV_DMA_MAX_BUFFER_NUMBER limitiert (nicht beliebig vergrößerbar, da daran auch Windows interner Speicher hängt).
    ULONG bufferCount;


#ifdef WINVER
    // Liste der MDLs, die zum Abbilden der Datenpuffer angelegt werden (max. 256)
    PMDL mdlList[SPCM4DRV_DMA_MAX_BUFFER_NUMBER];
#endif

    // Liste der von Windows angelegten SG-Listen für die Datenpuffer (max. 256)
    PSCATTER_GATHER_LIST winSGList[SPCM4DRV_DMA_MAX_BUFFER_NUMBER];

    // Ist ein Transfer (auf diesem Kanal) aktiv?
    BOOLEAN active;
    // Ist ein (DMA-)Ereignis an die Applikation zu senden?
    BOOLEAN somethingToDo;
    //volatile LONG somethingToDo;
    // Hat der aktuelle Transfer die Richtung PC --> PCI-Karte?
    BOOLEAN writeToDevice;

    // Adressoffset der verwendeten DMA Engine, beinhaltet auch die Richtung
    UINT32 dwEngAddrOffs;
    // Adressoffset des Controlregisters der verwendeten DMA Engine, beinhaltet auch die Richtung
    UINT32 dwEngCtrlAddrOffs;

    // aktuelle Busbreite (32Bit als Standard)
    SPCM2_DMA_BUSWIDTH busWidth;

    // Zahl der während eines Transfers bereits übertragenen Bytes
    ULONG64 qwBytesTransfered;
    ULONG64 qwBytesAlreadyFree;
    ULONG dmaSGListRefreshedElements;

    // Schützen der Zugriffe auf 'geteilte' Daten der DMA-Parameter
#ifdef WINVER
    WDFSPINLOCK spinLock;
#else
    struct semaphore semAccess;
#endif

    ULONG isrCount;
    ULONG dpcCount;
    ULONG resCount;

} COMMON_DMA_PARAMS, *PCOMMON_DMA_PARAMS;


//----------------------------------------------------------------------
// functions
//----------------------------------------------------------------------





#endif // DMA_COMMON_H

