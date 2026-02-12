//######################################################################
//
//  Module:     sglistmem.cpp
//
//  Descript.:  allocation of memory for the scatter gather list
//
//######################################################################

//----------------------------------------------------------------------
// SGLISTMEM
//----------------------------------------------------------------------

#ifdef WINVER
#   include "ntddk.h"
#   include "wdf.h"

#   include "m4i_krnl_wdm/prototypes.h"
#   include "m4i_krnl_wdm/spcm4drv.h"

#   include ".\m4i_krnl_wdm\trace.h"
#   include "sglistmem.tmh"
#else
#   include <linux/mm.h>
#   include "dma-nwc.h"
#   include "dma-xdma.h"
#   include "../m4i_krnl_linux/dmafunctions.h"
#   include "../m4i_krnl_linux/spcm_linux_card.h"
#   include "../m4i_krnl_linux/spcm_linux_debug.h"

#   include "../c_header/dlltyp.h"

#endif

//------ SPCM4DRV_AllocSGListMemory ------
#ifdef WINVER
NTSTATUS SPCM4DRV_AllocSGListMemory (DMA_CORE eDMACore, COMMON_DMA_PARAMS* pDmaParams, size_t requestedMemSize, size_t* pAllocatedMemSize )
{
    NTSTATUS status;
    WDF_COMMON_BUFFER_CONFIG commonBufConfig;
    size_t sgListMemSize;

    sgListMemSize = requestedMemSize;

    do 
    {
        WDF_COMMON_BUFFER_CONFIG_INIT( &commonBufConfig, FILE_32_BYTE_ALIGNMENT );

        // allocate memory for the sglist descriptors
        status = WdfCommonBufferCreateWithConfig( pDmaParams->dmaEnablerDesc,
                                                  sgListMemSize,
                                                  &commonBufConfig,
                                                  WDF_NO_OBJECT_ATTRIBUTES,
                                                  &pDmaParams->dmaSGListBuffer );
        
        if (!NT_SUCCESS(status)) 
            sgListMemSize /= 2;

    } while( !NT_SUCCESS(status) && 
             (sgListMemSize > SPCM4DRV_DMA_MIN_SG_LIST_MEM_SIZE) );
    
    if (!NT_SUCCESS(status)) 
    {
        TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfCommonBufferCreate failed with status %!STATUS!", status);
        SPCM4Print(("AllocSGListMemory: WdfCommonBufferCreate failed! status=0x%x", status));
    }
    else
    {
        *pAllocatedMemSize = sgListMemSize;

        pDmaParams->dwDmaSGListBufferSize    =(ULONG) sgListMemSize;
        pDmaParams->dmaSGListPhysicalAddress = WdfCommonBufferGetAlignedLogicalAddress( pDmaParams->dmaSGListBuffer );
        pDmaParams->dmaSGListVirtualAddress  = WdfCommonBufferGetAlignedVirtualAddress( pDmaParams->dmaSGListBuffer );

        if (eDMACore == NWC)
            ((NWC_DMA_PARAMS*)pDmaParams)->pNwcSGListStartEntry     = (PNWCORE_SGLIST_ENTRY)pDmaParams->dmaSGListVirtualAddress;
        else
            {
            ((XDMA_DMA_PARAMS*)pDmaParams)->pXDMASGListStartEntry  = (XDMA_SGLIST_ENTRY*)pDmaParams->dmaSGListVirtualAddress;

            // for C2H DMA we need additional space for metadata write back
            if ((pDmaParams->dwEngAddrOffs & XDMA_TARGET_MASK) == XDMA_TARGET_C2H_SGDMA)
                {
                WDF_COMMON_BUFFER_CONFIG_INIT (&commonBufConfig, FILE_32_BYTE_ALIGNMENT);

                // one write-back entry for each SG list entry
                size_t writeBackMemSize = sgListMemSize / sizeof (XDMA_SGLIST_ENTRY) * sizeof (XDMA_C2H_WRITEBACK);

                // allocate memory for the write back entries
                status = WdfCommonBufferCreateWithConfig (pDmaParams->dmaEnablerDesc,
                                                      writeBackMemSize,
                                                      &commonBufConfig,
                                                      WDF_NO_OBJECT_ATTRIBUTES,
                                                      &((XDMA_DMA_PARAMS*)pDmaParams)->dmaWriteBackBuffer);
                if (!NT_SUCCESS(status)) 
                    {
                    SPCM4Print(("AllocSGListMemory: WdfCommonBufferCreate(WB) failed! status=0x%x", status));
                    }
                else
                    {
                    ((XDMA_DMA_PARAMS*)pDmaParams)->dmaWriteBackPhysicalAddress = WdfCommonBufferGetAlignedLogicalAddress (((XDMA_DMA_PARAMS*)pDmaParams)->dmaWriteBackBuffer);
                    ((XDMA_DMA_PARAMS*)pDmaParams)->dmaWriteBackVirtualAddress  = WdfCommonBufferGetAlignedVirtualAddress (((XDMA_DMA_PARAMS*)pDmaParams)->dmaWriteBackBuffer);
                    ((XDMA_DMA_PARAMS*)pDmaParams)->pstWriteBackStartEntry = (XDMA_C2H_WRITEBACK*) ((XDMA_DMA_PARAMS*)pDmaParams)->dmaWriteBackVirtualAddress;
                    }
                }
            }

        TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "%d KB allocated", sgListMemSize / _KB_);

        SPCM4Print(("AllocSGListMemory ok! %d KB requested", requestedMemSize / _KB_ ));
        SPCM4Print(("AllocSGListMemory ok! %d KB allocated", sgListMemSize / _KB_ ));

        SPCM4Print(("  physAddr: 0x%x:0x%x", pDmaParams->dmaSGListPhysicalAddress.HighPart,
                                             pDmaParams->dmaSGListPhysicalAddress.LowPart ));
        SPCM4Print(("  virtAddr: 0x%x", pDmaParams->dmaSGListVirtualAddress ));
    }

    return status;
}
#else // linux
int8 SPCM4DRV_FreeSGListMemory (SPCM_ST_CARDINFO* pstCardInfo, COMMON_DMA_PARAMS* pDmaParams);

int8 SPCM4DRV_AllocSGListMemory (SPCM_ST_CARDINFO* pstCardInfo, COMMON_DMA_PARAMS* pDmaParams, size_t requestedMemSize_bytes, uint32 dwNotifySize_bytes, bool bPageAlignedBufferAddress, size_t* pAllocatedMemSize)
    {
    uint64 qwPhysicalAddress = 0;
    uint32 i = 0;

    uint32 dwNeededSGEntries = (requestedMemSize_bytes + (PAGE_SIZE - 1)) / PAGE_SIZE;
    uint32 dwSGEntriesPerPage = 0;
    if (pstCardInfo->eDMACore == NWC)
        dwSGEntriesPerPage = PAGE_SIZE / sizeof (NWCORE_SGLIST_ENTRY);
    else
        dwSGEntriesPerPage = PAGE_SIZE / sizeof (XDMA_SGLIST_ENTRY);
    if ((dwNotifySize_bytes < PAGE_SIZE) && (dwNotifySize_bytes != 0)) // if notify size is smaller than one page, we need more SG entries and therefore more space for SG-list
        dwNeededSGEntries *= PAGE_SIZE / dwNotifySize_bytes;

    if (!bPageAlignedBufferAddress) // if buffer is not page-aligned it uses one page more
        {
        dwNeededSGEntries++;

        // if buffer is not page-aligned, each notify size interrupt will create one additional SG-list entry
        if (dwNotifySize_bytes != 0)
            {
            uint32 dwNumNotifySizeInterrupts = requestedMemSize_bytes / dwNotifySize_bytes;
            dwNeededSGEntries += dwNumNotifySizeInterrupts;
            }
        }

    pDmaParams->qwMaxMappedBufferSize   = 0; // init with zero, will be filled below if everything worked
    pDmaParams->dmaSGListVirtualAddress = NULL;
    if (pstCardInfo->eDMACore == NWC)
        ((NWC_DMA_PARAMS*)pDmaParams)->pNwcSGListStartEntry    = NULL;
    else
        ((XDMA_DMA_PARAMS*)pDmaParams)->pXDMASGListStartEntry    = NULL;
    pDmaParams->dmaSGListElements       = 0;
    pDmaParams->dmaSGListPhysicalAddress.LowPart  = 0;
    pDmaParams->dmaSGListPhysicalAddress.HighPart = 0;
    ((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages  = 0;

    // examine the number of pages we need to store the requested SG-List,
    // one more if number can't be divided by 128
    if (pstCardInfo->eDMACore == NWC)
        pDmaParams->dmaMaxMapRegisters = requestedMemSize_bytes / sizeof (NWCORE_SGLIST_ENTRY);
    else
        pDmaParams->dmaMaxMapRegisters = requestedMemSize_bytes / sizeof (XDMA_SGLIST_ENTRY);
    pDmaParams->dwNumOfSGListPages = ((dwNeededSGEntries + (dwSGEntriesPerPage - 1)) / dwSGEntriesPerPage);
    pDmaParams->dwSGListMaxLen = pDmaParams->dwNumOfSGListPages * dwSGEntriesPerPage;
    DEBUGLOG (DBG_TRACE, "ReqMemSize: %zu NeededSGEntries: %u EntriesPerPage: %u\n", requestedMemSize_bytes, dwNeededSGEntries, dwSGEntriesPerPage);

    if (pstCardInfo->eDMACore == NWC)
        {
        // allocate a space for the page addresses
        ((NWC_DMA_PARAMS*)pDmaParams)->ppstSGListPages = kmalloc (pDmaParams->dwNumOfSGListPages * sizeof (void*), GFP_KERNEL);
        if (!((NWC_DMA_PARAMS*)pDmaParams)->ppstSGListPages)
            {
            DEBUGLOG (DBG_ERROR, "%s - kmalloc of %zu bytes for SG-list failed\n", __FUNCTION__, pDmaParams->dwNumOfSGListPages * sizeof (void*));

            pDmaParams->dwNumOfSGListPages = 0;
            return -EFAULT;
            }
        memset (((NWC_DMA_PARAMS*)pDmaParams)->ppstSGListPages, 0, pDmaParams->dwNumOfSGListPages * sizeof (void*));
        }
    else
        {
        // Metadata WriteBack
        uint32 dwNumWBEntries = pDmaParams->dwNumOfSGListPages * dwSGEntriesPerPage; // one WB entry for each SG entry
        uint32 dwWBEntriesPerPage = PAGE_SIZE / sizeof (XDMA_C2H_WRITEBACK);
        ((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages = (dwNumWBEntries + (dwWBEntriesPerPage - 1)) / dwWBEntriesPerPage;

        ((XDMA_DMA_PARAMS*)pDmaParams)->ppstWriteBackPages = kmalloc (((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages * sizeof (void*), GFP_KERNEL);
        if (!((XDMA_DMA_PARAMS*)pDmaParams)->ppstWriteBackPages)
            {
            DEBUGLOG (DBG_ERROR, "%s - kmalloc of %zu bytes for write-back failed\n", __FUNCTION__, ((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages * sizeof (void*));

            ((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages = 0;
            return -EFAULT;
            }
        memset (((XDMA_DMA_PARAMS*)pDmaParams)->ppstWriteBackPages, 0, ((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages * sizeof (void*));

        // allocate a space for the page addresses
        ((XDMA_DMA_PARAMS*)pDmaParams)->ppstSGListPages = kmalloc (pDmaParams->dwNumOfSGListPages * sizeof (void*), GFP_KERNEL);
        if (!((XDMA_DMA_PARAMS*)pDmaParams)->ppstSGListPages)
            {
            DEBUGLOG (DBG_ERROR, "%s - kmalloc of %zu bytes for SG-list failed\n", __FUNCTION__, pDmaParams->dwNumOfSGListPages * sizeof (void*));

            pDmaParams->dwNumOfSGListPages = 0;
            return -EFAULT;
            }
        memset (((XDMA_DMA_PARAMS*)pDmaParams)->ppstSGListPages, 0, pDmaParams->dwNumOfSGListPages * sizeof (void*));
        }


    pDmaParams->aqwDmaHandles = kmalloc (pDmaParams->dwNumOfSGListPages * sizeof (dma_addr_t), GFP_KERNEL);
    if (!pDmaParams->aqwDmaHandles)
        {
        DEBUGLOG (DBG_ERROR, "%s - kmalloc(DmaHandles) of %zu bytes failed\n", __FUNCTION__, pDmaParams->dwNumOfSGListPages * sizeof (dma_addr_t));

        pDmaParams->dwNumOfSGListPages = 0;
        SPCM4DRV_FreeSGListMemory (pstCardInfo, pDmaParams);

        return -EFAULT;
        }
    memset (pDmaParams->aqwDmaHandles, 0, pDmaParams->dwNumOfSGListPages * sizeof (dma_addr_t));

    if (pstCardInfo->eDMACore == XDMA)
        {
        ((XDMA_DMA_PARAMS*)pDmaParams)->aqwWriteBackDmaHandles = kmalloc (((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages * sizeof (dma_addr_t), GFP_KERNEL);
        if (!((XDMA_DMA_PARAMS*)pDmaParams)->aqwWriteBackDmaHandles)
            {
            DEBUGLOG (DBG_ERROR, "%s - kmalloc(DmaHandles (WB)) of %zu bytes failed\n", __FUNCTION__, ((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages * sizeof (dma_addr_t));

            ((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages = 0;
            SPCM4DRV_FreeSGListMemory (pstCardInfo, pDmaParams);

            return -EFAULT;
            }
        memset (((XDMA_DMA_PARAMS*)pDmaParams)->aqwWriteBackDmaHandles, 0, ((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages * sizeof (dma_addr_t));
        }

    // get the pages and store them in our page list
    DEBUGLOG (DBG_TRACE, "Allocate %d pages for Scatter Gather list\n", pDmaParams->dwNumOfSGListPages);
    for (i = 0; i < pDmaParams->dwNumOfSGListPages; i++)
        {
        bool bSGListAllocationError = false;
        if (pstCardInfo->eDMACore == NWC)
            {
            // we try to use GFP_DMA32 as we need the SG List located inside 32 bit address space
            #ifdef GFP_DMA32
                ((NWC_DMA_PARAMS*)pDmaParams)->ppstSGListPages[i] = dma_alloc_coherent (&pstCardInfo->pstPCIDevice->dev, PAGE_SIZE, pDmaParams->aqwDmaHandles + i, GFP_KERNEL | GFP_DMA32);

            // if we don't have it we first try ZONE_NORMAL (works on 32 bit and by chance also on 64 bit)
            #else
                ((NWC_DMA_PARAMS*)pDmaParams)->ppstSGListPages[i] = dma_alloc_coherent (&pstCardInfo->pstPCIDevice->dev, PAGE_SIZE, pDmaParams->aqwDmaHandles + i, GFP_KERNEL);

                // if the page is located above 32 bit we release it and re-allocate it in ZONE_DMA thus living with the 16MB limit
                #ifdef _LINUX64
                    if (((pDmaParams->aqwDmaHandles[i]) >> 32) != 0)
                        {
                        dma_free_coherent (&pstCardInfo->pstPCIDevice->dev, PAGE_SIZE, pDmaParams->ppstSGListPages[i], pDmaParams->aqwDmaHandles[i]);
                        ((NWC_DMA_PARAMS*)pDmaParams)->ppstSGListPages[i] = dma_alloc_coherent (&pstCardInfo->pstPCIDevice->dev, PAGE_SIZE, pDmaParams->aqwDmaHandles + i, GFP_KERNEL | GFP_DMA);
                        }
                #endif
            #endif
            if (!((NWC_DMA_PARAMS*)pDmaParams)->ppstSGListPages[i])
                bSGListAllocationError = true;
            }
        else
            {
            // Xilinx DMA core supports 64bit address for scatter-gather list :)
            ((XDMA_DMA_PARAMS*)pDmaParams)->ppstSGListPages[i] = dma_alloc_coherent (&pstCardInfo->pstPCIDevice->dev, PAGE_SIZE, pDmaParams->aqwDmaHandles + i, GFP_KERNEL);
            if (!((XDMA_DMA_PARAMS*)pDmaParams)->ppstSGListPages[i])
                bSGListAllocationError = true;
            }
            

        // failed: we release it and return an error
        if (bSGListAllocationError)
            {
            DEBUGLOG (DBG_ERROR, "dma_alloc_coherent failed -> clean old list\n");
            pDmaParams->dwNumOfSGListPages = i;
            SPCM4DRV_FreeSGListMemory (pstCardInfo, pDmaParams);

            return -EFAULT;
            }
        }
    qwPhysicalAddress = pDmaParams->aqwDmaHandles[0];
    pDmaParams->dmaSGListPhysicalAddress.LowPart  = (uint32)(qwPhysicalAddress & 0xFFFFFFFF);
    pDmaParams->dmaSGListPhysicalAddress.HighPart = (uint32)((qwPhysicalAddress >> 32) & 0xFFFFFFFF);


    if (pstCardInfo->eDMACore == XDMA)
        {
        for (i = 0; i < ((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages; i++)
            {
            ((XDMA_DMA_PARAMS*)pDmaParams)->ppstWriteBackPages[i] = dma_alloc_coherent (&pstCardInfo->pstPCIDevice->dev, PAGE_SIZE, ((XDMA_DMA_PARAMS*)pDmaParams)->aqwWriteBackDmaHandles + i, GFP_KERNEL);
            if (!((XDMA_DMA_PARAMS*)pDmaParams)->ppstWriteBackPages[i])
                {
                DEBUGLOG (DBG_ERROR, "dma_alloc_coherent(WB) failed -> clean old list\n");
                ((XDMA_DMA_PARAMS*)pDmaParams)->dwNumWBPages = i;
                SPCM4DRV_FreeSGListMemory (pstCardInfo, pDmaParams);

                return -EFAULT;
                }
            }
        }

    if (pstCardInfo->eDMACore == NWC)
        {
        NWC_DMA_PARAMS* pNWCDmaParams = (NWC_DMA_PARAMS*)pDmaParams;
        pDmaParams->dmaSGListVirtualAddress  = pNWCDmaParams->ppstSGListPages[0];
        pNWCDmaParams->pNwcSGListStartEntry = (PNWCORE_SGLIST_ENTRY)pDmaParams->dmaSGListVirtualAddress;
        }
    else
        {
        XDMA_DMA_PARAMS* pXDMADmaParams = (XDMA_DMA_PARAMS*)pDmaParams;
        pDmaParams->dmaSGListVirtualAddress  = pXDMADmaParams->ppstSGListPages[0];
        pXDMADmaParams->pXDMASGListStartEntry = (XDMA_SGLIST_ENTRY*)pDmaParams->dmaSGListVirtualAddress;
        pXDMADmaParams->pXDMASGListLastEntry  = (XDMA_SGLIST_ENTRY*)pXDMADmaParams->ppstSGListPages[pDmaParams->dwNumOfSGListPages - 1] + (dwNeededSGEntries % dwSGEntriesPerPage - 1);
        }

    pDmaParams->qwMaxMappedBufferSize = requestedMemSize_bytes;
    pDmaParams->bPageAlignedBuffer    = bPageAlignedBufferAddress;

    return 1;
    }

int8 SPCM4DRV_FreeSGListMemory (SPCM_ST_CARDINFO* pstCardInfo, COMMON_DMA_PARAMS* pDmaParams)
    {
    uint32 i = 0;

    if (pDmaParams == NULL)
        return 0;

    if (pstCardInfo->eDMACore == NWC)
        {
        NWC_DMA_PARAMS* pNWCDmaParams = (NWC_DMA_PARAMS*)pDmaParams;
        for (i = 0; i < pDmaParams->dwNumOfSGListPages; ++i)
            dma_free_coherent (&pstCardInfo->pstPCIDevice->dev, PAGE_SIZE, pNWCDmaParams->ppstSGListPages[i], pDmaParams->aqwDmaHandles[i]);

        if (pNWCDmaParams->ppstSGListPages != NULL)
            kfree (pNWCDmaParams->ppstSGListPages);
        pNWCDmaParams->ppstSGListPages = NULL;
        }
    else
        {
        XDMA_DMA_PARAMS* pXDMADmaParams = (XDMA_DMA_PARAMS*)pDmaParams;

        // ----- free "normal" data stuff -----
        for (i = 0; i < pDmaParams->dwNumOfSGListPages; ++i)
            dma_free_coherent (&pstCardInfo->pstPCIDevice->dev, PAGE_SIZE, pXDMADmaParams->ppstSGListPages[i], pDmaParams->aqwDmaHandles[i]);

        if (pXDMADmaParams->ppstSGListPages != NULL)
            kfree (pXDMADmaParams->ppstSGListPages);
        pXDMADmaParams->ppstSGListPages = NULL;

        // ----- free write-back stuff -----
        for (i = 0; i < pXDMADmaParams->dwNumWBPages; ++i)
            dma_free_coherent (&pstCardInfo->pstPCIDevice->dev, PAGE_SIZE, pXDMADmaParams->ppstWriteBackPages[i], pXDMADmaParams->aqwWriteBackDmaHandles[i]);

        if (pXDMADmaParams->ppstWriteBackPages != NULL)
            kfree (pXDMADmaParams->ppstWriteBackPages);
        pXDMADmaParams->ppstWriteBackPages = NULL;

        if (pXDMADmaParams->aqwWriteBackDmaHandles != NULL)
            kfree (pXDMADmaParams->aqwWriteBackDmaHandles);
        pXDMADmaParams->aqwWriteBackDmaHandles = NULL;

        pXDMADmaParams->dwNumWBPages   = 0;
        }
        
    pDmaParams->dwNumOfSGListPages = 0;

    if (pDmaParams->aqwDmaHandles != NULL)
        kfree (pDmaParams->aqwDmaHandles);
    pDmaParams->aqwDmaHandles = NULL;

    return 1;
    }

#endif

