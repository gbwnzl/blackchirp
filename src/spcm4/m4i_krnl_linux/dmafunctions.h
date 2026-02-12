#ifndef DMA_FUNCTIONS_H
#define DMA_FUNCTIONS_H

#include "spcm_linux_card.h"
#include "../m4i_krnl_common/dma-nwc.h"
#include "../m4i_krnl_common/dma-xdma.h"

//----------------------------------------------------------------------
// functions
//----------------------------------------------------------------------
#ifdef WIN32
    // TODO
#else
int8 byNWCDMAObjectInit (SPCM_ST_CARDINFO* pstCard);
void SPCM4DRV_NWC_ClearDma (SPCM_ST_CARDINFO* pContext);
int  SPCM4DRV_NWC_BuildSGList (SPCM_ST_CARDINFO* pContext, void* pvVirtualAddress, uint64 qwByteCount, int8 bReadDir, int8 bGPUUsed, uint64 qwNotifySize, PNWC_DMA_PARAMS pDmaParams, bool* pb64BitAddress);
void SPCM4DRV_NWC_SaveSGList (PSCATTER_GATHER_LIST pWinSGList, void* pParam);
int8 SPCM4DRV_NWC_StartTransfer (SPCM_ST_CARDINFO* pContext, PNWC_DMA_PARAMS pDmaParams, uint64 qwByteOffset, uint64 qwTransferSize, uint32 dwNotifySize);
void SPCM4DRV_NWC_StopTransfer (SPCM_ST_CARDINFO* pContext, NWC_DMA_PARAMS* pDmaParams);
void SPCM4DRV_NWC_ClearData (SPCM_ST_CARDINFO* pContext, NWC_DMA_PARAMS* pDmaParams);
void SPCM4DRV_NWC_RefreshSGList (SPCM_ST_CARDINFO* pContext, PNWC_DMA_PARAMS pDmaParams, uint64 qwNumberOfBytes, bool bFlushSGList);
void SPCM4DRV_NWC_DMA_Restart (SPCM_ST_CARDINFO* pContext, PNWC_DMA_PARAMS pDmaParams, bool fromDPC);
int8 byXDMADMAObjectInit (SPCM_ST_CARDINFO* pstCard);
void SPCM4DRV_XDMA_ClearDma (SPCM_ST_CARDINFO* pContext);
int  SPCM4DRV_XDMA_BuildSGList (SPCM_ST_CARDINFO* pContext, void* pvVirtualAddress, uint64 qwByteCount, int8 bReadDir, int8 bGPUUsed, uint64 qwNotifySize, PXDMA_DMA_PARAMS pDmaParams, bool* pb64BitAddress);
void SPCM4DRV_XDMA_SaveSGList (PSCATTER_GATHER_LIST pWinSGList, void* pParam);
int8 SPCM4DRV_XDMA_StartTransfer (SPCM_ST_CARDINFO* pContext, PXDMA_DMA_PARAMS pDmaParams, uint64 qwByteOffset, uint64 qwTransferSize, uint32 dwNotifySize);
void SPCM4DRV_XDMA_StopTransfer (SPCM_ST_CARDINFO* pContext, XDMA_DMA_PARAMS* pDmaParams);
void SPCM4DRV_XDMA_ClearData (SPCM_ST_CARDINFO* pContext, XDMA_DMA_PARAMS* pDmaParams);
void SPCM4DRV_XDMA_RefreshSGList (SPCM_ST_CARDINFO* pContext, PXDMA_DMA_PARAMS pDmaParams, uint64 qwNumberOfBytes, bool bFlushSGList);
void SPCM4DRV_XDMA_DMA_Restart (SPCM_ST_CARDINFO* pContext, PXDMA_DMA_PARAMS pDmaParams, bool fromDPC);
int8 SPCM4DRV_AllocSGListMemory (SPCM_ST_CARDINFO*, COMMON_DMA_PARAMS* pDmaParams, size_t requestedMemSize, uint32 dwNotifySize_bytes, bool bPagealignedMemoryAddress, size_t* pAllocatedMemSize);
int8 SPCM4DRV_FreeSGListMemory (SPCM_ST_CARDINFO*, COMMON_DMA_PARAMS* pDmaParams);
NWCORE_SGLIST_ENTRY* pstGetNWCSGListEntry  (NWC_DMA_PARAMS*  pDmaParams, uint32 dwIdx);
XDMA_SGLIST_ENTRY*   pstGetXDMASGListEntry (XDMA_DMA_PARAMS* pDmaParams, uint32 dwIdx);
uint32 dwGetIndexOfXDMASGListEntry (const XDMA_DMA_PARAMS* pDmaParams, const XDMA_SGLIST_ENTRY* pstEntry);
XDMA_C2H_WRITEBACK* pstGetXDMAWriteBackEntry (XDMA_DMA_PARAMS* pDmaParams, uint32 dwIdx);
int8 byNWCAllocateMemoryForSGList  (SPCM_ST_CARDINFO*, NWC_DMA_PARAMS*  pDmaParams, bool bPageAlignedMemoryAddress, size_t qwRequestedMemSize_bytes, uint32 dwNotifySize_bytes);
int8 byXDMAAllocateMemoryForSGList (SPCM_ST_CARDINFO*, XDMA_DMA_PARAMS* pDmaParams, bool bPageAlignedMemoryAddress, size_t qwRequestedMemSize_bytes, uint32 dwNotifySize_bytes);
 
#endif


#endif // DMA_FUNCTIONS_H

