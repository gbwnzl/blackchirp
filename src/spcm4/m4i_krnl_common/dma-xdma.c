#ifdef WINVER
#   include "ntddk.h"
#   include "wdf.h"

#   include ".\m4i_krnl_wdm\prototypes.h"
#   include ".\m4i_krnl_wdm\spcm4drv.h"
#   include "dma-xdma.h"
#   include "nwdcore.h"

#   include ".\m4i_krnl_wdm\trace.h"
#   include "dma-xdma.tmh"

static BOOLEAN _printSGList = DEBUG_PRINT_SG_LIST;

#    ifdef DEBUG_RESTART
extern BOOLEAN _DebugRestart;
extern ULONG   __dwDebugLevel;
#   endif // DEBUG_RESTART

VOID XDMASGListDump(ULONG dwBrdNr, PXDMA_DMA_PARAMS pXDMADmaParams);

#   define USLEEP(x) KeStallExecutionProcessor(x)
#   define DMA_LOCK(pDmaParams) WdfSpinLockAcquire (pDmaParams->spinLock)
#   define DMA_UNLOCK(pDmaParams) WdfSpinLockRelease (pDmaParams->spinLock)
#   define INTERRUPT_LOCK(pXDMADmaParams) WdfInterruptAcquireLock(pXDMADmaParams->stCommon.interruptHandle)
#   define INTERRUPT_UNLOCK(pXDMADmaParams) WdfInterruptReleaseLock(pXDMADmaParams->stCommon.interruptHandle )

#   define HIGH_DWORD(qwQuad) ((ULONG)((qwQuad >> 32) & 0xFFFFFFFF))
#   define  LOW_DWORD(qwQuad) ((ULONG)(qwQuad & 0xFFFFFFFF))
#   define QWORD_FROM_DWORD(dwHigh,dwLow) ((((ULONG64)dwHigh) << 32) | dwLow)

ULONG dwGetIndexOfXDMASGListEntry(const XDMA_DMA_PARAMS* pDmaParams, const XDMA_SGLIST_ENTRY* pstEntry)
    {
    #pragma message ("TODO: dwGetIndexOfXDMASGListEntry() korrekt?")
    return (ULONG)(pstEntry - pDmaParams->pXDMASGListStartEntry); // diff of two pointers gets us the difference in XDMA_SGLIST_ENTRYs!
    }

static XDMA_SGLIST_ENTRY* pstGetXDMASGListEntry (XDMA_DMA_PARAMS* pDmaParams, ULONG dwIdx)
    {
    return pDmaParams->pXDMASGListStartEntry + dwIdx;
    }

static ULONG64 qwGetPhysAddr(const XDMA_DMA_PARAMS* pDmaParams, ULONG dwSGEntryNum)
    {
    return QWORD_FROM_DWORD(pDmaParams->stCommon.dmaSGListPhysicalAddress.HighPart, pDmaParams->stCommon.dmaSGListPhysicalAddress.LowPart) + sizeof(XDMA_SGLIST_ENTRY) * dwSGEntryNum;
    }

// WriteBack
XDMA_C2H_WRITEBACK* pstGetXDMAWriteBackEntry (XDMA_DMA_PARAMS* pXDMADmaParams, ULONG dwIdx)
    {
    return pXDMADmaParams->pstWriteBackStartEntry + dwIdx;
    }

static ULONG64 qwGetWriteBackPhysAddr (const XDMA_DMA_PARAMS* pXDMADmaParams, ULONG dwIdx)
    {
    return QWORD_FROM_DWORD(pXDMADmaParams->dmaWriteBackPhysicalAddress.HighPart, pXDMADmaParams->dmaWriteBackPhysicalAddress.LowPart) + sizeof(XDMA_C2H_WRITEBACK) * dwIdx;
    }


#else // Linux
#   include <asm/page.h> // PAGE_SIZE
#   include <linux/pagemap.h> // page_cache_release
#   include "../m4i_krnl_linux/spcm_linux_card.h"
#   include "../m4i_krnl_linux/spcm_linux_debug.h"
#   include "../m4i_krnl_linux/spcm_linux_wrapper.h"
#   include "../m2i_krnl/spcm2_krnl_general.h"
#   include "dma-xdma.h"
#   include "../m4i_krnl_linux/dmafunctions.h"
#   ifdef USE_CUDA_RDMA
#       include "spcm_cuda.h"
#   endif

// to avoid lots of ifdefs in code
#   define FALSE false
#   define TRUE  true

// values from MSDN
#   define TRACE_LEVEL_CRITICAL    1
#   define TRACE_LEVEL_ERROR       2 
#   define TRACE_LEVEL_WARNING     3
#   define TRACE_LEVEL_INFORMATION 4
#   define TRACE_LEVEL_VERBOSE     5

#   define ERROR   DBG_ERROR
#   define WARNING DBG_WARN
#   define TRACE   DBG_TRACE
#   define INFO    DBG_TRACEALL

#   define DBG_INIT 0 // sinnvoll?
#   define DBG_DMA  1 // sinnvoll?

static bool _printSGList = false;

uint32 dwGetIndexOfXDMASGListEntry (const XDMA_DMA_PARAMS* pDmaParams, const XDMA_SGLIST_ENTRY* pstEntry)
    {
    // TODO: kann man das berechnen? oder irgendwo speichern? jedesmal die ganze Liste durchzuwühlen klingt nach viel Aufwand :(
    uint32 dwPageIdx = 0;
    for (; dwPageIdx < pDmaParams->stCommon.dwNumOfSGListPages; dwPageIdx++)
        {
        if ((pstEntry >= pDmaParams->ppstSGListPages[dwPageIdx]) && (pstEntry < pDmaParams->ppstSGListPages[dwPageIdx] + (PAGE_SIZE / sizeof (XDMA_SGLIST_ENTRY))))
            {
            break;
            }
        }
    return dwPageIdx * (PAGE_SIZE / sizeof (XDMA_SGLIST_ENTRY)) + (pstEntry - pDmaParams->ppstSGListPages[dwPageIdx]); // diff of two pointers gets us the difference in XDMA_SGLIST_ENTRYs!
    }

XDMA_SGLIST_ENTRY* pstGetXDMASGListEntry (XDMA_DMA_PARAMS* pDmaParams, uint32 dwIdx)
    {
    // one page contains PAGE_SIZE / sizeof(XDMA_SGLIST_ENTRY = 4096 / 32 = 128 (=0x80) XDMA_SGLIST_ENTRYs
    return (pDmaParams->ppstSGListPages[dwIdx / (PAGE_SIZE / sizeof (XDMA_SGLIST_ENTRY))] + (dwIdx & (PAGE_SIZE / sizeof (XDMA_SGLIST_ENTRY) - 1)));
    }

static dma_addr_t qwGetPhysAddr (const XDMA_DMA_PARAMS* pDmaParams, uint32 dwIdx)
    {
    // the old way (pre-ARM)
    //return __pa (pstGetXDMASGListEntry (pDmaParams, dwIdx));

    // each DmaHandle belongs to one page, and each page holds  (PAGE_SIZE / sizeof(XDMA_SGLIST_ENTRY) = 128 entries for the scatter gather list
    return pDmaParams->stCommon.aqwDmaHandles[dwIdx / (PAGE_SIZE / sizeof (XDMA_SGLIST_ENTRY))] + (dwIdx & (PAGE_SIZE / sizeof (XDMA_SGLIST_ENTRY) - 1)) * sizeof (XDMA_SGLIST_ENTRY);
    }

// WriteBack
XDMA_C2H_WRITEBACK* pstGetXDMAWriteBackEntry (XDMA_DMA_PARAMS* pDmaParams, uint32 dwIdx)
    {
    // one page contains PAGE_SIZE / sizeof(XDMA_C2H_WRITEBACK) = 4096 / 8 = 512 XDMA_C2H_WRITEBACKs
    return (pDmaParams->ppstWriteBackPages[dwIdx / (PAGE_SIZE / sizeof(XDMA_C2H_WRITEBACK))] + (dwIdx % (PAGE_SIZE / sizeof(XDMA_C2H_WRITEBACK))));
    }

static dma_addr_t qwGetWriteBackPhysAddr (const XDMA_DMA_PARAMS* pDmaParams, uint32 dwIdx)
    {
    // each DmaHandle belongs to one page, and each page holds  (PAGE_SIZE / sizeof(XDMA_C2H_WRITEBACK) = 512 entries for the scatter gather list
    return pDmaParams->aqwWriteBackDmaHandles[dwIdx / (PAGE_SIZE / sizeof(XDMA_C2H_WRITEBACK))] + (dwIdx % (PAGE_SIZE / sizeof(XDMA_C2H_WRITEBACK))) * sizeof (XDMA_C2H_WRITEBACK);
    }

static bool bDMAMappingError (struct device* pstDevice, dma_addr_t qwPhysDMAAddress)
	{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION (2,6,27))
    return dma_mapping_error (pstDevice, qwPhysDMAAddress);
#else
    return dma_mapping_error (qwPhysDMAAddress);
#endif
	}

void XDMASGListDump (uint32 dwBrdNr, PXDMA_DMA_PARAMS pDmaParams);

#   define USLEEP(x) udelay(x)
#   define DMA_LOCK(pDmaParams) down(&pDmaParams->semAccess)
#   define DMA_UNLOCK(pDmaParams) up(&pDmaParams->semAccess)

extern spinlock_t stIRQLock; // defined in spcm_linux_isr.c
#   define INTERRUPT_LOCK(pContext) spin_lock_irqsave(&stIRQLock, dwLocalIRQLockFlags);
#   define INTERRUPT_UNLOCK(pContext) spin_unlock_irqrestore(&stIRQLock, dwLocalIRQLockFlags)

#   define HIGH_DWORD(qwQuad) ((uint32)((qwQuad >> 32) & 0xFFFFFFFF))
#   define  LOW_DWORD(qwQuad) ((uint32)(qwQuad & 0xFFFFFFFF))
#   define QWORD_FROM_DWORD(dwHigh,dwLow) ((((uint64)dwHigh) << 32) | dwLow)
#endif


//------ SPCM4DRV_XDMA_InitDma ------
#ifdef WINVER
NTSTATUS SPCM4DRV_XDMA_InitDma( IN PSPCM4DRV_DEVICE_CONTEXT pContext )
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device;
    ULONG alignReq, channel;
    WDF_DMA_ENABLER_CONFIG dmaConfig;
    WDF_OBJECT_ATTRIBUTES attributes;

    BOOLEAN b64BitAddressesUsed;
    size_t availablePhysMem;
    ULONG physMemUsage;

    size_t maxTransferLength, 
           maxChannelTransferLength,
           maxEnablerLength, 
           fragEnablerLength;

    ULONG maxMapRegisters;
    size_t requestedSGListMemSize, allocatedSGListMemSize;

    size_t              qwContMemSize = 0;
    PVOID               pvContMemSys = NULL;
    PHYSICAL_ADDRESS    paContMemPhysAddr;

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_INIT, "InitDma" );
    SPCM4Print (("InitDMA - Start"));

    // Device-Objekt aus Kontext lesen.
    //-------------------------------------------------
    device = pContext->myDevice;

    // Zunächst fragen wir den auf diesem Rechner verfügbaren physischen Speicher ab.
    //-------------------------------------------------
    status = SPCM4DRV_QueryPhysMemSize (&availablePhysMem, &b64BitAddressesUsed);

    if (!NT_SUCCESS (status))
    {
        TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "QueryPhysMemSize failed with status %!STATUS!", status);
        SPCM4Print(("InitDMA: QueryPhysMemSize failed! status=0x%x", status));

        // Wenn die Abfrage fehl schlägt nehmen wir den Standard-Wert für die max. Transfer-Grösse.
        //-------------------------------------------------
        maxTransferLength = SPCM4DRV_DMA_DEF_IO_TRANSFER_LENGTH;
    }
    else
    {
        pContext->b64BitAddressesUsed = b64BitAddressesUsed;

        TraceEvent(TRACE_LEVEL_INFORMATION, DBG_INIT, "InitDma: availablePhysMem: %d MB (64 bit used: %d; PAGE_SIZE: %u)",
                   availablePhysMem / _MB_,
                   b64BitAddressesUsed,
                   PAGE_SIZE);
        SPCM4Print(("InitDMA: availablePhysMem: %d MB (64 bit used: %d; PAGE_SIZE: %u)",
                    availablePhysMem / _MB_,
                    b64BitAddressesUsed,
                    PAGE_SIZE));

        // Dann fragen wir ab, wieviel Promille des Speichers wir verwenden sollen.
        //-------------------------------------------------
        status = SPCM4DRV_QueryPhysMemUsage (&physMemUsage);

        if (!NT_SUCCESS (status))
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "QueryPhysMemUsage failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: QueryPhysMemUsage failed! status=0x%x", status));

            // Wenn die Abfrage fehlschlägt nehmen wir den Standard-Wert für den Speicher-Anteil.
            //-------------------------------------------------
            physMemUsage = SPCM4DRV_DMA_DEFAULT_MEMORY_USAGE;
        }

        // Wir berechnen die maximale mögliche Transfer-Grösse
        //-------------------------------------------------
        maxTransferLength = availablePhysMem / 1000 * physMemUsage;
    }

    // Wir begrenzen die maximale Transfer-Grösse, wenn nötig.
    //-------------------------------------------------
    //if (maxTransferLength > SPCM4DRV_DMA_MAX_IO_TRANSFER_LENGTH)
    //    maxTransferLength = SPCM4DRV_DMA_MAX_IO_TRANSFER_LENGTH;


    // Get the current alignment requirement
    //-------------------------------------------------
    alignReq = WdfDeviceGetAlignmentRequirement( device );

    // Set the alignment requirement
    //-------------------------------------------------
    WdfDeviceSetAlignmentRequirement( device, SPCM4DRV_DMA_REQUIRED_DESC_ALIGNMENT-1 );

    // Wir allokieren Ressourcen für die einzelnen Kanäle, zuerst für die c2s-Richtung.
    //-------------------------------------------------
    for( channel = 0; channel < SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS; ++channel )
    {
        // Der erste DMA-Enabler wird nur zum Anlegen eines common buffer 
        // für die sglist gebraucht.
        
        // Init enabler configuration (for only one PAGE, because we don't need map registers)
        //-------------------------------------------------
        WDF_DMA_ENABLER_CONFIG_INIT( &dmaConfig,
                                     WdfDmaProfilePacket,
                                     PAGE_SIZE );

        // Create one DMA enabler
        //-------------------------------------------------
        status = WdfDmaEnablerCreate( device,
                                      &dmaConfig,
                                      WDF_NO_OBJECT_ATTRIBUTES,
                                      &pContext->c2sXDMADmaParams[channel].stCommon.dmaEnablerDesc );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaEnablerCreate(c2s-desc) failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: WdfDmaEnablerCreate(c2s-desc) failed! status=0x%x", status));
            return status;
        }

        // Jeweils für die Kanäle 0 versuchen wir den Speicher für die gewünschte max. Transferlänge anzufordern.
        // Für die anderen Kanäle begnügen wir uns mit der 'reduced' Transfer-Größe.
        //-------------------------------------------------
        maxChannelTransferLength = channel == 0 ? maxTransferLength : SPCM4DRV_DMA_REDUCED_TRANSFER_LENGTH; 
            
        // Die Zahl der benötigten 'map register' wird berechnet.
        //-------------------------------------------------
        maxMapRegisters = (ULONG) (maxChannelTransferLength / PAGE_SIZE) + 1;

        // calculate the size of the requested scatter gather list memory in NWCore format
        //-------------------------------------------------
        requestedSGListMemSize = (size_t) (sizeof(XDMA_SGLIST_ENTRY) * maxMapRegisters);
        allocatedSGListMemSize = 0;

        // alloc memory for the scatter gather list
        //-------------------------------------------------
        // we need direction info in AllocSGListMemory
        pContext->c2sXDMADmaParams[channel].stCommon.dwEngAddrOffs = XDMA_TARGET_C2H_SGDMA + (channel << 8);
        pContext->c2sXDMADmaParams[channel].stCommon.dwEngCtrlAddrOffs = XDMA_TARGET_C2H_CHANNELS + (channel << 8);
        status = SPCM4DRV_AllocSGListMemory (XDMA, &pContext->c2sXDMADmaParams[channel].stCommon, 
                                              requestedSGListMemSize, 
                                              &allocatedSGListMemSize );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "AllocSGListMemory(c2s-desc) failed with status %!STATUS!", status);
            return status;
        }
        
        if( channel == 0 )
        {
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "c2s[0] SGList-virt: %p\n", pContext->c2sXDMADmaParams[0].stCommon.dmaSGListVirtualAddress);
        }


        // In Abhängigkeit vom tatsächlich allokierten SGList-Speicher berechnen wir die
        // maximale mögliche Transfer-Grösse für diesen Kanal neu und merken uns diese.
        //-------------------------------------------------
        maxMapRegisters = (ULONG)(allocatedSGListMemSize / sizeof(XDMA_SGLIST_ENTRY));
        maxChannelTransferLength  = maxMapRegisters - 1;
        maxChannelTransferLength *= PAGE_SIZE;
        pContext->c2sXDMADmaParams[channel].stCommon.dmaMaxMapRegisters = maxMapRegisters;
        pContext->c2sXDMADmaParams[channel].stCommon.dmaMaximumTransferLength = maxChannelTransferLength;

        // Init enabler configuration - jetzt für den tatsächlichen Datentransfer.
        //-------------------------------------------------
        WDF_DMA_ENABLER_CONFIG_INIT( &dmaConfig,
                                     WdfDmaProfileScatterGather64,
                                     pContext->c2sXDMADmaParams[channel].stCommon.dmaMaximumTransferLength );


        // Create DMA enabler for data transfers
        //-------------------------------------------------
        status = WdfDmaEnablerCreate( device,
                                      &dmaConfig,
                                      WDF_NO_OBJECT_ATTRIBUTES,
                                      &pContext->c2sXDMADmaParams[channel].stCommon.dmaEnablerData );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaEnablerCreate(c2s-data) failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: WdfDmaEnablerCreate(c2s-data) failed! status=0x%x", status));
            return status;
        }

        // get maximum and fragmented length
        //-------------------------------------------------
        maxEnablerLength  = WdfDmaEnablerGetMaximumLength (pContext->c2sXDMADmaParams[channel].stCommon.dmaEnablerData);
        fragEnablerLength = WdfDmaEnablerGetFragmentLength (pContext->c2sXDMADmaParams[channel].stCommon.dmaEnablerData,
                                                            WdfDmaDirectionReadFromDevice);
        TraceEvent (TRACE_LEVEL_INFORMATION, DBG_INIT, 
                    "C2S[%d]: maxChannelTransferLength:%I64d, maxEnablerLength:%I64d, fragEnablerLength:%I64d", 
                    channel, maxChannelTransferLength, maxEnablerLength, fragEnablerLength );

        SPCM4Print(("InitDMA: C2S[%d] maxMapRegs: %d", channel, maxMapRegisters));
        SPCM4Print(("InitDMA: C2S[%d] maxChannelLength:  %d MB", 
                    channel,
                    maxChannelTransferLength / _MB_));
        SPCM4Print(("InitDMA: C2S[%d] maxEnablerLength:  %d MB", 
                    channel,
                    maxEnablerLength / _MB_));
        SPCM4Print(("InitDMA: C2S[%d] fragEnablerLength: %d MB", 
                    channel,
                    fragEnablerLength / _MB_));

        // Obtain a pointer to WDM DMA adapter structure associated with the data DMA Enabler
        // (Wir haben nur einen Adapter pro Enabler, da wir kein Duplex-Profil ausgewählt hatten.
        //  Daher ist es egal, welche Richtung wir bei der Abfrage angeben.)
        //-------------------------------------------------
        pContext->c2sXDMADmaParams[channel].stCommon.pDmaAdapter = WdfDmaEnablerWdmGetDmaAdapter(pContext->c2sXDMADmaParams[channel].stCommon.dmaEnablerData,
                                                                                    WdfDmaDirectionReadFromDevice );
        if( pContext->c2sXDMADmaParams[channel].stCommon.pDmaAdapter == NULL)
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaEnablerWdmGetDmaAdapter failed!");
            SPCM4Print(("InitDMA: WdfDmaTransactionCreate failed!"));
            return STATUS_UNSUCCESSFUL;
        }

        // Create one DMA transaction per channel
        //-------------------------------------------------
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE( &attributes, TRANSACTION_CONTEXT );
        status = WdfDmaTransactionCreate( pContext->c2sXDMADmaParams[channel].stCommon.dmaEnablerData,
                                          &attributes,
                                          &pContext->c2sXDMADmaParams[channel].stCommon.dmaTransaction );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaTransactionCreate failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: WdfDmaTransactionCreate failed! status=0x%x", status));
            return status;
        }

        pContext->c2sXDMADmaParams[channel].stCommon.active        = FALSE;
        pContext->c2sXDMADmaParams[channel].stCommon.somethingToDo = FALSE;
        pContext->c2sXDMADmaParams[channel].stCommon.busWidth      = BUS_WIDTH_32;
        pContext->c2sXDMADmaParams[channel].stCommon.qwBufferSize  = 0;
        pContext->c2sXDMADmaParams[channel].stCommon.dmaSGListElements = 0;

        // Create a WDFSPINLOCK object to protect accesses to shared channel data
        //-------------------------------------------------
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = device;
        status = WdfSpinLockCreate( &attributes, &pContext->c2sXDMADmaParams[channel].stCommon.spinLock );
        if (!NT_SUCCESS(status))
        {
            TraceEvent (TRACE_LEVEL_ERROR, 
                        DBG_INIT, 
                        "WdfSpinLockCreate (c2s %d) failed with status %!STATUS!", 
                        channel, 
                        status);
            SPCM4Print(("InitDMA: WdfSpinLockCreate (c2s %d) failed! 0x%x\n", 
                        channel,
                        status));
            return status;
        }
    }


    // Wir allokieren Ressourcen für die einzelnen Kanäle, jetzt für die s2c-Richtung.
    //-------------------------------------------------
    for( channel = 0; channel < SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS; ++channel )
    {
        // Der erste DMA-Enabler wird nur zum Anlegen eines common buffer 
        // für die sglist gebraucht.

        // Init enabler configuration (for only one PAGE, because we don't need map registers)
        //-------------------------------------------------
        WDF_DMA_ENABLER_CONFIG_INIT (&dmaConfig,
                                     WdfDmaProfilePacket,
                                     PAGE_SIZE );

        // Create one DMA enabler - 
        //-------------------------------------------------
        status = WdfDmaEnablerCreate( device,
                                      &dmaConfig,
                                      WDF_NO_OBJECT_ATTRIBUTES,
                                      &pContext->s2cXDMADmaParams[channel].stCommon.dmaEnablerDesc );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaEnablerCreate(s2c-desc) failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: WdfDmaEnablerCreate(s2c-desc) failed! status=0x%x", status));
            return status;
        }

        // Jeweils für die Kanäle 0 versuchen wir den Speicher für die gewünschte max. Transferlänge anzufordern.
        // Für die anderen Kanäle begnügen wir uns mit der 'reduced' Transfer-Größe.
        //-------------------------------------------------
        maxChannelTransferLength = channel == 0 ? maxTransferLength : SPCM4DRV_DMA_REDUCED_TRANSFER_LENGTH; 

        // Die Zahl der benötigten 'map register' wird berechnet.
        //-------------------------------------------------
        maxMapRegisters = (ULONG) (maxChannelTransferLength / PAGE_SIZE) + 1;

        // calculate the size of the requested scatter gather list memory in NWCore format
        //-------------------------------------------------
        requestedSGListMemSize = (size_t) (sizeof(XDMA_SGLIST_ENTRY) * maxMapRegisters);
        allocatedSGListMemSize = 0;

        // alloc memory for the scatter gather list
        //-------------------------------------------------
        // we need direction info in AllocSGListMemory
        pContext->s2cXDMADmaParams[channel].stCommon.dwEngAddrOffs = XDMA_TARGET_H2C_SGDMA + (channel << 8);
        pContext->s2cXDMADmaParams[channel].stCommon.dwEngCtrlAddrOffs = XDMA_TARGET_H2C_CHANNELS + (channel << 8);
        status = SPCM4DRV_AllocSGListMemory (XDMA, &pContext->s2cXDMADmaParams[channel].stCommon, 
                                              requestedSGListMemSize, 
                                              &allocatedSGListMemSize );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "AllocSGListMemory(s2c_%d) failed with status %!STATUS!", channel, status);
            SPCM4Print(("InitDMA: WdfDmaEnablerCreate(s2c-desc) failed! status=0x%x", status));
            return status;
        }

        // In Abhängigkeit vom tatsächlich allokierten SGList-Speicher berechnen wir die
        // maximale mögliche Transfer-Grösse für diesen Kanal neu und merken uns diese.
        //-------------------------------------------------
        maxMapRegisters = (ULONG)(allocatedSGListMemSize / sizeof(XDMA_SGLIST_ENTRY));
        maxChannelTransferLength  = maxMapRegisters - 1;
        maxChannelTransferLength *= PAGE_SIZE;
        pContext->s2cXDMADmaParams[channel].stCommon.dmaMaxMapRegisters = maxMapRegisters;
        pContext->s2cXDMADmaParams[channel].stCommon.dmaMaximumTransferLength = maxChannelTransferLength;

        // Init enabler configuration - jetzt für den tatsächlichen Datentransfer.
        //-------------------------------------------------
        WDF_DMA_ENABLER_CONFIG_INIT( &dmaConfig,
                                     WdfDmaProfileScatterGather64,
                                     pContext->s2cXDMADmaParams[channel].stCommon.dmaMaximumTransferLength );


        // Create DMA enabler for data transfers
        //-------------------------------------------------
        status = WdfDmaEnablerCreate( device,
                                      &dmaConfig,
                                      WDF_NO_OBJECT_ATTRIBUTES,
                                      &pContext->s2cXDMADmaParams[channel].stCommon.dmaEnablerData );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaEnablerCreate(s2c-data) failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: WdfDmaEnablerCreate(s2c-data) failed! status=0x%x", status));
            return status;
        }

        // get maximum and fragmented length
        //-------------------------------------------------
        maxEnablerLength  = WdfDmaEnablerGetMaximumLength (pContext->s2cXDMADmaParams[channel].stCommon.dmaEnablerData);
        fragEnablerLength = WdfDmaEnablerGetFragmentLength (pContext->s2cXDMADmaParams[channel].stCommon.dmaEnablerData,
                                                            WdfDmaDirectionWriteToDevice);
        TraceEvent (TRACE_LEVEL_INFORMATION, DBG_INIT, 
                    "S2C[%d]: maxChannelTransferLength:%I64d, maxEnablerLength:%I64d, fragEnablerLength:%I64d", 
                    channel, maxChannelTransferLength, maxEnablerLength, fragEnablerLength );

        SPCM4Print(("InitDMA: S2C[%d] maxChannelLength:  %d MB", 
                    channel,
                    maxChannelTransferLength / _MB_));
        SPCM4Print(("InitDMA: S2C[%d] maxEnablerLength:  %d MB", 
                    channel,
                    maxEnablerLength / _MB_));
        SPCM4Print(("InitDMA: S2C[%d] fragEnablerLength: %d MB", 
                    channel,
                    fragEnablerLength / _MB_));

        // Obtain a pointer to WDM DMA adapter structure associated with the data DMA Enabler
        // (Wir haben nur ein Adaper pro Enabler, da wir kein Duplex-Profil ausgewählt hatten.
        //  Daher ist es egal, welche Richtung wir bei der Abfrage angeben.)
        //-------------------------------------------------
        pContext->s2cXDMADmaParams[channel].stCommon.pDmaAdapter = WdfDmaEnablerWdmGetDmaAdapter(pContext->s2cXDMADmaParams[channel].stCommon.dmaEnablerData,
                                                                                    WdfDmaDirectionWriteToDevice );
        if( pContext->s2cXDMADmaParams[channel].stCommon.pDmaAdapter == NULL)
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaEnablerWdmGetDmaAdapter failed!");
            SPCM4Print(("InitDMA: WdfDmaTransactionCreate failed!"));
            return STATUS_UNSUCCESSFUL;
        }

        // Create one DMA transaction per channel
        //-------------------------------------------------
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE( &attributes, TRANSACTION_CONTEXT );
        status = WdfDmaTransactionCreate( pContext->s2cXDMADmaParams[channel].stCommon.dmaEnablerData,
                                          &attributes,
                                          &pContext->s2cXDMADmaParams[channel].stCommon.dmaTransaction );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaTransactionCreate failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: WdfDmaTransactionCreate failed! status=0x%x", status));
            return status;
        }

        pContext->s2cXDMADmaParams[channel].stCommon.active        = FALSE;
        pContext->s2cXDMADmaParams[channel].stCommon.somethingToDo = FALSE;
        pContext->s2cXDMADmaParams[channel].stCommon.busWidth      = BUS_WIDTH_32;
        pContext->s2cXDMADmaParams[channel].stCommon.qwBufferSize  = 0;
        pContext->s2cXDMADmaParams[channel].stCommon.dmaSGListElements = 0;

        // Create a WDFSPINLOCK object to protect accesses to shared channel data
        //-------------------------------------------------
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = device;
        status = WdfSpinLockCreate( &attributes, &pContext->s2cXDMADmaParams[channel].stCommon.spinLock );
        if (!NT_SUCCESS(status))
        {
            TraceEvent (TRACE_LEVEL_ERROR, 
                        DBG_INIT, 
                        "WdfSpinLockCreate (s2c %d) failed with status %!STATUS!", 
                        channel, 
                        status);
            SPCM4Print(("InitDMA: WdfSpinLockCreate (s2c %d) failed! 0x%x\n", 
                        channel,
                        status));
            return status;
        }
    }

    // Wir fragen ab, ob und wieviel kontinuierlichen Speicher wir anlegen sollen.
    status = SPCM4DRV_QueryContMemSize (&qwContMemSize);
    if (!NT_SUCCESS (status) || (qwContMemSize == 0) )
    {
        TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "QueryContMemSize failed with status %!STATUS!", status);
        SPCM4Print(("InitDma: QueryContMemSize failed! status=0x%08x", status));

        // Wenn die Abfrage fehlschlägt brauchen wir keinen kont. Speicher anzulegen.
        pContext->qwContMemSize = 0;
        pContext->pvContMemSysAddr = NULL;
        pContext->paContMemPhysAddr.QuadPart = 0;
        pContext->pvContMemAppAddr = NULL;
    }
    else
    {
        // try to alloc a large contiguous memory area
        do
        {
            status = SPCM4DRV_AllocContMemory (qwContMemSize, &pvContMemSys );
            if (!NT_SUCCESS (status))
            {
                TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "QueryContMemSize failed with status %!STATUS!", status);
                SPCM4Print (("AllocContMemory failed! size: %d MB", (ULONG) (qwContMemSize / _MB_)));
                qwContMemSize /= 2;
            }
        } 
        while (!NT_SUCCESS(status) && (qwContMemSize > 0));

        // build physiscal address
        paContMemPhysAddr = MmGetPhysicalAddress (pvContMemSys);

        // save contmem data
        pContext->qwContMemSize     = qwContMemSize;
        pContext->pvContMemSysAddr  = pvContMemSys;
        pContext->paContMemPhysAddr = paContMemPhysAddr;
        pContext->pvContMemAppAddr  = NULL;

        // init cont mem with 0
        RtlZeroMemory(pvContMemSys, qwContMemSize);

        TraceEvent(TRACE_LEVEL_INFORMATION, DBG_INIT, "InitDma: %d MB contMem allocated!", qwContMemSize/ _MB_);
        SPCM4Print (("InitDma: %d MB contMem allocated", qwContMemSize / _MB_ ));
    }

    return status;
}

#else // Linux
int8 byXDMADMAObjectInit (SPCM_ST_CARDINFO* pstCard)
    {
    uint32 dwChannel = 0;
    for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_C2S_CHANNELS; ++dwChannel)
        {
        XDMA_DMA_PARAMS* pstXDMADmaParams = pstCard->astXDMA_C2SDMAParams + dwChannel;
        pstXDMADmaParams->stCommon.active = false;
        pstXDMADmaParams->stCommon.somethingToDo = false;
        pstXDMADmaParams->stCommon.busWidth = BUS_WIDTH_32;
        pstXDMADmaParams->stCommon.qwBufferSize = 0;
        pstXDMADmaParams->stCommon.dmaSGListElements = 0;
        pstXDMADmaParams->stCommon.dwEngAddrOffs = XDMA_TARGET_C2H_SGDMA + (dwChannel << 8);
        pstXDMADmaParams->stCommon.dwEngCtrlAddrOffs = XDMA_TARGET_C2H_CHANNELS + (dwChannel << 8);
        pstXDMADmaParams->stCommon.qwMaxMappedBufferSize = 0;
        pstXDMADmaParams->stCommon.bPageAlignedBuffer = false;
        sema_init (&pstXDMADmaParams->stCommon.semAccess, 1);
        }
    for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_S2C_CHANNELS; ++dwChannel)
        {
        XDMA_DMA_PARAMS* pstXDMADmaParams = pstCard->astXDMA_S2CDMAParams + dwChannel;
        pstXDMADmaParams->stCommon.active = false;
        pstXDMADmaParams->stCommon.somethingToDo = false;
        pstXDMADmaParams->stCommon.busWidth = BUS_WIDTH_32;
        pstXDMADmaParams->stCommon.qwBufferSize = 0;
        pstXDMADmaParams->stCommon.dmaSGListElements = 0;
        pstXDMADmaParams->stCommon.dwEngAddrOffs = XDMA_TARGET_H2C_SGDMA + (dwChannel << 8);
        pstXDMADmaParams->stCommon.dwEngCtrlAddrOffs = XDMA_TARGET_H2C_CHANNELS + (dwChannel << 8);
        pstXDMADmaParams->stCommon.qwMaxMappedBufferSize = 0;
        pstXDMADmaParams->stCommon.bPageAlignedBuffer = false;
        sema_init (&pstXDMADmaParams->stCommon.semAccess, 1);
        }
        
    return 0;
    }

int8 byXDMAAllocateMemoryForSGList (SPCM_ST_CARDINFO* pstCardInfo, XDMA_DMA_PARAMS* pstXDMADMAParams, bool bPageAlignedBuffer, size_t qwRequestedMemSize_bytes, uint32 dwNotifySize_bytes)
    {
    size_t allocatedSGListMemSize = 0;
    int8 byStatus = 0;

    // re-use existing buffer if it is large enough
    if ((qwRequestedMemSize_bytes <= pstXDMADMAParams->stCommon.qwMaxMappedBufferSize)
        && (bPageAlignedBuffer == pstXDMADMAParams->stCommon.bPageAlignedBuffer))
        {
        DEBUGLOG (DBG_TRACE, "Reusing buffer\n");
        return 0;
        }

    // clear previously allocated memory
    if (pstXDMADMAParams->stCommon.qwMaxMappedBufferSize != 0)
        {
        DEBUGLOG (DBG_TRACE, "Freeing old buffer\n");
        SPCM4DRV_FreeSGListMemory (pstCardInfo, &pstXDMADMAParams->stCommon);
        }

    // alloc memory for the scatter gather list
    //-------------------------------------------------
    DEBUGLOG (DBG_TRACE, "Allocating buffer for %zu bytes\n", qwRequestedMemSize_bytes);
    byStatus = SPCM4DRV_AllocSGListMemory (pstCardInfo, &pstXDMADMAParams->stCommon, qwRequestedMemSize_bytes, dwNotifySize_bytes, bPageAlignedBuffer, &allocatedSGListMemSize);
    if (byStatus < 0)
        {
        SPCM4Print("AllocateMemoryForSGList: AllocSGListMemory() failed! status=0x%x", byStatus);
        return -EFAULT;
        }

    return 0;
    }


#endif


//------ SPCM4DRV_ClearDma ------
#ifdef WINVER
VOID SPCM4DRV_XDMA_ClearDma (IN PSPCM4DRV_DEVICE_CONTEXT pContext)
#else
void SPCM4DRV_XDMA_ClearDma (SPCM_ST_CARDINFO* pContext)
#endif
{
    // Hier werden die DMA-Ressourcen wieder freigegeben!

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_INIT, "ClearDma" );
    SPCM4Print(("ClearDMA ..."));

    // free cont memory
#ifdef WINVER
    if (pContext->pvContMemSysAddr)
    {
        SPCM4DRV_FreeContMemory (pContext->pvContMemSysAddr);
        pContext->pvContMemSysAddr = NULL;
    }
#else
    // TODO
#endif
}


//------ SPCM4DRV_BuildSGList ------
//
// Diese Funktion dient dazu, Windows zu beauftragen, uns für eine übergebene
// MDL eine Scatter-Gather-Liste zu bauen!
//
#ifdef WINVER
VOID SPCM4DRV_XDMA_BuildSGList (IN PSPCM4DRV_DEVICE_CONTEXT pContext, PMDL pMdl, PXDMA_DMA_PARAMS pXDMADmaParams)
#else
int SPCM4DRV_XDMA_BuildSGList (SPCM_ST_CARDINFO* pstCard, void* pvUserBuffer, uint64 qwByteCount, int8 bReadDir, int8 bGPUUsed, uint64 qwNotifySize, PXDMA_DMA_PARAMS pXDMADmaParams, bool* pb64BitAddress)
#endif
{
    COMMON_DMA_PARAMS* pDmaParams = &pXDMADmaParams->stCommon;
#ifdef WINVER
    PDMA_ADAPTER pDmaAdapter;
    VOID* virtualAddress;
    ULONG byteCount;
    NTSTATUS NTStatus;

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_INIT, "BuildSGList" );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "SPCM4DRV_BuildSGList\n");

    // Wir benötigen den DMA Adapter sowie die Eigenschaften des
    // Puffers (Adresse, Größe, Richtung).
    pDmaAdapter    = pDmaParams->pDmaAdapter;
    virtualAddress = MmGetMdlVirtualAddress (pMdl);
    byteCount      = MmGetMdlByteCount (pMdl);

    // Die MDL speichern wir, um sie später wieder freigeben zu können.
    pDmaParams->mdlList[pDmaParams->bufferCount-1] = pMdl;

    // Leeren der Cache-Speicher (nur ein Platzhalter - vielleicht für später ?)
    KeFlushIoBuffers (pMdl, !pDmaParams->writeToDevice, TRUE);

    // Wir beauftragen den Adapter (also das BS), uns die SG-Liste zu bauen.
    // Bei Erfolg wird die Funktion 'SPCM4DRV_XDMA_SaveSGList' gerufen-
    NTStatus =
    pDmaAdapter->DmaOperations->GetScatterGatherList (pDmaAdapter,
                                                      pContext->pWdmDevObj,
                                                      pMdl,
                                                      virtualAddress,
                                                      byteCount,
                                                      SPCM4DRV_XDMA_SaveSGList,
                                                      pXDMADmaParams,
                                                      pDmaParams->writeToDevice);
    if (NTStatus != STATUS_SUCCESS)
        {
        pDmaParams->winSGList[pDmaParams->bufferCount-1] = NULL;
        SPCM4DRV_DebugPrint (ERROR, pContext->dwBoardNumber, "   BuildSGList: GetScatterGatherList Error: %u\n", NTStatus);
        }

#else // Linux
    // ----- in contrast to windows this function is called only once and processess the complete buffer -----
    uint64 qwDMAPageSize            = 0;
    uint64 qwUserBufOffset          = 0;
    uint32 dwCurrentSGListIdx       = 0;
    uint32 dwCurrentSGElementIdx    = 0;
#   ifdef USE_CUDA_RDMA
    uint32_t dwNVidiaIdx            = 0;
#   endif
    int lNumPages                   = 1;
    enum dma_data_direction eDMADir = (pDmaParams->writeToDevice? DMA_TO_DEVICE : DMA_FROM_DEVICE);
    PSCATTER_GATHER_LIST pstCurrentWinSGList = NULL;
    int lOrder = 0; // __get_free_pages() allocates 2^lOrder pages
    unsigned dwSGEntriesPerWinSGListPage = (PAGE_SIZE - sizeof(uint32)/*NumberOfLements*/ - sizeof (int32)/*lOrder*/) / sizeof (SCATTER_GATHER_ELEMENT);
    unsigned dwNumPagesForBytes = qwByteCount / PAGE_SIZE;
    unsigned dwNumWinSGListPagesForBufferPages = dwNumPagesForBytes / dwSGEntriesPerWinSGListPage + 1;
    unsigned  dwNumWinSGListPagesForBufferPages_old =  dwNumWinSGListPagesForBufferPages;
    int lHighestBitIdx = 0;
    while (dwNumWinSGListPagesForBufferPages_old >>= 1)
        lHighestBitIdx++;
    if( ((0x1 << lHighestBitIdx) - 1) &  dwNumWinSGListPagesForBufferPages)
        lOrder = lHighestBitIdx + 1; // there are bits below MSB set, so we use next-highest power-of-two
    else
        lOrder = lHighestBitIdx; // only MSB set
    // 10 is max
    if (lOrder > 10)
        lOrder = 10;
    lOrder++; // will be substracted again in do-while-loop

    if (down_interruptible (&pDmaParams->semAccess))
        return -EBUSY;

    // check whether we're in our continuous buffer or in a (fragmented) user buffer
    pDmaParams->bContMemUsed = false;
    if (((uint64) (size_t) pvUserBuffer >= pstCard->qwContMemUserStart) && (((uint64) (size_t) pvUserBuffer + qwByteCount) <= (pstCard->qwContMemUserStart + pstCard->qwContMemUserLen)))
        {
        pDmaParams->bContMemUsed = true;
        qwUserBufOffset = ((uint64) (size_t) pvUserBuffer) - pstCard->qwContMemUserStart;
        SPCM4DRV_DebugPrint(DBG_TRACE, pstCard->dwBoardNumber, "%s - using ContMem buffer\n", __FUNCTION__);
        }


    if (qwNotifySize == 0)
        qwNotifySize = qwByteCount;

    if (pDmaParams->bContMemUsed)
        qwDMAPageSize = qwNotifySize;
    else if (qwNotifySize <= PAGE_SIZE)
        qwDMAPageSize = qwNotifySize;
    else
        qwDMAPageSize = PAGE_SIZE;


    // TODO: realloc der SGListe?


    // get new page(s) for scatter gather entries
    // each page can hold (PAGE_SIZE - sizeof (uint32) - sizeof(int32)) / sizeof (SCATTER_GATHER_ELEMENT) = (4096 - 4 - 4) / 12 = 340 scatter-gather elements
    // TODO: nochmal überlegen, ob das langt: 340 * SPCM4DRV_DMA_MAX_BUFFER_NUMBER * PAGE_SIZE = 340 * 256 * 4096 = 357MB als max DMA Puffer. Evtl kann man mit kmalloc etwas größere Brocken bekommen
    //       das ist jetzt bei Verwendung von get_free_pages() nur noch worst-case, d.h. wenn Einzelseiten angefordert werden (lOrder=0).
    //       Sonst werden 256 mal mehrere Seiten auf einmal angefordert, so dass der mapbare Speicher deutlich größer wird
//    pDmaParams->winSGList[dwCurrentSGListIdx] = (PSCATTER_GATHER_LIST)get_zeroed_page (GFP_KERNEL/* | __GFP_HIGHMEM*/); // don't really care where this is located in RAM, aber mit HIGHMEM kommt NULL zurück 
    do
        {
        lOrder--;
        pDmaParams->winSGList[dwCurrentSGListIdx] = (PSCATTER_GATHER_LIST)__get_free_pages(GFP_KERNEL /*| __GFP_HIGHMEM*/, lOrder); // don't really care where this is located in RAM 
        } while (pDmaParams->winSGList[dwCurrentSGListIdx] == NULL && lOrder > 0);
    if (pDmaParams->winSGList[dwCurrentSGListIdx] == NULL)
        {
        SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: get_free_pages failed *****\n");
        pDmaParams->bufferCount = 0;
        up (&pDmaParams->semAccess);
        return -EFAULT;
        }
    pstCurrentWinSGList = pDmaParams->winSGList[dwCurrentSGListIdx];
    pstCurrentWinSGList->NumberOfElements = 0;
    pstCurrentWinSGList->lOrder = lOrder;

    // ----- a CUDA buffer needs to be handled by special NVidia functions -----
    if (bGPUUsed)
        {
#   ifdef USE_CUDA_RDMA
        uint64 qwRemainingByteCount = qwByteCount;
        size_t qwPinSize = 0; 
        SPCM4DRV_DebugPrint(DBG_TRACE, pstCard->dwBoardNumber, "***** BuildSGList: mapping GPU buffer START *****\n");

        pDmaParams->qwGPUVirtualAddress = ((uint64_t)pvUserBuffer) & GPU_BOUND_MASK;
        qwPinSize = ((uint64_t)pvUserBuffer) + qwByteCount - pDmaParams->qwGPUVirtualAddress;

#       ifdef JETSON_INTEGRATED_GPU
        if (nvidia_p2p_get_pages (pDmaParams->qwGPUVirtualAddress, qwPinSize, &pstCard->pstCudaPageTable, vCudaCallback, pstCard) != 0)
#       else // Intel/AMD or Nvidia Clara with dGPU
        if (nvidia_p2p_get_pages (0, 0, pDmaParams->qwGPUVirtualAddress, qwPinSize, &pstCard->pstCudaPageTable, vCudaCallback, pstCard) != 0)
#       endif
            {
            SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: nvidia_p2p_get_pages failed *****\n");
            pDmaParams->bufferCount = 0;
            up (&pDmaParams->semAccess);
            return -EFAULT;
            }

        // ----- check version of page table -----
        SPCM4DRV_DebugPrint(DBG_TRACE, pstCard->dwBoardNumber, "%s - Page Table version check\n", __FUNCTION__);
        if (!NVIDIA_P2P_PAGE_TABLE_VERSION_COMPATIBLE (pstCard->pstCudaPageTable))
            {
            SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: P2P page table version mismatch *****\n");
#       ifdef JETSON_INTEGRATED_GPU
            nvidia_p2p_put_pages (pstCard->pstCudaPageTable);
#       else // Intel/AMD or Nvidia Clara with dGPU
            nvidia_p2p_put_pages (0, 0, pDmaParams->qwGPUVirtualAddress, pstCard->pstCudaPageTable);
#       endif
            pDmaParams->bufferCount = 0;
            up (&pDmaParams->semAccess);
            return -EFAULT;
            }

#       ifdef JETSON_INTEGRATED_GPU
        SPCM4DRV_DebugPrint(DBG_TRACE, pstCard->dwBoardNumber, "%s - nvidia_p2p_dma_map_pages()\n", __FUNCTION__);
        if (nvidia_p2p_dma_map_pages (&pstCard->pstPCIDevice->dev, pstCard->pstCudaPageTable, &pstCard->pstCudaDmaMap, pDmaParams->writeToDevice? DMA_TO_DEVICE : DMA_FROM_DEVICE) != 0)
            {
            SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: nvidia_p2p_dma_map_pages failed *****\n");

            nvidia_p2p_put_pages (pstCard->pstCudaPageTable);

            pDmaParams->bufferCount = 0;
            up (&pDmaParams->semAccess);
            return -EFAULT;
            }
        SPCM4DRV_DebugPrint(DBG_TRACE, pstCard->dwBoardNumber, "%s - nvidia_p2p_dma_map_pages() -> %u entries\n", __FUNCTION__, pstCard->pstCudaDmaMap->entries);
#       endif



#       ifdef JETSON_INTEGRATED_GPU
        for (dwNVidiaIdx = 0; dwNVidiaIdx < pstCard->pstCudaDmaMap->entries; dwNVidiaIdx++)
            {
            dma_addr_t qwPhysDMAAddress = pstCard->pstCudaDmaMap->hw_address[dwNVidiaIdx];
            uint64 qwLen = pstCard->pstCudaDmaMap->hw_len[dwNVidiaIdx];
#       else
        for (dwNVidiaIdx = 0; dwNVidiaIdx < pstCard->pstCudaPageTable->entries; ++dwNVidiaIdx)
            {
            uint64 qwPhysDMAAddress = pstCard->pstCudaPageTable->pages[dwNVidiaIdx]->physical_address;
            uint64 qwLen            = 0;
            switch (pstCard->pstCudaPageTable->page_size) // page_size is an enum, not a value in bytes!
                {
                case NVIDIA_P2P_PAGE_SIZE_4KB:   qwLen =   4 * 1024; break;
                case NVIDIA_P2P_PAGE_SIZE_64KB:  qwLen =  64 * 1024; break;
                case NVIDIA_P2P_PAGE_SIZE_128KB: qwLen = 128 * 1024; break;
                };
#       endif
            if (qwRemainingByteCount < qwLen)
                qwLen = qwRemainingByteCount;
            qwRemainingByteCount -= qwLen;

            SPCM4DRV_DebugPrint(DBG_TRACE, pstCard->dwBoardNumber, "***** BuildSGList: GPU Page: %llu Len: %llu *****\n", qwPhysDMAAddress, qwLen);

            // ----- if current SG-list is full, we allocate memory for new one -----
            if (dwCurrentSGElementIdx >= ((0x1 << lOrder) * PAGE_SIZE - sizeof (uint32) - sizeof(int32)) / sizeof(SCATTER_GATHER_ELEMENT) - 1)
                {
                dwCurrentSGElementIdx = 0;
                dwCurrentSGListIdx++; 
                if (dwCurrentSGListIdx >= SPCM4DRV_DMA_MAX_BUFFER_NUMBER)
                    {
                    SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: not enough space for SG list *****\n");
#       ifdef JETSON_INTEGRATED_GPU
                    nvidia_p2p_put_pages (pstCard->pstCudaPageTable);
#       else // Intel/AMD or Nvidia Clara with dGPU
                    nvidia_p2p_put_pages (0, 0, pDmaParams->qwGPUVirtualAddress, pstCard->pstCudaPageTable);
#       endif
                    pDmaParams->bufferCount = 0;
                    up (&pDmaParams->semAccess);
                    return -EFAULT;
                    }

                // get new page for scatter gather entries
                // each page can hold (4096 - 4 - 4) / 12 = 341 scatter-gather elements
                //pDmaParams->winSGList[dwCurrentSGListIdx] = (PSCATTER_GATHER_LIST)get_zeroed_page (GFP_KERNEL/* | __GFP_HIGHMEM*/); // don't really care where this is located in RAM 
                // ----- try to get next biggest block for winSGList -----
                lOrder++;
                do
                    {
                    lOrder--;
                    pDmaParams->winSGList[dwCurrentSGListIdx] = (PSCATTER_GATHER_LIST)__get_free_pages(GFP_KERNEL /*| __GFP_HIGHMEM*/, lOrder); // don't really care where this is located in RAM 
                    } while (pDmaParams->winSGList[dwCurrentSGListIdx] == NULL && lOrder > 0);
                if (pDmaParams->winSGList[dwCurrentSGListIdx] == NULL)
                    {
                    SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: get_free_pages failed *****\n");
#       ifdef JETSON_INTEGRATED_GPU
                    nvidia_p2p_put_pages (pstCard->pstCudaPageTable);
#       else // Intel/AMD or Nvidia Clara with dGPU
                    nvidia_p2p_put_pages (0, 0, pDmaParams->qwGPUVirtualAddress, pstCard->pstCudaPageTable);
#       endif
                    pDmaParams->bufferCount = 0;
                    up (&pDmaParams->semAccess);
                    return -EFAULT;
                    }
                pstCurrentWinSGList = pDmaParams->winSGList[dwCurrentSGListIdx];
                pstCurrentWinSGList->NumberOfElements = 0;
                pstCurrentWinSGList->lOrder = lOrder;
                }

            // ----- store address and length of mapped buffer -----
            pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].Length               = qwLen; // will be more than 65536 on Jetson, but the breakdown into notify-size compatible chunks will be done elsewhere
            pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].Address.LowPart      = qwPhysDMAAddress & 0xFFFFFFFF;
            pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].Address.HighPart     = (qwPhysDMAAddress >> 32) & 0xFFFFFFFF;
            pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].pvUserPageOrPageList = NULL; // TODO: brauch ich das noch irgendwofür?
            pstCurrentWinSGList->NumberOfElements++;

            // ----- prepare next loop
            dwCurrentSGElementIdx++;
            }

        pDmaParams->bGPUMemUsed = true;
        SPCM4DRV_DebugPrint(DBG_TRACE, pstCard->dwBoardNumber, "***** BuildSGList: mapping GPU buffer END %d Elemente*****\n", pstCurrentWinSGList->NumberOfElements);
#   else
        SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** BuildSGList: no support for CUDA RDMA in kernel module\n");
        pDmaParams->bufferCount = 0;
        up (&pDmaParams->semAccess);
        return -EFAULT;
#   endif
        }
    else
        {
        uint64 qwContMemPhysDMAAddress = 0;
#   if (LINUX_VERSION_CODE >= KERNEL_VERSION (5,8,0))
        down_read (&current->mm->mmap_lock);
#   else
        down_read (&current->mm->mmap_sem);
#   endif


        pDmaParams->bGPUMemUsed = false;
        pDmaParams->qwGPUVirtualAddress = 0;

        if (pDmaParams->bContMemUsed)
            {
            qwContMemPhysDMAAddress = dma_map_single (&pstCard->pstPCIDevice->dev, pstCard->pvContMemBuffer, qwNotifySize, DMA_BIDIRECTIONAL);
            if (bDMAMappingError (&pstCard->pstPCIDevice->dev, qwContMemPhysDMAAddress))
                {
                SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: mapping continuous buffer failed *****\n");
                goto CLEANUP;
                }
            }


        while (qwByteCount > 0)
            {
            uint64 qwPhysDMAAddress = 0;
            uint64 qwLen            = 0;
            struct page* pstUserPage = NULL;
            bool bMerge = false;
            if (!pDmaParams->bContMemUsed)
                {
                uint64 qwPageOffset = 0;

#   if (LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0))
#       if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,76)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4,4,95)))
            // WORKAROUND: openSUSE 42.3 uses the interface of kernel 4.9.0 but on kernel 4.4.76...
            //             but how to detect that the current kernel has these changes?
                if (1 != get_user_pages ((unsigned long) (pvUserBuffer + qwUserBufOffset), 1, FOLL_WRITE, &pstUserPage, NULL))
#       else
                if (get_user_pages (current, current->mm, (unsigned long) (pvUserBuffer + qwUserBufOffset), lNumPages, 1, 0, &pstUserPage, NULL) != lNumPages)
#       endif
#   else
#       if (LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0))
                if (get_user_pages ((unsigned long) (pvUserBuffer + qwUserBufOffset), lNumPages, 1, 0, &pstUserPage, NULL) != lNumPages)
#       elif ((LINUX_VERSION_CODE == KERNEL_VERSION(6,4,0)) && (SUSE_VERSION == 156))
                // openSUSE 15.6 comes with kernel 6.4.0, but the interface is backported from 6.5.0
                if (get_user_pages ((unsigned long) (pvUserBuffer + qwUserBufOffset), lNumPages, FOLL_WRITE, &pstUserPage) != lNumPages)
#       elif (LINUX_VERSION_CODE < KERNEL_VERSION(6,5,0))
                if (get_user_pages ((unsigned long) (pvUserBuffer + qwUserBufOffset), lNumPages, FOLL_WRITE, &pstUserPage, NULL) != lNumPages)
#       else
                if (get_user_pages ((unsigned long) (pvUserBuffer + qwUserBufOffset), lNumPages, FOLL_WRITE, &pstUserPage) != lNumPages)
#       endif
#   endif
                    {
                    SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: get_user_pages failed *****\n");
                    goto CLEANUP;
                    }

                qwPageOffset = ((unsigned long) (pvUserBuffer + qwUserBufOffset)) & (PAGE_SIZE - 1);
                qwLen        = qwDMAPageSize - (((unsigned long)(pvUserBuffer + qwUserBufOffset)) & (qwDMAPageSize - 1));

#   ifndef _LINUX64 // 32 bit
#       if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))
                qwPhysDMAAddress = dma_map_page (&pstCard->pstPCIDevice->dev, pstUserPage, (uint32)qwPageOffset, (uint32)qwLen, eDMADir);
                if (bDMAMappingError (&pstCard->pstPCIDevice->dev, qwPhysDMAAddress))
                    {
                    SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: page mapping failed *****\n");
                    goto CLEANUP;
                    }
#       else
                qwPhysDMAAddress = dma_map_page (NULL, pstUserPage, (uint32)qwPageOffset, (uint32)qwLen, eDMADir);
#       endif
#   else // 64 bit
#       if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23)) // pci_dac_page_to_dma no longer available with 2.6.23 and following
                qwPhysDMAAddress = dma_map_page (&pstCard->pstPCIDevice->dev, pstUserPage, (uint32)qwPageOffset, (uint32)qwLen, eDMADir);

                if (bDMAMappingError (&pstCard->pstPCIDevice->dev, qwPhysDMAAddress))
                    {
                    SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: page mapping failed *****\n");
                    goto CLEANUP;
                    }
#       else
                qwPhysDMAAddress = pci_dac_page_to_dma (pstCard->pstPCIDevice, pstUserPage, (uint32)qwPageOffset, eDMADir);
#       endif
#   endif
                }
            else // ContMem
                {
                qwPhysDMAAddress = qwContMemPhysDMAAddress + qwUserBufOffset;
                qwLen = qwNotifySize;
                }
            
            // limit length to buffer length
            if (qwLen > qwByteCount)
                qwLen = qwByteCount;

            // ----- if current SG-list is full, we allocate memory for new one -----
            if (dwCurrentSGElementIdx >= ((0x1 << lOrder) * PAGE_SIZE - sizeof (uint32) - sizeof(int32)) / sizeof(SCATTER_GATHER_ELEMENT) - 1)
                {
                dwCurrentSGElementIdx = 0;
                dwCurrentSGListIdx++; 
                if (dwCurrentSGListIdx >= SPCM4DRV_DMA_MAX_BUFFER_NUMBER)
                    {
                    SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: not enough space for SG list *****\n");
                    goto CLEANUP;
                    }

                // get new page for scatter gather entries
                // each page can hold (4096 - 4 - 4) / 12 = 341 scatter-gather elements
                //pDmaParams->winSGList[dwCurrentSGListIdx] = (PSCATTER_GATHER_LIST)get_zeroed_page (GFP_KERNEL/* | __GFP_HIGHMEM*/); // don't really care where this is located in RAM 
                // ----- try to get next biggest block for winSGList -----
                lOrder++;
                do
                    {
                    lOrder--;
                    pDmaParams->winSGList[dwCurrentSGListIdx] = (PSCATTER_GATHER_LIST)__get_free_pages(GFP_KERNEL /*| __GFP_HIGHMEM*/, lOrder); // don't really care where this is located in RAM 
                    } while (pDmaParams->winSGList[dwCurrentSGListIdx] == NULL && lOrder > 0);
                if (pDmaParams->winSGList[dwCurrentSGListIdx] == NULL)
                    {
                    SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: get_free_pages failed *****\n");
                    goto CLEANUP;
                    }
                pstCurrentWinSGList = pDmaParams->winSGList[dwCurrentSGListIdx];
                pstCurrentWinSGList->NumberOfElements = 0;
                pstCurrentWinSGList->lOrder = lOrder;
                }

            // ----- store address and length of mapped buffer -----
            // first check if we can merge entries
            bMerge = false;
            if (dwCurrentSGElementIdx > 0)
                {
                uint64 qwPrevPhysDMAAddress = QWORD_FROM_DWORD(pstCurrentWinSGList->Elements[dwCurrentSGElementIdx - 1].Address.HighPart, pstCurrentWinSGList->Elements[dwCurrentSGElementIdx - 1].Address.LowPart);
                uint64 qwPrevLen = pstCurrentWinSGList->Elements[dwCurrentSGElementIdx - 1].Length;
                // merge if the two pages are adjacent to each other
                if (qwPrevPhysDMAAddress + qwPrevLen == qwPhysDMAAddress)
                    {
                    // ... but only if the combined length does not exceed our length entry
                    if (qwPrevLen + qwLen < 0xFFFFFFFF)
                        bMerge = true;
                    }
                }
            if (bMerge)
                {
                struct _USER_PAGE_LIST* pstLast = (struct _USER_PAGE_LIST*)pstCurrentWinSGList->Elements[dwCurrentSGElementIdx - 1].pvUserPageOrPageList;

                SPCM4DRV_DebugPrint(DBG_TRACE, pstCard->dwBoardNumber, "***** BuildSGList: Merging. Addr: 0x%016llx  Len: %llu\n", qwPhysDMAAddress, qwLen);
                pstCurrentWinSGList->Elements[dwCurrentSGElementIdx - 1].Length += qwLen;

                // we store the merged pages in a linked list
                while (pstLast->pstNext)
                    pstLast = pstLast->pstNext;
                pstLast->pstNext = kmalloc (sizeof (struct _USER_PAGE_LIST), GFP_KERNEL);
                pstLast = pstLast->pstNext;
                pstLast->pstUserPage      = pstUserPage;
                pstLast->Length           = qwLen;
                pstLast->Address.LowPart  = LOW_DWORD (qwPhysDMAAddress);
                pstLast->Address.HighPart = HIGH_DWORD(qwPhysDMAAddress);

                pstLast->pstNext = NULL;
                }
            else
                {
                struct _USER_PAGE_LIST* pstFirst = NULL;
                SPCM4DRV_DebugPrint(DBG_TRACE, pstCard->dwBoardNumber, "***** BuildSGList: new Entry %u. Addr: 0x%016llx  Len: %llu\n", dwCurrentSGElementIdx, qwPhysDMAAddress, qwLen);
                pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].Length           = qwLen;
                pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].Address.LowPart  = LOW_DWORD (qwPhysDMAAddress);
                pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].Address.HighPart = HIGH_DWORD(qwPhysDMAAddress);
                pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].pvUserPageOrPageList = kmalloc (sizeof (struct _USER_PAGE_LIST), GFP_KERNEL);
                if (pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].pvUserPageOrPageList == NULL)
                    {
                    SPCM4DRV_DebugPrint(DBG_ERROR, pstCard->dwBoardNumber, "***** ERROR in BuildSGList: kmalloc _USER_PAGE_LIST failed *****\n");
                    goto CLEANUP;
                    }
    
                pstFirst = (struct _USER_PAGE_LIST*)pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].pvUserPageOrPageList;
                pstFirst->pstUserPage      = pstUserPage;
                pstFirst->Length           = qwLen;
                pstFirst->Address.LowPart  = LOW_DWORD (qwPhysDMAAddress);
                pstFirst->Address.HighPart = HIGH_DWORD(qwPhysDMAAddress);
                pstFirst->pstNext = NULL;
                pstCurrentWinSGList->NumberOfElements++;

                // ----- prepare next loop
                dwCurrentSGElementIdx++;
                }
            qwUserBufOffset += qwLen;
            qwByteCount -= qwLen;
            
            } // while (qwByteCount)
#   if (LINUX_VERSION_CODE >= KERNEL_VERSION (5,8,0))
        up_read (&current->mm->mmap_lock);
#   else
        up_read (&current->mm->mmap_sem);
#   endif
        } // !nVidia
    pDmaParams->bufferCount = dwCurrentSGListIdx + 1;

    up (&pDmaParams->semAccess);

    return 0;

CLEANUP:
    // unlock because ClearData immediately locks it again and would hang
    pDmaParams->bufferCount = dwCurrentSGListIdx + 1;
    DMA_UNLOCK(pDmaParams);

    // unmap already mapped pages
    SPCM4DRV_XDMA_ClearData (pstCard, pXDMADmaParams);

#   if (LINUX_VERSION_CODE >= KERNEL_VERSION (5,8,0))
    up_read (&current->mm->mmap_lock);
#   else
    up_read (&current->mm->mmap_sem);
#   endif
    up (&pDmaParams->semAccess);

    return -EFAULT;
#endif // WINVER
}

//------ SPCM4DRV_XDMA_SaveSGList ------
#ifdef WINVER
VOID SPCM4DRV_XDMA_SaveSGList (PDEVICE_OBJECT pDevObj, PIRP pIrpIfSystemQueing, PSCATTER_GATHER_LIST pWinSGList, PVOID pvParam)
#else
void SPCM4DRV_XDMA_SaveSGList (PSCATTER_GATHER_LIST pWinSGList, void* pvParam)
#endif
{
    PXDMA_DMA_PARAMS pDmaParams = (PXDMA_DMA_PARAMS) pvParam;

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_DMA, "XDMA_SaveSGList - Win-SGList has %d elements", pWinSGList->NumberOfElements );
    SPCM4DRV_DebugPrint (TRACE, 1, 
                         "SPCM4DRV_XDMA_SaveSGList - Win-SGList has %d elements\n",
                         pWinSGList->NumberOfElements);

    // save sglist because later we have to free it
    pDmaParams->stCommon.winSGList[pDmaParams->stCommon.bufferCount-1] = pWinSGList;
}

//------ SPCM4DRV_StartTransfer ------
//
// In dieser Funktionen bauen wir aus den gespeicherten SG-Listen eine Liste
// von Liste von DMA-Aufträgen in dem Format, den der PLX benötigt. Die Liste wird
// dem PLX übergeben und dann der DMA-Transfer gestartet.
//
#ifdef WINVER
INT8 SPCM4DRV_XDMA_StartTransfer (IN PSPCM4DRV_DEVICE_CONTEXT pContext, PXDMA_DMA_PARAMS pXDMADmaParams,
                             ULONG64 qwByteOffset, ULONG64 qwTransferSize, ULONG dwNotifySize)
#else
int8 SPCM4DRV_XDMA_StartTransfer (SPCM_ST_CARDINFO* pContext, PXDMA_DMA_PARAMS pXDMADmaParams, uint64 qwByteOffset, uint64 qwTransferSize, uint32 dwNotifySize)
#endif
{
    COMMON_DMA_PARAMS* pDmaParams = &pXDMADmaParams->stCommon;
    ULONG bufferNo, i, dwSGEntryNum, dmaDescrPtrLow, dmaDescrPtrHigh;
    XDMA_SGLIST_ENTRY *pSGListCurrentEntry, *pSGListLastEntry, *pSGListLastPhysEntry;
    ULONG64 qwInitalAvail = qwTransferSize;
    ULONG dwBytesSinceLastInterrupt;
    BOOLEAN isTransferUsingWholeBuffer = TRUE;
    ULONG64 qwPhysicalAddress = 0;
#ifdef WINVER
    PCHAR pBar1Mem = (PCHAR)pContext->memMappedAddress[1];
#else
    uint32* pBar1Mem = pContext->apdwMemMappedAddress[1];
    unsigned long dwLocalIRQLockFlags;
#endif

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_DMA, "XDMA_StartTransfer" );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "XDMA_StartTransfer\n");

    // set pointer to BAR0 memory region

    // get the pointer of the sglist memory allocated in InitDMA
    pSGListCurrentEntry  = pXDMADmaParams->pXDMASGListStartEntry;
    pSGListLastEntry     = pSGListCurrentEntry;
    pSGListLastPhysEntry = pSGListCurrentEntry;

    // set number of sglist entry
    dwSGEntryNum = 1;
    dwBytesSinceLastInterrupt = 0;
    pDmaParams->dmaSGListElements = 0;


    // build a sglist in the format needed by XDMA
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "   XDMA_StartTransfer: bufferCount: %u\n", pDmaParams->bufferCount);
    for (bufferNo = 0; bufferNo < pDmaParams->bufferCount; bufferNo++)
        {
        ULONG elements;

        // Wir holen uns die erste/nächste SG-Liste (von Windows)
        PSCATTER_GATHER_LIST pWinSGList = pDmaParams->winSGList[bufferNo];

        // Ist das Anlegen der Windows-SG Liste fehlgeschlagen?
        if (pWinSGList == NULL)
            {
            SPCM4DRV_DebugPrint (ERROR, pContext->dwBoardNumber, "   StartTransfer: WinSGList is NULL\n");
            return 0;
            }

        // Diese Liste besteht aus 'elements' Blöcken.
        elements = pWinSGList->NumberOfElements;
        SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "   StartTransfer: SG Elemente: %u NotifySize: %u\n", elements, dwNotifySize);

        // Jedes Element der WinSG-Liste bauen wir in die NWC-Liste ein.
        for (i=0; i < elements; ++i)
            {
            // bereits aufgeteilte Länge, wenn Blöck größer dwNotifySize
            ULONG lAlreadySplittedLen = 0;

            // Wir nehmen uns die Länge des nächsten Elements der Windows-SG-Liste
            ULONG currentElementLength = pWinSGList->Elements[i].Length;
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "   StartTransfer: %u currentElementLength: %u\n", i, currentElementLength);

            while (currentElementLength != 0)
                {
                // Die Länge des neuen Eintrages für die NWC-SG-Liste 
                ULONG currentEntryLength;

                // Wenn wir ohne 'NotifySize' arbeiten oder 
                // wenn die 'NotifySize' größer ist als max. NWC-Transfer-Länge und
                // wenn die Länge des neuen Listen-Elements größer ist als die max. NWC-Transfer-Länge (1 MB - 4 KB),
                // dann begrenzen wir die Länge des neuen Listen-Eintrags auf die max. NWC-Transfer-Länge.
                if (((dwNotifySize == 0) ||
                     (dwNotifySize > SPCM4DRV_DMA_MAX_XDMA_TRANSFER_LENGTH)) && 
                    (currentElementLength > SPCM4DRV_DMA_MAX_XDMA_TRANSFER_LENGTH))
                    currentEntryLength = SPCM4DRV_DMA_MAX_XDMA_TRANSFER_LENGTH;
                else
                    currentEntryLength = currentElementLength;
                SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "   StartTransfer: %u currentEntryLength: %u\n", i, currentEntryLength);

                // Die Länge des neuen NWC-Eintrages wird 
                // von der Länge des aktuellen Elements der Windows-SG-Liste abgezogen.
                currentElementLength -= currentEntryLength;
                SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "   StartTransfer: %u remaining currentElementLength: %u\n", i, currentElementLength);

                // Abhängig von der NotifySize bauen wir aus diesem Eintrag ein oder mehrere Pakete
                while (currentEntryLength != 0)
                    {
#ifdef WINVER
                    ULONG64 qwPhysAddrOfNextDescriptor = 0;
#else
                    dma_addr_t qwPhysAddrOfNextDescriptor = 0;
#endif
                    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "   StartTransfer: %u SGEntry: %u\n", i, dwSGEntryNum);
                    // Überprüfen, ob die Anzahl MapRegister passt
#ifdef WINVER
                    if (dwSGEntryNum >= pDmaParams->dmaMaxMapRegisters)
                        {
                        SPCM4DRV_DebugPrint (ERROR, pContext->dwBoardNumber, "   StartTransfer: not enough dmaMaxMapRegisters\n");
                        return 0;
                        }
#else
                    // TODO: gibt es hier was ähnliches?
#endif

                    // Berechne die physische Adresse des nächsten Sg-Listen-Eintrags.
#ifdef WINVER
                    qwPhysAddrOfNextDescriptor = qwGetPhysAddr (pXDMADmaParams, dwSGEntryNum);
#else
                    qwPhysAddrOfNextDescriptor = qwGetPhysAddr (pXDMADmaParams, dwSGEntryNum);
#endif
                    pSGListCurrentEntry->dwNextDescAddrLow = LOW_DWORD(qwPhysAddrOfNextDescriptor);
                    pSGListCurrentEntry->dwNextDescAddrHigh = HIGH_DWORD(qwPhysAddrOfNextDescriptor);
                    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "   StartTransfer: %u NextDescAddr: 0x%llx\n", i, qwPhysAddrOfNextDescriptor);

                    // control initialisieren
                    pSGListCurrentEntry->dwControl =      SGLIST_ENTRY_CONTROL_MAGIC; // marks descriptor as valid
                    pSGListCurrentEntry->dwSrcAddrLow =   0;
                    pSGListCurrentEntry->dwSrcAddrHigh =  0;
                    pSGListCurrentEntry->dwDestAddrLow =  0;
                    pSGListCurrentEntry->dwDestAddrHigh = 0;


                    // Adresse als 64bit inkrementieren, damit der Übertrag von von Low nach High korrekt ist
                    qwPhysicalAddress = (((ULONG64)pWinSGList->Elements[i].Address.HighPart) << 32) | pWinSGList->Elements[i].Address.LowPart;
                    qwPhysicalAddress += lAlreadySplittedLen;

                    // ---------------------------------------------------------------------------------
                    // NotifySize=0 -> nur ein Interrupt ganz am Ende, wir nehmen die Blöcke wie sie kommen
                    if (dwNotifySize == 0)
                        {
                        // unterschiedliche Stellen für die Pufferadresse, abhängig von DMA-Richtung
                        if (pDmaParams->writeToDevice)
                            {
                            pSGListCurrentEntry->dwSrcAddrLow  =  LOW_DWORD (qwPhysicalAddress);
                            pSGListCurrentEntry->dwSrcAddrHigh = HIGH_DWORD (qwPhysicalAddress);
                            }
                        else
                            {
                            pSGListCurrentEntry->dwDestAddrLow  =  LOW_DWORD (qwPhysicalAddress);
                            pSGListCurrentEntry->dwDestAddrHigh = HIGH_DWORD (qwPhysicalAddress);
                            // TODO: WriteBack?
                            }
                        pSGListCurrentEntry->dwLength       = currentEntryLength;
                        // Wir zählen mit, wieviel von diesem Element bereits verarbeitet wurde
                        lAlreadySplittedLen += pSGListCurrentEntry->dwLength;

                        currentEntryLength = 0;
                        }


                    // ---------------------------------------------------------------------------------
                    // Eine ganze NotifySize bzw. der verbleibende Rest passt in das Element 
                    // (entweder Continuous Mem oder Notify < 4k)
                    else if (currentEntryLength >= (dwNotifySize - dwBytesSinceLastInterrupt))
                        {
                        /*
                        if( lAlreadySplittedLen == 0 )
                            // SW: Descr. umfasst Paketanfang - SOP.
                            pSGListCurrentEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_SOP;
                        */

                        // unterschiedliche Stellen für die Pufferadresse, abhängig von DMA-Richtung
                        if (pDmaParams->writeToDevice)
                            {
                            pSGListCurrentEntry->dwSrcAddrLow  =  LOW_DWORD (qwPhysicalAddress);
                            pSGListCurrentEntry->dwSrcAddrHigh = HIGH_DWORD (qwPhysicalAddress);
                            }
                        else
                            {
                            pSGListCurrentEntry->dwDestAddrLow  =  LOW_DWORD (qwPhysicalAddress);
                            pSGListCurrentEntry->dwDestAddrHigh = HIGH_DWORD (qwPhysicalAddress);
                            // TODO: WriteBack?
                            }
                        pSGListCurrentEntry->dwLength       = (dwNotifySize - dwBytesSinceLastInterrupt);

                        // Wir zählen mit, wieviel von diesem Element bereits verarbeitet wurde
                        lAlreadySplittedLen += pSGListCurrentEntry->dwLength;
                        currentEntryLength  -= pSGListCurrentEntry->dwLength;

                        // Eine ganze NotifySize passt hier rein, daher kommt am Ende immer der Interrupt
                        pSGListCurrentEntry->dwControl |= SGLIST_ENTRY_CONTROL_IRQONCOMPL;
                        dwBytesSinceLastInterrupt = 0;
                        }


                    // ---------------------------------------------------------------------------------
                    // Eine NotifySize geht über mehrere Elemente
                    else 
                        {
                        // unterschiedliche Stellen für die Pufferadresse, abhängig von DMA-Richtung
                        if (pDmaParams->writeToDevice)
                            {
                            pSGListCurrentEntry->dwSrcAddrLow  =  LOW_DWORD (qwPhysicalAddress);
                            pSGListCurrentEntry->dwSrcAddrHigh = HIGH_DWORD (qwPhysicalAddress);
                            }
                        else
                            {
                            pSGListCurrentEntry->dwDestAddrLow  =  LOW_DWORD (qwPhysicalAddress);
                            pSGListCurrentEntry->dwDestAddrHigh = HIGH_DWORD (qwPhysicalAddress);
                            // TODO: WriteBack?
                            }
                        pSGListCurrentEntry->dwLength       = currentEntryLength;
                        currentEntryLength = 0;

                        // Wir zählen mit, wieviel von diesem Element bereits verarbeitet wurde
                        lAlreadySplittedLen += pSGListCurrentEntry->dwLength;

                        // einen Interrupt alle NotifySize einbauen
                        dwBytesSinceLastInterrupt += pSGListCurrentEntry->dwLength;
                        if (dwBytesSinceLastInterrupt >= dwNotifySize)
                            {
                            pSGListCurrentEntry->dwControl |= SGLIST_ENTRY_CONTROL_IRQONCOMPL;
                            dwBytesSinceLastInterrupt = 0;
                            }
                        }

                    // ----- für C2S DMAs nutzen wir das Metadata Writeback Feature, um u.a. die korrekte Länge bei Transfers < NotifySize (z.B. TS) zu bekommen
                    if (!pDmaParams->writeToDevice)
                        {
#ifdef WINVER
                        ULONG64 qwPhysicalAddressWriteBack;
#else                        
                        uint64 qwPhysicalAddressWriteBack;
#endif
                        XDMA_C2H_WRITEBACK* pWriteBack = pstGetXDMAWriteBackEntry (pXDMADmaParams, dwSGEntryNum - 1);
                        pWriteBack->dwStatus = C2H_WRITEBACK_MAGIC;
                        pWriteBack->dwLength = 0;

                        qwPhysicalAddressWriteBack = qwGetWriteBackPhysAddr (pXDMADmaParams, dwSGEntryNum - 1);
                        pSGListCurrentEntry->dwSrcAddrLow  =  LOW_DWORD (qwPhysicalAddressWriteBack);
                        pSGListCurrentEntry->dwSrcAddrHigh = HIGH_DWORD (qwPhysicalAddressWriteBack);
                        }
                    dwSGEntryNum++;

                    // Den aktuellen Eintrag merken wir uns als möglichen letzten Eintrag,
                    // bevor wir den Zeiger auf den nächsten Eintrag stellen.
                    // OR: Hierbei beachten wir den verfügbaren Buffer
                    if (qwTransferSize >= pSGListCurrentEntry->dwLength)
                        {
                        qwTransferSize -= pSGListCurrentEntry->dwLength;
                        pSGListLastEntry = pSGListCurrentEntry;

                        if (qwTransferSize == 0)
                            pSGListCurrentEntry->dwControl |= SGLIST_ENTRY_CONTROL_STOP_FETCH_DESC; // damit bei DA der Transfer hier aufhört
                        }
                    //
                    // SW: ??? Diese Stelle hier muss unbedingt nochmal überprüft werden !!!
                    //
                    else if ( qwTransferSize > 0 )
                        {
                        //SW: Der letzte Puffer, für den auch Daten übergeben wurden.

                        pSGListCurrentEntry->dwLength = (ULONG)qwTransferSize;

                        qwTransferSize = 0;
                        pSGListLastEntry = pSGListCurrentEntry;

                        if (qwTransferSize == 0)
                            pSGListCurrentEntry->dwControl |= SGLIST_ENTRY_CONTROL_STOP_FETCH_DESC; // damit bei DA der Transfer hier aufhört
                        }

                    // if the page isn't activated the page needs to look like being transferred
                    else
                        {
                        // UE: das Nullen ist ein Ueberbleibsel vom NWC...
                        //pSGListCurrentEntry->dwLength = 0; // TODO: korrekt?
                        // UE: stattdessen koennte man die Magic Number loeschen, das kann auch zum Stopp fuehren
                        //     wenn das entsprechende stop-on flag gesetzt ist
                        //pSGListCurrentEntry->dwControl &= ~SGLIST_ENTRY_CONTROL_MAGIC

                        // we have unused descriptor/s in the chain ... 
                        if( isTransferUsingWholeBuffer )
                            isTransferUsingWholeBuffer = FALSE;
                        }

#ifdef DMA_BOUNCE_BUFFER
                    // force sync of user buffer to bounce buffer
                    if (pDmaParams->writeToDevice)
                        {
                        dma_sync_single_for_device (&pContext->pstPCIDevice->dev, QWORD_FROM_DWORD(pSGListCurrentEntry->dwSrcAddrHigh, pSGListCurrentEntry->dwSrcAddrLow), pSGListCurrentEntry->dwLength, DMA_TO_DEVICE);
                        }
#endif
                    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "%s - %p Idx: %u\n", __FUNCTION__, pSGListLastEntry, dwGetIndexOfXDMASGListEntry(pXDMADmaParams, pSGListLastEntry));
#ifdef WINVER
                    pSGListLastPhysEntry = pSGListCurrentEntry++;
#else
                    pSGListLastPhysEntry = pSGListCurrentEntry;
                    pSGListCurrentEntry = pstGetXDMASGListEntry (pXDMADmaParams, dwSGEntryNum - 1);
#endif
                    }
                }
            }
        }

    // Wir merken und die Zahl der tatsächlich entstandenen Blöcke.
    // (j-1,da am Ende der Listen-Funktion j nochmal incrementiert wurde!)
    pDmaParams->dmaSGListElements = --dwSGEntryNum;
#ifdef WINVER
    pXDMADmaParams->pXDMASGListLastEntry  = ((XDMA_SGLIST_ENTRY*)pDmaParams->dmaSGListVirtualAddress) + (pDmaParams->dmaSGListElements - 1);
#else
    pXDMADmaParams->pXDMASGListLastEntry  = pstGetXDMASGListEntry (pXDMADmaParams, pDmaParams->dmaSGListElements - 1);
#endif

    // Beim Starten haben wir noch keine wieder verfügbar gemachten Einträge.
    pDmaParams->dmaSGListRefreshedElements = 0;

    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "Our SGList has %d elements", pDmaParams->dmaSGListElements );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "Our SGList has %d elements\n", pDmaParams->dmaSGListElements);

    // build descriptor pointer
    dmaDescrPtrLow  = pDmaParams->dmaSGListPhysicalAddress.LowPart;
    dmaDescrPtrHigh = pDmaParams->dmaSGListPhysicalAddress.HighPart;

    // SW: Der letzte PHYSISCHE Eintrag der ganzen Liste braucht keinen INTATEND-Eintrag,
    // sondern eher der letzte BENUTZTE Eintrag, oder ???

    // OR: der letzte Eintrag hat immer ein INTATEND Bit
    pSGListLastEntry->dwControl |= SGLIST_ENTRY_CONTROL_IRQONCOMPL;

    // !!! SW 130626:
    // Wir merken und diese 'festen' IRQ-Flags auch im length2-Feld.
    // Damit können wir sie von den zeitweilig beim EOC-Modus dazukommenden IRQ-Flags unterscheiden.

    // TODO: was im WriteBack?

    if( isTransferUsingWholeBuffer )
        {
        TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "Modus: END_OF_CHAIN" );
        SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "Modus: END_OF_CHAIN\n" );

        // Wenn die ganze SG-Liste bei diesem Transfer genutzt wird, 
        // hängen wir als Ende-Kennung eine '0' ans Ende der Liste und
        // nuzten dann nicht das NWD_SW_DESC_PTR register zum Starten des DMA !!!

        //pSGListLastPhysEntry->dwNextDescAddrLow =  0;
        //pSGListLastPhysEntry->dwNextDescAddrHigh = 0;
        pSGListLastPhysEntry->dwControl |= SGLIST_ENTRY_CONTROL_STOP_FETCH_DESC; // damit die SG-Liste nicht herumklappt
        pSGListLastPhysEntry->dwNextDescAddrLow =  dmaDescrPtrLow;
        pSGListLastPhysEntry->dwNextDescAddrHigh = dmaDescrPtrHigh;
        }
    else
        {
        // Wenn bei Start des Transfers nur ein Teil der SG-Liste genutzt wird, 
        // schließen wir die Kette der SGListen-Descriptoren, indem der letzte physische Eintrag 
        // auf den Listen/Anfang zeigt und 
        // nuzten dann das NWD_SW_DESC_PTR register zum Starten des DMA !!!

        TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "Modus: POINTER_IN_REGS" );
        SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "Modus: POINTER_IN_REGS\n" );

        pSGListLastPhysEntry->dwNextDescAddrLow =  dmaDescrPtrLow;
        pSGListLastPhysEntry->dwNextDescAddrHigh = dmaDescrPtrHigh;
        }


    //Für C2S nicht nutzbar, solange Descriptor-Felder für PLX-NAchbildung verwendet werden.
    //  pSGListLastPhysEntry->dwControl |=       NWD_S2CDESC_CNTRL_FLAG_EOP;

    // save pointer to current first and last entries, here we begin later to refresh the sgList
    pXDMADmaParams->pXDMASGListCurrentFirstTestEntry = pXDMADmaParams->pXDMASGListStartEntry;
    pXDMADmaParams->pXDMASGListCurrentSGListStart    = pXDMADmaParams->pXDMASGListStartEntry;
    pXDMADmaParams->pXDMASGListCurrentLastEntry =      pSGListLastEntry;
    pXDMADmaParams->dwNumProcessedCompletedDesc =      0;

    // the current first entry is the one behind the last entry
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "SGListLast: %p Dma->Last: %p IdxOf(LGListLast) %u\n", pSGListLastEntry, pXDMADmaParams->pXDMASGListLastEntry, dwGetIndexOfXDMASGListEntry (pXDMADmaParams, pSGListLastEntry));
    if (pSGListLastEntry == pXDMADmaParams->pXDMASGListLastEntry)
        pXDMADmaParams->pXDMASGListCurrentFirstEntry = pXDMADmaParams->pXDMASGListStartEntry;
    else
#   ifdef WINVER
        pXDMADmaParams->pXDMASGListCurrentFirstEntry = pSGListLastEntry + 1;
#   else
        pXDMADmaParams->pXDMASGListCurrentFirstEntry = pstGetXDMASGListEntry (pXDMADmaParams, dwGetIndexOfXDMASGListEntry (pXDMADmaParams, pSGListLastEntry) + 1);
#   endif

    pXDMADmaParams->pXDMASGListCurrentFirstRefreshedEntry = NULL;

    // start dma operation
    // OR: in case of write the transferSize is the initial number of bytes that has been put into the buffer, 
    // the rest is "transferred"
    if (pDmaParams->writeToDevice)
        {
        pDmaParams->qwBytesTransfered =   pDmaParams->qwBufferSize - qwInitalAvail;
        pDmaParams->qwBytesAlreadyFree =  qwTransferSize;       // SW 130726
        }
    else
        {
        pDmaParams->qwBytesTransfered =   0;
        pDmaParams->qwBytesAlreadyFree =  0;                  // SW 130726
        }

    //_printSGList = TRUE;
    XDMASGListDump( pContext->dwBoardNumber, pXDMADmaParams );
    //DbgBreakPoint();
    //_printSGList = FALSE;

    pDmaParams->active = TRUE;

    // set hw desc ptr (fill list into XDMA)
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "firstDescAddr:%8x:%8x\n", dmaDescrPtrHigh, dmaDescrPtrLow);
    XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngAddrOffs + XDMA_REG_CHANNEL_SGDMA_LOW_ADDR,  dmaDescrPtrLow);
    XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngAddrOffs + XDMA_REG_CHANNEL_SGDMA_HIGH_ADDR, dmaDescrPtrHigh);

    // currently the "adjacent descriptor" mode is not used
    XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngAddrOffs + XDMA_REG_CHANNEL_SGDMA_ADJACENT, 0);

    XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_IRQ_ENABLE_MASK_SET, CHANNEL_IRQ_ENABLE_IM_DESC_COMPL | CHANNEL_IRQ_ENABLE_IM_DESC_STOPPED);

    INTERRUPT_LOCK (pXDMADmaParams);
    if (pDmaParams->writeToDevice)
        XDMA_WriteByOffset (pBar1Mem, XDMA_TARGET_IRQBLOCK + XDMA_REG_IRQBLOCK_CH_IRQ_EN_MASK_SET, XDMA_IRQREQ_H2C_INT_ENG0); // nur ein PC->Karte DMA Kanal
    else
        {
        ULONG dwChannel = (pDmaParams->dwEngAddrOffs - XDMA_TARGET_C2H_SGDMA) >> 8;
        XDMA_WriteByOffset (pBar1Mem, XDMA_TARGET_IRQBLOCK + XDMA_REG_IRQBLOCK_CH_IRQ_EN_MASK_SET, XDMA_IRQREQ_C2H_INT_ENG0 << dwChannel);
        }

    INTERRUPT_UNLOCK (pXDMADmaParams);

    // enable interrupts at IRQONCOMPL
    XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_CONTROL_SET, CHANNEL_CONTROL_IE_DESC_COMPL | CHANNEL_CONTROL_IE_DESC_STOPPED);
    // start DMA
    XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_CONTROL_SET, CHANNEL_CONTROL_RUN);

    return 1;
    }


//------ SPCM4DRV_StopTransfer ------
#ifdef WINVER
VOID SPCM4DRV_XDMA_StopTransfer (IN PSPCM4DRV_DEVICE_CONTEXT pContext, PXDMA_DMA_PARAMS pXDMADmaParams)
#else
void SPCM4DRV_XDMA_StopTransfer (SPCM_ST_CARDINFO* pContext, XDMA_DMA_PARAMS* pXDMADmaParams)
#endif
{
    COMMON_DMA_PARAMS* pDmaParams = &pXDMADmaParams->stCommon;
#ifdef WINVER
    PUCHAR pBar1Mem = pContext->memMappedAddress[1];
#else
    uint32* pBar1Mem = pContext->apdwMemMappedAddress[1];
#endif
    XDMA_SGLIST_ENTRY* pSGListCurrentEntry;
     
    ULONG i;

    // OR: Sicherung eingebaut
    DMA_LOCK(pDmaParams);

    //XDMASGListDump( pContext->dwBoardNumber, pDmaParams );

    // clear sglist entries
    pSGListCurrentEntry = pXDMADmaParams->pXDMASGListStartEntry;
    for( i = 0; i < pDmaParams->dmaSGListElements; ++i )
        {
        memset (pSGListCurrentEntry, 0, sizeof (XDMA_SGLIST_ENTRY));
#   ifdef WINVER
        pSGListCurrentEntry++;
#   else
        pSGListCurrentEntry = pstGetXDMASGListEntry (pXDMADmaParams, i + 1);
#   endif
        }

    // Write to channel ¿Control register¿ to stop DMA run
    XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_CONTROL_CLR, CHANNEL_CONTROL_RUN);

    // TODO: warten, denn die Karte arbeitet jetzt noch den letzten Descriptor ab
    
    pDmaParams->active = FALSE;

    // OR: Sicherung eingebaut
    DMA_UNLOCK(pDmaParams);
    }

//------ SPCM4DRV_ClearData ------
#ifdef WINVER
VOID SPCM4DRV_XDMA_ClearData (IN PSPCM4DRV_DEVICE_CONTEXT pContext, PXDMA_DMA_PARAMS pXDMADmaParams)
#else
void SPCM4DRV_XDMA_ClearData (SPCM_ST_CARDINFO* pContext, XDMA_DMA_PARAMS* pXDMADmaParams)
#endif
    {
    COMMON_DMA_PARAMS* pDmaParams = &pXDMADmaParams->stCommon;
    ULONG i;
    XDMA_SGLIST_ENTRY* pSGListCurrentEntry;
#ifndef WINVER
    enum dma_data_direction eDMADir = (pDmaParams->writeToDevice? DMA_TO_DEVICE : DMA_FROM_DEVICE);
#endif

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_DMA, "ClearData - buffer: %d", pDmaParams->bufferCount );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "SPCM4DRV_ClearData: buffer:%d\n", pDmaParams->bufferCount );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "-------------\n" );

    DMA_LOCK(pDmaParams);

#ifndef WINVER
    if (pDmaParams->bGPUMemUsed)
        {
#   ifdef USE_CUDA_RDMA
        if (pDmaParams->bufferCount != 0)
            {
            SPCM4DRV_DebugPrint(DBG_TRACE, pContext->dwBoardNumber, "***** SPCM4DRV_ClearData: freeing GPU memory at %llu\n", pDmaParams->qwGPUVirtualAddress);
#       ifdef JETSON_INTEGRATED_GPU
            nvidia_p2p_put_pages (pContext->pstCudaPageTable);
#       else // Intel/AMD or Nvidia Clara with dGPU
            nvidia_p2p_put_pages (0, 0, pDmaParams->qwGPUVirtualAddress, pContext->pstCudaPageTable);
#       endif

            pContext->pstCudaPageTable = NULL;

            for (i = 0; i < pDmaParams->bufferCount; i++)
                {
                free_pages ((unsigned long)pDmaParams->winSGList[i], pDmaParams->winSGList[i]->lOrder);
                pDmaParams->winSGList[i] = NULL;
                }
            }
#   else
        SPCM4DRV_DebugPrint(DBG_ERROR, pContext->dwBoardNumber, "***** SPCM4DRV_ClearData: no support for CUDA RDMA in kernel module\n");
#   endif
        }
    else
#endif // !WINVER
        {
        for (i = 0; i < pDmaParams->bufferCount; i++)
            {
            PSCATTER_GATHER_LIST pWinSGList = pDmaParams->winSGList[i];
#ifdef WINVER
            PMDL pMdl = pDmaParams->mdlList[i];

            MmUnlockPages (pMdl);
            
            IoFreeMdl (pMdl);

            pDmaParams->mdlList[i] = NULL;
            if (pWinSGList != NULL)
                pDmaParams->pDmaAdapter->DmaOperations->PutScatterGatherList (pDmaParams->pDmaAdapter,
                                                                              pWinSGList,
                                                                              pDmaParams->writeToDevice);

#else
            if (!pDmaParams->bContMemUsed)
                {
                // ----- unmap dma pages -----
                uint32 dwElementIdx = 0;
                for (; dwElementIdx < pWinSGList->NumberOfElements; ++dwElementIdx)
                    {
                    USER_PAGE_LIST* pstUserPageListEntry = (USER_PAGE_LIST*)pWinSGList->Elements[dwElementIdx].pvUserPageOrPageList;
                    while (pstUserPageListEntry != NULL)
                        {
                        USER_PAGE_LIST* pstNextUserPageListEntry = pstUserPageListEntry->pstNext;

                        uint64 qwPhysDMAAddress = QWORD_FROM_DWORD (pstUserPageListEntry->Address.HighPart, pstUserPageListEntry->Address.LowPart);
                        struct page* pstUserPage = pstUserPageListEntry->pstUserPage;
#   if (LINUX_VERSION_CODE > KERNEL_VERSION (2,6,0))
                        dma_unmap_page (&pContext->pstPCIDevice->dev, qwPhysDMAAddress, pstUserPageListEntry->Length, eDMADir);
#   else
                        dma_unmap_page (NULL, qwPhysDMAAddress, pstUserPageListEntry->Length, eDMADir);
#   endif

                        // ----- page release and mark as dirty in case of read -----
                        if (eDMADir == DMA_FROM_DEVICE)
                            {
                            if (!PageReserved (pstUserPage))
                                SetPageDirty (pstUserPage);
                            }

#   if (LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0))
                        page_cache_release (pstUserPage);
#   else
                        put_page (pstUserPage);
#   endif

                        kfree (pstUserPageListEntry);
                        pstUserPageListEntry = pstNextUserPageListEntry;
                        }

/*
// TODO: UNMAP bei Liste von Seiten!
                    uint64 qwPhysDMAAddress = (((uint64)pWinSGList->Elements[dwElementIdx].Address.HighPart) << 32) | pWinSGList->Elements[dwElementIdx].Address.LowPart;
                    struct page* pstUserPage = pWinSGList->Elements[dwElementIdx].pstUserPage;
#   if (LINUX_VERSION_CODE > KERNEL_VERSION (2,6,0))
                    dma_unmap_page (&pContext->pstPCIDevice->dev, qwPhysDMAAddress, pWinSGList->Elements[dwElementIdx].Length, eDMADir);
#   else
                    dma_unmap_page (NULL, qwPhysDMAAddress, pWinSGList->Elements[dwElementIdx].Length, eDMADir);
#   endif

                    // ----- page release and mark as dirty in case of read -----
                    if (eDMADir == DMA_FROM_DEVICE)
                        {
                        if (!PageReserved (pstUserPage))
                            SetPageDirty (pstUserPage);
                        }

#   if (LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0))
                    page_cache_release (pstUserPage);
#   else
                    put_page (pstUserPage);
#   endif
*/
                    }
                }
            free_pages ((unsigned long)pDmaParams->winSGList[i], pDmaParams->winSGList[i]->lOrder);
#endif
            pDmaParams->winSGList[i] = NULL;
            }
        }

    pDmaParams->bufferCount = 0;
    pDmaParams->qwBufferSize = 0;

    pDmaParams->qwBytesTransfered = 0;
    pDmaParams->qwBytesAlreadyFree = 0;

    pDmaParams->busWidth = BUS_WIDTH_32;


    // clear sglist entries
    pSGListCurrentEntry = pXDMADmaParams->pXDMASGListStartEntry;
    for( i = 0; i < pDmaParams->dmaSGListElements; ++i )
        {
        memset (pSGListCurrentEntry, 0, sizeof (XDMA_SGLIST_ENTRY));
#   ifdef WINVER
        pSGListCurrentEntry++;
#   else
        pSGListCurrentEntry = pstGetXDMASGListEntry (pXDMADmaParams, i + 1);
#   endif
        }

    DMA_UNLOCK(pDmaParams);
    }

//------ SPCM4DRV_RefreshSGList ------
#ifdef WINVER
VOID SPCM4DRV_XDMA_RefreshSGList (IN OUT PSPCM4DRV_DEVICE_CONTEXT pContext, IN PXDMA_DMA_PARAMS pXDMADmaParams, IN ULONG64 qwNumberOfBytes, BOOLEAN bFlushSGList)
#else
void SPCM4DRV_XDMA_RefreshSGList (SPCM_ST_CARDINFO* pContext, PXDMA_DMA_PARAMS pXDMADmaParams, uint64 qwNumberOfBytes, bool bFlushSGList)
#endif
    {
    COMMON_DMA_PARAMS* pDmaParams = &pXDMADmaParams->stCommon;
    XDMA_SGLIST_ENTRY* pSGListCurrentFirstEntry;
    XDMA_SGLIST_ENTRY* pSGListCurrentLastEntry;
    XDMA_SGLIST_ENTRY* pSGListNewLastEntry;
    XDMA_SGLIST_ENTRY* pSGListFirstRefreshedEntry;
    ULONG64 qwBytesToShift;
    ULONG64 qwOrigBytesToShift;


#ifdef DEBUG_RESTART
#   ifdef WINVER
    PUCHAR pBar0Mem = pContext->memMappedAddress[0];
#   else
    uint32* pBar0Mem = pContext->apdwMemMappedAddress[0];
#   endif
    volatile UINT32 engCntl, reg_next_desc, reg_sw_desc, reg_compl_desc;
#endif

    // komplette Funktion mit Spinlock gekapselt
    DMA_LOCK(pDmaParams);

    if( !pDmaParams->active )
        {
        DMA_UNLOCK(pDmaParams);
        return;
        }
    
#ifdef DEBUG_RESTART
    engCntl = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
    reg_next_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR);
    reg_sw_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR);
    reg_compl_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_COMPLETED_DESC_PTR);
    SPCM4DRV_DebugPrintInfo( pContext, "RL cr:%4x pl:%8x nx:%8x sw:%8x | l:%4d f:%4d t:%4d r:%4d\n", 
                             engCntl, reg_compl_desc, reg_next_desc, reg_sw_desc, 
                            pDmaParams->pXDMASGListCurrentLastEntry->entryNumber2, 
                            pDmaParams->pXDMASGListCurrentFirstEntry->entryNumber2,
                            pDmaParams->pXDMASGListCurrentFirstTestEntry->entryNumber2,
                            (pDmaParams->pXDMASGListCurrentFirstRefreshedEntry == NULL) ? 0 : pDmaParams->pXDMASGListCurrentFirstRefreshedEntry->entryNumber2);
#endif // DEBUG_RESTART
    pSGListCurrentFirstEntry =   pXDMADmaParams->pXDMASGListCurrentFirstEntry;
    pSGListCurrentLastEntry =    pXDMADmaParams->pXDMASGListCurrentLastEntry;
    pSGListNewLastEntry =        pSGListCurrentLastEntry;

    pSGListFirstRefreshedEntry = pSGListCurrentFirstEntry;

    qwBytesToShift = qwNumberOfBytes + pDmaParams->qwBytesAlreadyFree;
    qwOrigBytesToShift = qwBytesToShift;

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_DMA, "RefreshSGList > numOfBytes=%llu", qwNumberOfBytes );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "RefreshSGList > numOfBytes=%llu\n", qwNumberOfBytes);

//    _printSGList = TRUE;
    XDMASGListDump( pContext->dwBoardNumber, pXDMADmaParams );
//    _printSGList = FALSE;

    // Solange was zu schieben ist ... 
    // OR: und der zu schiebende Bereich nicht kleiner als der nächste Eintrag ist

#ifndef WINVER
    uint32 dwCurrentFirstEntryIdx = dwGetIndexOfXDMASGListEntry (pXDMADmaParams, pSGListCurrentFirstEntry);
#endif
    while (qwBytesToShift >= pSGListCurrentFirstEntry->dwLength)
        {
        //SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "RefreshSGList: BytesToShift =%llu NextDescAddrLow: %u Len: %u\n", qwBytesToShift, pSGListCurrentFirstEntry->dwNextDescAddrLow, pSGListCurrentFirstEntry->dwLength);

        // Reset WriteBack entries?")

        // ... Die Blocklänge wird von der Zahl verarbeiteten Daten abgezogen.

        qwBytesToShift -= pSGListCurrentFirstEntry->dwLength;

#ifdef DMA_BOUNCE_BUFFER
        // force sync of user buffer to bounce buffer
        if (pDmaParams->writeToDevice)
            {
            dma_sync_single_for_device (&pContext->pstPCIDevice->dev, QWORD_FROM_DWORD(pSGListCurrentFirstEntry->dwSrcAddrHigh, pSGListCurrentFirstEntry->dwSrcAddrLow), pSGListCurrentFirstEntry->dwLength, DMA_TO_DEVICE);
            }
#endif

        // Wenn alle verarbeiteten Daten wieder verfügbar sind, dann ist der eben
        // wieder verwendbar gemachte Block nun der neue letzte Block.
        pSGListNewLastEntry = pSGListCurrentFirstEntry;

        // Der Zeiger wird auf nächsten Block gesetzt.
        if (pSGListCurrentFirstEntry == pXDMADmaParams->pXDMASGListLastEntry)
            {
            pSGListCurrentFirstEntry = pXDMADmaParams->pXDMASGListStartEntry;
#ifndef WINVER
            dwCurrentFirstEntryIdx = 0;
#endif
            }
        else
            {
#ifdef WINVER
            pSGListCurrentFirstEntry++;
#else
            dwCurrentFirstEntryIdx++;
            pSGListCurrentFirstEntry = pstGetXDMASGListEntry (pXDMADmaParams, dwCurrentFirstEntryIdx); // the possible wrap-around is handled above
#endif
            }

        // Wir zählen die wieder verfügbaren Blöcke mit.
        pDmaParams->dmaSGListRefreshedElements++;
        }

    if (bFlushSGList && qwBytesToShift != 0)
        {
        SPCM4DRV_DebugPrint(TRACE, pContext->dwBoardNumber, "Flush start - BytesToShift: %llu\n", qwBytesToShift);

        // ... Die Blocklänge wird von der Zahl verarbeiteten Daten abgezogen.
        qwBytesToShift -= qwBytesToShift;


        // Wenn alle verarbeiteten Daten wieder verfügbar sind, dann ist der eben
        // wieder verwendbar gemachte Block nun der neue letzte Block.
        pSGListNewLastEntry = pSGListCurrentFirstEntry;

        // Der Zeiger wird auf nächsten Block gesetzt.
        if (pSGListCurrentFirstEntry == pXDMADmaParams->pXDMASGListLastEntry)
            {
            pSGListCurrentFirstEntry = pXDMADmaParams->pXDMASGListStartEntry;
#ifndef WINVER
            dwCurrentFirstEntryIdx = 0;
#endif
            }
        else
            {
#ifdef WINVER
            pSGListCurrentFirstEntry++;
#else
            dwCurrentFirstEntryIdx++;
            pSGListCurrentFirstEntry = pstGetXDMASGListEntry (pXDMADmaParams, dwCurrentFirstEntryIdx);
#endif
            }

        // Wir zählen die wieder verfügbaren Blöcke mit.
        pDmaParams->dmaSGListRefreshedElements++;
        }
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "RefreshSGList: shifting done\n");

    // Die Zahl der verarbeiteten Bytes wird von 'qwBytesTransfered' abgezogen. 
    // OR: dabei lassen wir die nicht mehr geschobenen Bytes stehen fürs nächste Mal
    pDmaParams->qwBytesTransfered -=  (qwNumberOfBytes - qwBytesToShift + pDmaParams->qwBytesAlreadyFree);
    // SW 130726 pDmaParams->qwBytesTransfered -= numberOfBytes;
    pDmaParams->qwBytesAlreadyFree = qwBytesToShift;

    // Die neuen Zeiger auf den ersten und letzten Eintrag der SG-Liste werden gesichert.
    pXDMADmaParams->pXDMASGListCurrentFirstEntry = pSGListCurrentFirstEntry;
    pXDMADmaParams->pXDMASGListCurrentLastEntry =  pSGListNewLastEntry;

    // Wenn nötig wird auch der Zeiger auf den ersten aufgefüllten Eintrag gesichert.
    // und zwar nur, wenn genug Daten da waren, um auch einen Unterschied an der SG-Liste zu verursachen
    if( (pXDMADmaParams->pXDMASGListCurrentFirstRefreshedEntry == NULL) &&
        (qwBytesToShift != qwOrigBytesToShift))
        pXDMADmaParams->pXDMASGListCurrentFirstRefreshedEntry = pSGListFirstRefreshedEntry;

/*
    // remove stop flag from old end of list
    pSGListCurrentLastEntry->dwControl &= ~SGLIST_ENTRY_CONTROL_STOP_FETCH_DESC;
    // ... and set it again on new end of list
    pXDMADmaParams->pXDMASGListCurrentLastEntry->dwControl |= SGLIST_ENTRY_CONTROL_STOP_FETCH_DESC;
*/
    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_DMA, "XDMA_RefreshSGList < transferred:%llu", pDmaParams->qwBytesTransfered );
    SPCM4DRV_DebugPrint(TRACE, pContext->dwBoardNumber, "XDMA_RefreshSGList < byteTransferred:%llu\n", pDmaParams->qwBytesTransfered);
    if (pXDMADmaParams->pXDMASGListCurrentFirstRefreshedEntry != NULL)
        SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "XDMA_RefreshSGList FirstRefreshedIdx: %lu\n", dwGetIndexOfXDMASGListEntry (pXDMADmaParams, pXDMADmaParams->pXDMASGListCurrentFirstRefreshedEntry));
        

     //_printSGList = TRUE;
     XDMASGListDump( pContext->dwBoardNumber, pXDMADmaParams );
     //_printSGList = FALSE;

#ifdef DEBUG_RESTART
    if( _DebugRestart && (pSGListCurrentFirstEntry->entryNumber2 != 1))
        {
        size_t strLength = 0;
        ULONG i;

        RtlStringCbLengthA( pContext->debugInfo, DEBUG_INFO_BUFFER_SIZE, &strLength );
        DbgPrint( "len:%d\n", strLength );
        for( i = 0; i < (strLength / 512) + 1; ++i )
            DbgPrint( &(pContext->debugInfo[512*i]) );

        DbgBreakPoint();
        }
#endif // DEBUG_RESTART

    // Falls DMA inzwischen fertig ist, müsssen wir es neu anstoßen!
    if (qwBytesToShift != qwOrigBytesToShift) // shifted something
        {
        pXDMADmaParams->lCnt = 0;
        SPCM4DRV_XDMA_DMA_Restart (pContext, pXDMADmaParams, FALSE);
        }

#ifdef DEBUG_RESTART
    if( _DebugRestart )
        {
        _DebugRestart = FALSE;
        }
#endif // DEBUG_RESTART

    // OR: erst ganz am Ende wieder freigeben
    DMA_UNLOCK(pDmaParams);

    return;
    }


//------ vDMA_Restart ------
#ifdef WINVER
BOOLEAN CheckDmaChannel( PSPCM4DRV_DEVICE_CONTEXT pContext, void* pvDmaParams, BOOLEAN bFromDPC);
VOID SPCM4DRV_XDMA_DMA_Restart (IN OUT PSPCM4DRV_DEVICE_CONTEXT pContext, IN PXDMA_DMA_PARAMS pXDMADmaParams, BOOLEAN fromDPC)
#else
bool bCheckDMAChannel (SPCM_ST_CARDINFO* pstCard, void* pvDMAParams, bool bFromDPC);
void SPCM4DRV_XDMA_DMA_Restart (SPCM_ST_CARDINFO* pContext, PXDMA_DMA_PARAMS pXDMADmaParams, bool fromDPC)
#endif
    {
    COMMON_DMA_PARAMS* pDmaParams = &pXDMADmaParams->stCommon;
    ULONG dwStatus = 0;
    ULONG dwCompleteDescCnt = 0;
#ifdef WINVER
    PUCHAR pBar1Mem = pContext->memMappedAddress[1];
#else
    uint32* pBar1Mem = pContext->apdwMemMappedAddress[1];
#endif

#ifdef DEBUG_RESTART
    CHAR pR = (fromDPC) ? 'S' : 's';

    if( !fromDPC && _DebugRestart && (pDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2 != 1))
        DbgPrint("0:%d\n", pDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2);
#endif // DEBUG_RESTART
    
    if( !pDmaParams->active )
        {
        SPCM4DRV_DebugPrint (ERROR, pContext->dwBoardNumber, "XDMA_DMA_Restart: DMA not active!\n");
#ifdef WINVER
        //DbgBreakPoint();
#endif
        return;
        }

    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "SPCM4DRV_XDMA_DMA_Restart (fromDPC: %c)\n", fromDPC? '1' : '0');

    if (!pXDMADmaParams->pXDMASGListCurrentFirstRefreshedEntry)
        {
        SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "!!! Restart: FirstRefresh == NULL\n");
        return;
        }

    dwStatus = XDMA_ReadByOffset (pBar1Mem, pDmaParams->dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_STATUS);
    dwCompleteDescCnt = XDMA_ReadByOffset (pBar1Mem, pDmaParams->dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_COMPL_DESC_CNT);
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "!!! Restart: Status: %08x  NumDesc: %u  NumProcessedDesc: %u fromDPC: %c\n", dwStatus, dwCompleteDescCnt, pXDMADmaParams->dwNumProcessedCompletedDesc, fromDPC? '1' : '0');

    if (pXDMADmaParams->dwNumProcessedCompletedDesc == dwCompleteDescCnt)
        {
        if (!(dwStatus & CHANNEL_STATUS_BUSY))
            {
            ULONG dwIdx = 0;
#ifdef WINVER
            ULONG64 dmaDescPtr = 0;
#else
            dma_addr_t dmaDescPtr = 0;
#endif
            ULONG dmaDescrPtrLow = 0;
            ULONG dmaDescrPtrHigh = 0;

            ULONG dwStopRemovedFrom = 0;
            XDMA_SGLIST_ENTRY* pXDMASGListPrevLastEntry =    NULL;
            XDMA_SGLIST_ENTRY* pXDMASGListCurrentLastEntry = NULL;
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "!!! Restart: Restarting\n");

            pXDMADmaParams->pXDMASGListCurrentSGListStart = pXDMADmaParams->pXDMASGListCurrentFirstRefreshedEntry;
            dwIdx = dwGetIndexOfXDMASGListEntry (pXDMADmaParams, pXDMADmaParams->pXDMASGListCurrentFirstRefreshedEntry);

            // remove stop flag from old end of list
            if (dwIdx == 0)
                {
                pXDMASGListPrevLastEntry = pXDMADmaParams->pXDMASGListLastEntry;
                dwStopRemovedFrom = dwIdx;
                }
            else
                {
                pXDMASGListPrevLastEntry = pstGetXDMASGListEntry (pXDMADmaParams, dwIdx - 1);
                dwStopRemovedFrom = dwIdx -1;
                }
            pXDMASGListPrevLastEntry->dwControl &= ~SGLIST_ENTRY_CONTROL_STOP_FETCH_DESC;
            // ... and set it again on new end of list
            pXDMASGListCurrentLastEntry = pstGetXDMASGListEntry (pXDMADmaParams, (dwIdx + pDmaParams->dmaSGListRefreshedElements -1) % pXDMADmaParams->stCommon.dmaSGListElements);
            pXDMASGListCurrentLastEntry->dwControl |= SGLIST_ENTRY_CONTROL_STOP_FETCH_DESC;

            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "!!! Restart: deleted STOP from entry %u\n", dwStopRemovedFrom);

            pXDMADmaParams->dwNumProcessedCompletedDesc = 0;
            pDmaParams->dmaSGListRefreshedElements = 0;
            pXDMADmaParams->pXDMASGListCurrentFirstTestEntry = pXDMADmaParams->pXDMASGListCurrentSGListStart;

            dmaDescPtr = qwGetPhysAddr (pXDMADmaParams, dwIdx);
            dmaDescrPtrLow =   LOW_DWORD (dmaDescPtr);
            dmaDescrPtrHigh = HIGH_DWORD (dmaDescPtr);

            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "Idx: %u newFirstDescAddr:%08x:%08x\n", dwIdx, dmaDescrPtrHigh, dmaDescrPtrLow);
            XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngAddrOffs + XDMA_REG_CHANNEL_SGDMA_LOW_ADDR,  dmaDescrPtrLow);
            XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngAddrOffs + XDMA_REG_CHANNEL_SGDMA_HIGH_ADDR, dmaDescrPtrHigh);
            pXDMADmaParams->pXDMASGListCurrentFirstRefreshedEntry = NULL;

            //_printSGList = TRUE;
            XDMASGListDump( pContext->dwBoardNumber, pXDMADmaParams );
            //_printSGList = FALSE;

            // restart DMA by toggling RUN bit
            XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_CONTROL_CLR, CHANNEL_CONTROL_RUN);
            XDMA_WriteByOffset (pBar1Mem, pDmaParams->dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_CONTROL_SET, CHANNEL_CONTROL_RUN);
            }
        }
    else //if (dwStatus & CHANNEL_STATUS_DESC_STOPPED)
        {
        // es wurden nicht alle Descriptoren in der DPC abgearbeitet. Da manchmal weniger Interrupts als erwartet zu kommen scheinen (zusammmengefasst? oder zu schnell? trat bevorzugt bei kleineren Blöcken auf), starten wir hier die Prüfung, die normalerweise in der DPC stattfinden würde, nochmal.
        pXDMADmaParams->lCnt++;
        if (pXDMADmaParams->lCnt < 10) // nur begrenzt of wieder den DMA-Kanal prüfen, weil sonst eine Endlosschleife entstehen kann
            {
            //SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "!!! Restart: return to processing\n");
#ifdef WINVER
            BOOLEAN bWakeUp = CheckDmaChannel (pContext, pXDMADmaParams, FALSE);
            if (bWakeUp)
                {
                if (pDmaParams->writeToDevice)
                    {
                    if (pContext->pS2CEvent)
                        KeSetEvent (pContext->pS2CEvent, IO_NO_INCREMENT, FALSE);
                    }
                else
                    {
                    if (pContext->pC2SEvent)
                        KeSetEvent (pContext->pC2SEvent, IO_NO_INCREMENT, FALSE);
                    }
                }
#else
            bool bWakeUp = bCheckDMAChannel (pContext, pXDMADmaParams, false);
            if (bWakeUp)
                {
                pContext->bWakeUp = 1;
                wake_up_interruptible (&pContext->wqKernelEvent);
                }
#endif
            }
        }
    }

// ----- Debug Funktion zum Liste Dumpen -----
#ifdef WINVER
VOID XDMASGListDump (ULONG dwBrdNr, PXDMA_DMA_PARAMS pXDMADmaParams)
#else
void XDMASGListDump (uint32 dwBrdNr, PXDMA_DMA_PARAMS pXDMADmaParams)
#endif
    {
    ULONG i;

    XDMA_SGLIST_ENTRY* pSGList = pXDMADmaParams->pXDMASGListStartEntry;

    if( !_printSGList )
        return;

#if (1)
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "***************************************\n");
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "    %8s %8s %8s %8s %8s %8s %8s %8s %8s\n",
                        "Control", "Length", "SrcLow", "SrcHigh", "DestLow",
                        "DestHigh", "NextLow", "NextHigh", "Flags");
    for (i=0; i < pXDMADmaParams->stCommon.dmaSGListElements; i++)
        {
        if (pSGList != NULL)
            SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "%02u: %08x %08x %08x %08x %08x %08x %08x %08x %s %s\n",
            i, pSGList->dwControl, pSGList->dwLength, pSGList->dwSrcAddrLow, 
            pSGList->dwSrcAddrHigh, pSGList->dwDestAddrLow, pSGList->dwDestAddrHigh,
            pSGList->dwNextDescAddrLow, pSGList->dwNextDescAddrHigh,
            ((pSGList->dwControl & SGLIST_ENTRY_CONTROL_IRQONCOMPL) ? "IntCmpl" : " "),
            ((pSGList->dwControl & SGLIST_ENTRY_CONTROL_STOP_FETCH_DESC) ? "Stop" : " "));
#   ifdef WINVER
        pSGList++;
#   else
        pSGList = pstGetXDMASGListEntry (pXDMADmaParams, i + 1);
#   endif
        }
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "***************************************\n");
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "WriteBack\n");
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "    %8s %8s\n",
                        "Status", "Length");
    for (i=0; i < pXDMADmaParams->stCommon.dmaSGListElements; i++)
        {
        XDMA_C2H_WRITEBACK* pWriteBack = pstGetXDMAWriteBackEntry (pXDMADmaParams, i);
        SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "%02u: %08x %08x\n",
            i, pWriteBack->dwStatus, pWriteBack->dwLength);
        
        }
#endif

    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "Elements:           %d\n", pXDMADmaParams->stCommon.dmaSGListElements);
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "currentFirst:       %d\n", dwGetIndexOfXDMASGListEntry (pXDMADmaParams, pXDMADmaParams->pXDMASGListCurrentFirstEntry));
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "currentLast:        %d\n", dwGetIndexOfXDMASGListEntry (pXDMADmaParams, pXDMADmaParams->pXDMASGListCurrentLastEntry));
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "currentFirstTest:   %d\n", dwGetIndexOfXDMASGListEntry (pXDMADmaParams, pXDMADmaParams->pXDMASGListCurrentFirstTestEntry));
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "currentSGListStart: %d\n", dwGetIndexOfXDMASGListEntry (pXDMADmaParams, pXDMADmaParams->pXDMASGListCurrentSGListStart));
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "bytesTransferred:  %llu\n", pXDMADmaParams->stCommon.qwBytesTransfered);
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "bytesAlreadyFree:  %llu\n", pXDMADmaParams->stCommon.qwBytesAlreadyFree);
    //SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "***************************************\n");
    }

