#ifndef DMA_XDMA_H
#define DMA_XDMA_H

//######################################################################
//
//  Module:     dma-xdma.h
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

#define SPCM4DRV_DMA_REQUIRED_DESC_ALIGNMENT	32                  // DescNextDescPtr[31:5]

#define SPCM4DRV_DMA_MAX_CHANNEL_NUM_PER_DIR    4

#define SPCM4DRV_DMA_DEF_IO_TRANSFER_LENGTH 	(512*_MB_)		    //  512 MByte
#define SPCM4DRV_DMA_REDUCED_TRANSFER_LENGTH 	(30*_MB_)	        //   30 MByte

#define SPCM4DRV_DMA_MAX_XDMA_TRANSFER_LENGTH   ((1ULL<<28) - 1)	// length in SG-descriptor is 28bits

#define SPCM4DRV_DMA_MIN_SG_LIST_MEM_SIZE		(PAGE_SIZE)

#define SPCM4DRV_DMA_DEFAULT_MEMORY_USAGE       (1000)				// [promille] 100 %

#ifdef AMD64
#define SPCM4DRV_DMA_MAX_BUFFER_LENGTH			(1024*1024*30)		// 30 MByte
#else
#define SPCM4DRV_DMA_MAX_BUFFER_LENGTH			(1024*1024*60)		// 60 MByte
#endif
#define SPCM4DRV_DMA_MAX_BUFFER_LENGTH_WINVISTA (2UL*_GB_ - PAGE_SIZE) // 2GB-PageSize
#define SPCM4DRV_DMA_MAX_BUFFER_LENGTH_WIN7     ((0xFFFFFFFF - PAGE_SIZE) + 1) // 4GB-PageSize

#define SPCM4DRV_DMA_MAX_BUFFER_NUMBER 			(256)

#include "dma-common.h"

// ----- WINDOWS -----
#ifdef WINVER

// ----- LINUX -----
#else

#   include <linux/version.h>
#   include "../c_header/dlltyp.h"
#   include "../m2i_krnl/spcm2_krnl_general.h"

#endif

#include "xdmacore.h"

//----------------------------------------------------------------------
// structures
//----------------------------------------------------------------------
typedef struct _XDMA_DMA_PARAMS
{
    // needs to be first entry!
    struct _COMMON_DMA_PARAMS stCommon;

#ifdef WINVER
    // something?
    WDFCOMMONBUFFER     dmaWriteBackBuffer;
    XDMA_C2H_WRITEBACK* pstWriteBackStartEntry;
    PHYSICAL_ADDRESS    dmaWriteBackPhysicalAddress;
    PVOID               dmaWriteBackVirtualAddress;

#else
    XDMA_SGLIST_ENTRY** ppstSGListPages;    // pages fo the scatter-gather list entries for the XDMA core (M5i)

    uint32 dwNumWBPages;
    XDMA_C2H_WRITEBACK** ppstWriteBackPages; // pages for the write back meta data
    dma_addr_t*  aqwWriteBackDmaHandles;    // physical addresses of the pages in ppstWriteBackPages
#endif
    
    XDMA_SGLIST_ENTRY* pXDMASGListStartEntry;                   // Zeiger auf ersten Eintrag der SG-Liste
    XDMA_SGLIST_ENTRY* pXDMASGListLastEntry;                    // Zeiger auf letzten Eintrag der SG-Liste
    XDMA_SGLIST_ENTRY* pXDMASGListCurrentFirstEntry;            // Zeiger auf den aktuell nächsten Eintrag der SG-Liste
    XDMA_SGLIST_ENTRY* pXDMASGListCurrentLastEntry;             // Zeiger auf den aktuell letzten Eintrag der SG-Liste
    XDMA_SGLIST_ENTRY* pXDMASGListCurrentFirstTestEntry;        // Zeiger auf den aktuell ersten Eintrag der SG-Liste zum Testen auf übertragene Blöcke
    XDMA_SGLIST_ENTRY* pXDMASGListCurrentFirstRefreshedEntry;   // Zeiger auf den aktuell ersten Eintrag der SG-Liste, der per SetPointer wieder verfügbar gemacht wird

    XDMA_SGLIST_ENTRY* pXDMASGListCurrentSGListStart;
    ULONG dwNumProcessedCompletedDesc;

    int lCnt;

} XDMA_DMA_PARAMS, *PXDMA_DMA_PARAMS;

//----------------------------------------------------------------------
// functions
//----------------------------------------------------------------------





#endif // DMA_XDMA_H

