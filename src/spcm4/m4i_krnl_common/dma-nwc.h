#ifndef DMA_NWD_H
#define DMA_NWD_H

//######################################################################
//
//  Module:     dma-nwc.h
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
#define SPCM4DRV_DMA_NUMBER_OF_C2S_CHANNELS		3
#define SPCM4DRV_DMA_NUMBER_OF_S2C_CHANNELS		1

#define SPCM4DRV_DMA_DEF_IO_TRANSFER_LENGTH 	(512*_MB_)		    //  512 MByte
#define SPCM4DRV_DMA_REDUCED_TRANSFER_LENGTH 	(30*_MB_)	        //   30 MByte

#define SPCM4DRV_DMA_MAX_NWC_TRANSFER_LENGTH 	(1*_MB_ - 4*_KB_)	// 1020 KByte (DescByteCount[19:0]: 2^20-1 = 1 MB - 1)

#define SPCM4DRV_DMA_DEFAULT_SG_LIST_FACTOR		(128)				// for 128 MB data we need 1 MB sglist buffer
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

// NWD = NorthWest DMA
// Bitmasken für Descriptoren
#define NWD_S2CDESC_BYTECOUNT_MASK          0x000fffff  
#define NWD_S2CDESC_CNTRL_FLAG_SOP          0x80000000  
#define NWD_S2CDESC_CNTRL_FLAG_EOP          0x40000000  
#define NWD_S2CDESC_CNTRL_FLAG_IRQONERROR   0x02000000  
#define NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL   0x01000000  

#define NWD_S2CDESC_STATUS_FLAG_COMPLETE    0x01000000  

#ifndef WINVER
#   include <linux/types.h>
#endif
#include "dma-common.h"

// ----- WINDOWS -----
#ifdef WINVER

// ----- LINUX -----
#else

#   include "../c_header/dlltyp.h"
#   include "../m2i_krnl/spcm2_krnl_general.h"

#   include <linux/version.h>
#   if (LINUX_VERSION_CODE < 0x02061A) // 2.6.26
#       include <asm/semaphore.h>
#   else
#       include <linux/semaphore.h>
#   endif

#endif

#include "nwdcore.h"

//----------------------------------------------------------------------
// structures
//----------------------------------------------------------------------

//Card to System Direction (data flow is from card memory to system memory)
typedef struct _NWCORE_SGLIST_ENTRY
{
	// Die ersten 8 Elements stellen die SG-Liste für den NW Core dar ...
	ULONG dwStatus;         // {S2CDescStatusFlags[7:0], S2CDescStatusErrorFlags[3:0], S2CDescByteCount[19:0]}
	ULONG length2;          // dwUserStatusLow;
	ULONG entryNumber;      // dwUserStatusHigh;
	ULONG entryNumber2;		// wird nicht benötigt: localAddr;        // dwCardAddr;
	ULONG dwControl;        // {S2CDescControlFlags[7:0], DescCardAddr[35:32], DescByteCount[19:0]}
	ULONG pciAddrLow;       // dwSystemAddrLow;
	ULONG pciAddrHigh;      // dwSystemAddrHigh;
	ULONG nextEntryPhysLow; // dwNextDescPtr

} NWCORE_SGLIST_ENTRY, *PNWCORE_SGLIST_ENTRY;

typedef struct _NWC_DMA_PARAMS
{
    // needs to be first entry!
    struct _COMMON_DMA_PARAMS stCommon;

#ifdef WINVER
#else
    NWCORE_SGLIST_ENTRY**  ppstSGListPages; // pages for the scatter-gather list entries for the NorthWest core(M4i, M2p)
#endif

    // Zeiger auf ersten Eintrag der SG-Liste
    PNWCORE_SGLIST_ENTRY pNwcSGListStartEntry;
    // Zeiger auf letzten Eintrag der SG-Liste
    PNWCORE_SGLIST_ENTRY* pNwcSGListLastEntry;
    // Zeiger auf den aktuell nächsten Eintrag der SG-Liste
    PNWCORE_SGLIST_ENTRY pNwcSGListCurrentFirstEntry;
    // Zeiger auf den aktuell letzten Eintrag der SG-Liste
    PNWCORE_SGLIST_ENTRY pNwcSGListCurrentLastEntry;
    // Zeiger auf den aktuell ersten Eintrag der SG-Liste zum Testen auf übertragene Blöcke
    PNWCORE_SGLIST_ENTRY pNwcSGListCurrentFirstTestEntry;
    // Zeiger auf den aktuell ersten Eintrag der SG-Liste, der per SetPointer wieder verfügbar gemacht wird
    PNWCORE_SGLIST_ENTRY pNwcSGListCurrentFirstRefreshedEntry;
    // Zeiger auf den letzten Eintrag der SG-Liste, dessen Nachfolger-Adresse NULL ist (End Of Chain)  
    PNWCORE_SGLIST_ENTRY pNwcSGListCurrentEOCEntry;
    // Zeiger auf den letzten Eintrag der SG-Liste im REG-Modus, bei dem wir ein temporäres INT-Flag eingefügt haben  
    PNWCORE_SGLIST_ENTRY pNwcSGListCurrentLastRegModeEntry;

    BOOLEAN bDelayDMAStart;

} NWC_DMA_PARAMS, *PNWC_DMA_PARAMS;

//----------------------------------------------------------------------
// functions
//----------------------------------------------------------------------





#endif // DMA_NWD_H

