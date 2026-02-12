//#define NWC

#ifdef WINVER
#   include "ntddk.h"
#   include "wdf.h"

#   include ".\m4i_krnl_wdm\prototypes.h"
#   include ".\m4i_krnl_wdm\spcm4drv.h"
#   include "dma-nwc.h"

#   include ".\m4i_krnl_wdm\trace.h"
#   include "dma-nwc.tmh"

static BOOLEAN _printSGList = DEBUG_PRINT_SG_LIST;

#    ifdef DEBUG_RESTART
extern BOOLEAN _DebugRestart;
extern ULONG   __dwDebugLevel;
#   endif // DEBUG_RESTART

VOID NwcSGListDump2 (PSPCM4DRV_DEVICE_CONTEXT pContext, PNWC_DMA_PARAMS pDmaParams);

#   define USLEEP(x) KeStallExecutionProcessor(x)
#   define DMA_LOCK(pDmaParams) WdfSpinLockAcquire (pDmaParams->spinLock)
#   define DMA_UNLOCK(pDmaParams) WdfSpinLockRelease (pDmaParams->spinLock)
#   define INTERRUPT_LOCK(pContext) WdfInterruptAcquireLock(pContext->astInterruptHandles[0])
#   define INTERRUPT_UNLOCK(pContext) WdfInterruptReleaseLock( pContext->astInterruptHandles[0])

#   define HIGH_DWORD(qwQuad) ((ULONG)((qwQuad >> 32) & 0xFFFFFFFF))
#   define  LOW_DWORD(qwQuad) ((ULONG)(qwQuad & 0xFFFFFFFF))

#else // Linux
#   include <asm/page.h> // PAGE_SIZE
#   include <linux/pagemap.h> // page_cache_release
#   include "../m4i_krnl_linux/spcm_linux_card.h"
#   include "../m4i_krnl_linux/spcm_linux_debug.h"
#   include "../m4i_krnl_linux/spcm_linux_wrapper.h"
#   include "../m2i_krnl/spcm2_krnl_general.h"
#   include "nwdcore.h"
#   include "dma-nwc.h"
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

void NwcSGListDump2 (SPCM_ST_CARDINFO* pContext, PNWC_DMA_PARAMS pNWCDmaParams);
void NwcDMADump (SPCM_ST_CARDINFO* pContext, uint32 dwBrdNr, PCOMMON_DMA_PARAMS pDmaParams);

NWCORE_SGLIST_ENTRY* pstGetNWCSGListEntry (NWC_DMA_PARAMS* pDmaParams, uint32 dwIdx)
    {
    // one page contains PAGE_SIZE / sizeof(NWCORE_SGLIST_ENTRY) = 4096 / 32 = 128 (=0x80) NWCORE_SGLIST_ENTRYs
    return (pDmaParams->ppstSGListPages[dwIdx / (PAGE_SIZE / sizeof (NWCORE_SGLIST_ENTRY))] + (dwIdx & (PAGE_SIZE / sizeof (NWCORE_SGLIST_ENTRY) - 1)));
    }

static dma_addr_t qwGetPhysAddr (NWC_DMA_PARAMS* pDmaParams, uint32 dwIdx)
    {
    // the old way (pre-ARM)
    //return __pa (pstGetNWCSGListEntry (pDmaParams, dwIdx));

    // each DmaHandle belongs to one page, and each page holds  (PAGE_SIZE / sizeof(NWCORE_SGLIST_ENTRY) = 4096 / 32 = 128 entries for the scatter gather list
    return pDmaParams->stCommon.aqwDmaHandles[dwIdx / (PAGE_SIZE / sizeof (NWCORE_SGLIST_ENTRY))] + (dwIdx & (PAGE_SIZE / sizeof (NWCORE_SGLIST_ENTRY) - 1)) * sizeof (NWCORE_SGLIST_ENTRY);
    }

static bool bDMAMappingError (struct device* pstDevice, dma_addr_t qwPhysDMAAddress)
	{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION (2,6,27))
    return dma_mapping_error (pstDevice, qwPhysDMAAddress);
#else
    return dma_mapping_error (qwPhysDMAAddress);
#endif
	}

void NwcSGListDump (uint32 dwBrdNr, COMMON_DMA_PARAMS* pDmaParams);

#   define USLEEP(x) udelay(x)
#   define DMA_LOCK(pDmaParams) down(&pDmaParams->semAccess)
#   define DMA_UNLOCK(pDmaParams) up(&pDmaParams->semAccess)

extern spinlock_t stIRQLock; // defined in spcm_linux_isr.c
#   define INTERRUPT_LOCK(pContext) spin_lock_irqsave(&stIRQLock, dwLocalIRQLockFlags);
#   define INTERRUPT_UNLOCK(pContext) spin_unlock_irqrestore(&stIRQLock, dwLocalIRQLockFlags)

#   define HIGH_DWORD(qwQuad) ((uint32)((qwQuad >> 32) & 0xFFFFFFFF))
#   define  LOW_DWORD(qwQuad) ((uint32)(qwQuad & 0xFFFFFFFF))
#   define QWORD_FROM_DWORD(dwHigh, dwLow)((((unsigned long long)dwHigh) << 32) | dwLow)
#endif


//------ SPCM4DRV_NWC_InitDma ------
#ifdef WINVER
NTSTATUS SPCM4DRV_NWC_InitDma( IN PSPCM4DRV_DEVICE_CONTEXT pContext )
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
    for( channel = 0; channel < SPCM4DRV_DMA_NUMBER_OF_C2S_CHANNELS; ++channel )
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
                                      &pContext->c2sNwcDmaParams[channel].stCommon.dmaEnablerDesc );
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
        requestedSGListMemSize = (size_t) (sizeof(NWCORE_SGLIST_ENTRY) * maxMapRegisters);
        allocatedSGListMemSize = 0;

        // alloc memory for the scatter gather list
        //-------------------------------------------------
        status = SPCM4DRV_AllocSGListMemory (NWC, &pContext->c2sNwcDmaParams[channel].stCommon, 
                                              requestedSGListMemSize, 
                                              &allocatedSGListMemSize );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "AllocSGListMemory(c2s-desc) failed with status %!STATUS!", status);
            return status;
        }
        
        if( channel == 0 )
        {
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "c2s[0] SGList-virt: %p\n", pContext->c2sNwcDmaParams[0].stCommon.dmaSGListVirtualAddress);
        }


        // In Abhängigkeit vom tatsächlich allokierten SGList-Speicher berechnen wir die
        // maximale mögliche Transfer-Grösse für diesen Kanal neu und merken uns diese.
        //-------------------------------------------------
        maxMapRegisters = (ULONG)(allocatedSGListMemSize / sizeof(NWCORE_SGLIST_ENTRY));
        maxChannelTransferLength  = maxMapRegisters - 1;
        maxChannelTransferLength *= PAGE_SIZE;
        pContext->c2sNwcDmaParams[channel].stCommon.dmaMaxMapRegisters = maxMapRegisters;  
        pContext->c2sNwcDmaParams[channel].stCommon.dmaMaximumTransferLength = maxChannelTransferLength;

        // Init enabler configuration - jetzt für den tatsächlichen Datentransfer.
        //-------------------------------------------------
        WDF_DMA_ENABLER_CONFIG_INIT( &dmaConfig,
                                     WdfDmaProfileScatterGather64,
                                     pContext->c2sNwcDmaParams[channel].stCommon.dmaMaximumTransferLength );


        // Create DMA enabler for data transfers
        //-------------------------------------------------
        status = WdfDmaEnablerCreate( device,
                                      &dmaConfig,
                                      WDF_NO_OBJECT_ATTRIBUTES,
                                      &pContext->c2sNwcDmaParams[channel].stCommon.dmaEnablerData );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaEnablerCreate(c2s-data) failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: WdfDmaEnablerCreate(c2s-data) failed! status=0x%x", status));
            return status;
        }

        // get maximum and fragmented length
        //-------------------------------------------------
        maxEnablerLength  = WdfDmaEnablerGetMaximumLength (pContext->c2sNwcDmaParams[channel].stCommon.dmaEnablerData);
        fragEnablerLength = WdfDmaEnablerGetFragmentLength (pContext->c2sNwcDmaParams[channel].stCommon.dmaEnablerData, 
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
        pContext->c2sNwcDmaParams[channel].stCommon.pDmaAdapter = WdfDmaEnablerWdmGetDmaAdapter(pContext->c2sNwcDmaParams[channel].stCommon.dmaEnablerData,
                                                                                    WdfDmaDirectionReadFromDevice );
        if( pContext->c2sNwcDmaParams[channel].stCommon.pDmaAdapter == NULL)
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaEnablerWdmGetDmaAdapter failed!");
            SPCM4Print(("InitDMA: WdfDmaTransactionCreate failed!"));
            return STATUS_UNSUCCESSFUL;
        }

        // Create one DMA transaction per channel
        //-------------------------------------------------
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE( &attributes, TRANSACTION_CONTEXT );
        status = WdfDmaTransactionCreate( pContext->c2sNwcDmaParams[channel].stCommon.dmaEnablerData,
                                          &attributes,
                                          &pContext->c2sNwcDmaParams[channel].stCommon.dmaTransaction );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaTransactionCreate failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: WdfDmaTransactionCreate failed! status=0x%x", status));
            return status;
        }

        pContext->c2sNwcDmaParams[channel].stCommon.active        = FALSE;
        pContext->c2sNwcDmaParams[channel].stCommon.somethingToDo = FALSE;
        pContext->c2sNwcDmaParams[channel].stCommon.busWidth      = BUS_WIDTH_32;
        pContext->c2sNwcDmaParams[channel].stCommon.qwBufferSize  = 0;
        pContext->c2sNwcDmaParams[channel].stCommon.dmaSGListElements = 0;
        pContext->c2sNwcDmaParams[channel].stCommon.dwEngAddrOffs = NWD_BASE_C2S_ENGINES +
                                                        channel * NWD_DESCR_ENG_OFFSETS;

        // Create a WDFSPINLOCK object to protect accesses to shared channel data
        //-------------------------------------------------
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = device;
        status = WdfSpinLockCreate( &attributes, &pContext->c2sNwcDmaParams[channel].stCommon.spinLock );
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
    for( channel = 0; channel < SPCM4DRV_DMA_NUMBER_OF_S2C_CHANNELS; ++channel )
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
                                      &pContext->s2cNwcDmaParams[channel].stCommon.dmaEnablerDesc );
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
        requestedSGListMemSize = (size_t) (sizeof(NWCORE_SGLIST_ENTRY) * maxMapRegisters);
        allocatedSGListMemSize = 0;

        // alloc memory for the scatter gather list
        //-------------------------------------------------
        status = SPCM4DRV_AllocSGListMemory (NWC, &pContext->s2cNwcDmaParams[channel].stCommon, 
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
        maxMapRegisters = (ULONG)(allocatedSGListMemSize / sizeof(NWCORE_SGLIST_ENTRY));
        maxChannelTransferLength  = maxMapRegisters - 1;
        maxChannelTransferLength *= PAGE_SIZE;
        pContext->s2cNwcDmaParams[channel].stCommon.dmaMaxMapRegisters = maxMapRegisters;  
        pContext->s2cNwcDmaParams[channel].stCommon.dmaMaximumTransferLength = maxChannelTransferLength;

        // Init enabler configuration - jetzt für den tatsächlichen Datentransfer.
        //-------------------------------------------------
        WDF_DMA_ENABLER_CONFIG_INIT( &dmaConfig,
                                     WdfDmaProfileScatterGather64,
                                     pContext->s2cNwcDmaParams[channel].stCommon.dmaMaximumTransferLength );


        // Create DMA enabler for data transfers
        //-------------------------------------------------
        status = WdfDmaEnablerCreate( device,
                                      &dmaConfig,
                                      WDF_NO_OBJECT_ATTRIBUTES,
                                      &pContext->s2cNwcDmaParams[channel].stCommon.dmaEnablerData );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaEnablerCreate(s2c-data) failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: WdfDmaEnablerCreate(s2c-data) failed! status=0x%x", status));
            return status;
        }

        // get maximum and fragmented length
        //-------------------------------------------------
        maxEnablerLength  = WdfDmaEnablerGetMaximumLength (pContext->s2cNwcDmaParams[channel].stCommon.dmaEnablerData);
        fragEnablerLength = WdfDmaEnablerGetFragmentLength (pContext->s2cNwcDmaParams[channel].stCommon.dmaEnablerData, 
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
        pContext->s2cNwcDmaParams[channel].stCommon.pDmaAdapter = WdfDmaEnablerWdmGetDmaAdapter(pContext->s2cNwcDmaParams[channel].stCommon.dmaEnablerData,
                                                                                    WdfDmaDirectionWriteToDevice );
        if( pContext->s2cNwcDmaParams[channel].stCommon.pDmaAdapter == NULL)
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaEnablerWdmGetDmaAdapter failed!");
            SPCM4Print(("InitDMA: WdfDmaTransactionCreate failed!"));
            return STATUS_UNSUCCESSFUL;
        }

        // Create one DMA transaction per channel
        //-------------------------------------------------
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE( &attributes, TRANSACTION_CONTEXT );
        status = WdfDmaTransactionCreate( pContext->s2cNwcDmaParams[channel].stCommon.dmaEnablerData,
                                          &attributes,
                                          &pContext->s2cNwcDmaParams[channel].stCommon.dmaTransaction );
        if (!NT_SUCCESS(status)) 
        {
            TraceEvent(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDmaTransactionCreate failed with status %!STATUS!", status);
            SPCM4Print(("InitDMA: WdfDmaTransactionCreate failed! status=0x%x", status));
            return status;
        }

        pContext->s2cNwcDmaParams[channel].stCommon.active        = FALSE;
        pContext->s2cNwcDmaParams[channel].stCommon.somethingToDo = FALSE;
        pContext->s2cNwcDmaParams[channel].stCommon.busWidth      = BUS_WIDTH_32;
        pContext->s2cNwcDmaParams[channel].stCommon.qwBufferSize  = 0;
        pContext->s2cNwcDmaParams[channel].stCommon.dmaSGListElements = 0;
        pContext->s2cNwcDmaParams[channel].stCommon.dwEngAddrOffs = NWD_BASE_S2C_ENGINES +
                                                        channel * NWD_DESCR_ENG_OFFSETS;

        // Create a WDFSPINLOCK object to protect accesses to shared channel data
        //-------------------------------------------------
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = device;
        status = WdfSpinLockCreate( &attributes, &pContext->s2cNwcDmaParams[channel].stCommon.spinLock );
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
int8 byNWCDMAObjectInit (SPCM_ST_CARDINFO* pstCard)
    {
    uint32 dwChannel = 0;
    for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_C2S_CHANNELS; ++dwChannel)
        {
        NWC_DMA_PARAMS* pstDMAParams = pstCard->astNWC_C2SDMAParams + dwChannel;
        pstDMAParams->stCommon.active = false;
        pstDMAParams->stCommon.somethingToDo = false;
        pstDMAParams->stCommon.busWidth = BUS_WIDTH_32;
        pstDMAParams->stCommon.qwBufferSize = 0;
        pstDMAParams->stCommon.dmaSGListElements = 0;
        pstDMAParams->stCommon.dwEngAddrOffs = NWD_BASE_C2S_ENGINES + dwChannel * NWD_DESCR_ENG_OFFSETS;
        pstDMAParams->stCommon.qwMaxMappedBufferSize = 0;
        pstDMAParams->stCommon.bPageAlignedBuffer = false;
        sema_init (&pstDMAParams->stCommon.semAccess, 1);
        }
    for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_S2C_CHANNELS; ++dwChannel)
        {
        NWC_DMA_PARAMS* pstDMAParams = pstCard->astNWC_S2CDMAParams + dwChannel;
        pstDMAParams->stCommon.active = false;
        pstDMAParams->stCommon.somethingToDo = false;
        pstDMAParams->stCommon.busWidth = BUS_WIDTH_32;
        pstDMAParams->stCommon.qwBufferSize = 0;
        pstDMAParams->stCommon.dmaSGListElements = 0;
        pstDMAParams->stCommon.dwEngAddrOffs = NWD_BASE_S2C_ENGINES + dwChannel * NWD_DESCR_ENG_OFFSETS;
        pstDMAParams->stCommon.qwMaxMappedBufferSize = 0;
        pstDMAParams->stCommon.bPageAlignedBuffer = false;
        sema_init (&pstDMAParams->stCommon.semAccess, 1);
        }
        
    return 0;
    }

int8 byNWCAllocateMemoryForSGList (SPCM_ST_CARDINFO* pstCardInfo, NWC_DMA_PARAMS* pstNWCDMAParams, bool bPageAlignedBuffer, size_t qwRequestedMemSize_bytes, uint32 dwNotifySize_bytes)
    {
    size_t allocatedSGListMemSize = 0;
    int8 byStatus = 0;
    COMMON_DMA_PARAMS* pstDMAParams = & pstNWCDMAParams->stCommon;

    // re-use existing buffer if it is large enough
    if ((qwRequestedMemSize_bytes <= pstDMAParams->qwMaxMappedBufferSize)
        && (bPageAlignedBuffer == pstDMAParams->bPageAlignedBuffer))
        {
        DEBUGLOG (DBG_TRACE, "Reusing buffer\n");
        return 0;
        }

    // clear previously allocated memory
    if (pstDMAParams->qwMaxMappedBufferSize != 0)
        {
        DEBUGLOG (DBG_TRACE, "Freeing old buffer\n");
        SPCM4DRV_FreeSGListMemory (pstCardInfo, pstDMAParams);
        }

    // alloc memory for the scatter gather list
    //-------------------------------------------------
    DEBUGLOG (DBG_TRACE, "Allocating buffer for %zu bytes\n", qwRequestedMemSize_bytes);
    byStatus = SPCM4DRV_AllocSGListMemory (pstCardInfo, pstDMAParams, qwRequestedMemSize_bytes, dwNotifySize_bytes, bPageAlignedBuffer, &allocatedSGListMemSize);
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
VOID SPCM4DRV_NWC_ClearDma (IN PSPCM4DRV_DEVICE_CONTEXT pContext)
#else
void SPCM4DRV_NWC_ClearDma (SPCM_ST_CARDINFO* pContext)
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
VOID SPCM4DRV_NWC_BuildSGList (IN PSPCM4DRV_DEVICE_CONTEXT pContext, PMDL pMdl, PNWC_DMA_PARAMS pNWCDmaParams)
#else
int SPCM4DRV_NWC_BuildSGList (SPCM_ST_CARDINFO* pstCard, void* pvUserBuffer, uint64 qwByteCount, int8 bReadDir, int8 bGPUUsed, uint64 qwNotifySize, PNWC_DMA_PARAMS pNWCDmaParams, bool* pb64BitAddress)
#endif
{
    COMMON_DMA_PARAMS* pDmaParams = &pNWCDmaParams->stCommon;
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
    // Bei Erfolg wird die Funktion 'SPCM4DRV_NWC_SaveSGList' gerufen-
    NTStatus =
    pDmaAdapter->DmaOperations->GetScatterGatherList (pDmaAdapter,
                                                      pContext->pWdmDevObj,
                                                      pMdl,
                                                      virtualAddress,
                                                      byteCount,
                                                      SPCM4DRV_NWC_SaveSGList,
                                                      pNWCDmaParams,
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
#       else // Intel/AMD or Clara with dGPU
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
#       else // Intel/AMD or Clara with dGPU
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
                }

#       endif
            // if buffer is smaller than one page
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
#       else // Intel/AMD or Clara with dGPU
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
#       else // Intel/AMD or Clara with dGPU
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
            pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].Length               = qwLen;
            pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].Address.LowPart      = qwPhysDMAAddress & 0xFFFFFFFF;
            pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].Address.HighPart     = (qwPhysDMAAddress >> 32) & 0xFFFFFFFF;
            pstCurrentWinSGList->Elements[dwCurrentSGElementIdx].pvUserPageOrPageList = pstUserPage;
            pstCurrentWinSGList->NumberOfElements++;

            // ----- prepare next loop
            dwCurrentSGElementIdx++;
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
    SPCM4DRV_NWC_ClearData (pstCard, pNWCDmaParams);

#   if (LINUX_VERSION_CODE >= KERNEL_VERSION (5,8,0))
    up_read (&current->mm->mmap_lock);
#   else
    up_read (&current->mm->mmap_sem);
#   endif
    up (&pDmaParams->semAccess);

    return -EFAULT;
#endif // WINVER
}

//------ SPCM4DRV_NWC_SaveSGList ------
#ifdef WINVER
VOID SPCM4DRV_NWC_SaveSGList (PDEVICE_OBJECT pDevObj, PIRP pIrpIfSystemQueing, PSCATTER_GATHER_LIST pWinSGList, PVOID pvParam)
#else
void SPCM4DRV_NWC_SaveSGList (PSCATTER_GATHER_LIST pWinSGList, void* pvParam)
#endif
{
    COMMON_DMA_PARAMS* pDmaParams = (COMMON_DMA_PARAMS*) pvParam;

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_DMA, "NWC_SaveSGList - Win-SGList has %d elements", pWinSGList->NumberOfElements );
    SPCM4DRV_DebugPrint (TRACE, 1, 
                         "SPCM4DRV_NWC_SaveSGList - Win-SGList has %d elements\n",
                         pWinSGList->NumberOfElements);

    // save sglist because later we have to free it
    pDmaParams->winSGList[pDmaParams->bufferCount-1] = pWinSGList;
}

//------ SPCM4DRV_StartTransfer ------
//
// In dieser Funktionen bauen wir aus den gespeicherten SG-Listen eine Liste
// von Liste von DMA-Aufträgen in dem Format, den der PLX benötigt. Die Liste wird
// dem PLX übergeben und dann der DMA-Transfer gestartet.
//
#ifdef WINVER
INT8 SPCM4DRV_NWC_StartTransfer (IN PSPCM4DRV_DEVICE_CONTEXT pContext, PNWC_DMA_PARAMS pNWCDmaParams,
                             ULONG64 qwByteOffset, ULONG64 qwTransferSize, ULONG dwNotifySize)
#else
int8 SPCM4DRV_NWC_StartTransfer (SPCM_ST_CARDINFO* pContext, PNWC_DMA_PARAMS pNWCDmaParams, uint64 qwByteOffset, uint64 qwTransferSize, uint32 dwNotifySize)
#endif
{
    ULONG bufferNo, i, dwSGEntryNum, dmaDescrPtr;
    COMMON_DMA_PARAMS* pDmaParams = &pNWCDmaParams->stCommon;
    PNWCORE_SGLIST_ENTRY pSGListCurrentEntry, pSGListLastEntry, pSGListLastPhysEntry;
    SPCM2_DMA_BUSWIDTH busWidth;
    ULONG64 qwInitialAvail = qwTransferSize;
    ULONG dwBytesSinceLastInterrupt;
    UINT32 dwReg;
    BOOLEAN isTransferUsingWholeBuffer = TRUE;
    ULONG64 qwPhysicalAddress = 0;
#ifdef WINVER
    PCHAR pBar0Mem = (PCHAR)pContext->memMappedAddress[0];
#else
    uint32* pBar0Mem = pContext->apdwMemMappedAddress[0];
    unsigned long dwLocalIRQLockFlags;
#endif

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_DMA, "StartTransfer" );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "StartTransfer\n");

    // set pointer to BAR0 memory region

    // get the pointer of the sglist memory allocated in InitDMA
    pSGListCurrentEntry  = pNWCDmaParams->pNwcSGListStartEntry;
    pSGListLastEntry     = pSGListCurrentEntry;
    pSGListLastPhysEntry = pSGListCurrentEntry;

    // set number of sglist entry
    dwSGEntryNum = 1;
    dwBytesSinceLastInterrupt = 0;
    pDmaParams->dmaSGListElements = 0;


    // build a sglist in the format needed by NWC
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "   StartTransfer: bufferCount: %u\n", pDmaParams->bufferCount);
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
                     (dwNotifySize > SPCM4DRV_DMA_MAX_NWC_TRANSFER_LENGTH)) && 
                    (currentElementLength > SPCM4DRV_DMA_MAX_NWC_TRANSFER_LENGTH))
                    currentEntryLength = SPCM4DRV_DMA_MAX_NWC_TRANSFER_LENGTH;
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
                    pSGListCurrentEntry->nextEntryPhysLow = (pDmaParams->dmaSGListPhysicalAddress.LowPart +
                                                            (sizeof(NWCORE_SGLIST_ENTRY) * dwSGEntryNum));
#else
                    pSGListCurrentEntry->nextEntryPhysLow = qwGetPhysAddr (pNWCDmaParams, dwSGEntryNum);
#endif

                    // status und control initialisieren
                    pSGListCurrentEntry->dwStatus =  0;
                    pSGListCurrentEntry->dwControl = 0;
                    pSGListCurrentEntry->length2 =   0;


                    // Adresse als 64bit inkrementieren, damit der Übertrag von von Low nach High korrekt ist
                    qwPhysicalAddress = (((ULONG64)pWinSGList->Elements[i].Address.HighPart) << 32) | pWinSGList->Elements[i].Address.LowPart;
                    qwPhysicalAddress += lAlreadySplittedLen;

                    // ---------------------------------------------------------------------------------
                    // NotifySize=0 -> nur ein Interrupt ganz am Ende, wir nehmen die Blöcke wie sie kommen
                    if (dwNotifySize == 0)
                        {
                        pSGListCurrentEntry->pciAddrLow  =    LOW_DWORD (qwPhysicalAddress);
                        pSGListCurrentEntry->pciAddrHigh =   HIGH_DWORD (qwPhysicalAddress);
                        pSGListCurrentEntry->dwControl |=    (currentEntryLength & NWD_S2CDESC_BYTECOUNT_MASK);
// BT: Wird doch duch Core gesetzt?
                        pSGListCurrentEntry->dwStatus |=     (currentEntryLength & NWD_S2CDESC_BYTECOUNT_MASK);

                        // Wir zählen mit, wieviel von diesem Element bereits verarbeitet wurde
                        lAlreadySplittedLen += (pSGListCurrentEntry->dwControl & NWD_S2CDESC_BYTECOUNT_MASK);

                        currentEntryLength = 0;

                        // SW: Descr. umfaßt immer ganzes Paket - SOP und EOP werden gesetzt.
                        //pSGListCurrentEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_SOP | NWD_S2CDESC_CNTRL_FLAG_EOP;
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

                        // Die physische Adresse des aktuellen Blocks wird eingestellt.
                        pSGListCurrentEntry->pciAddrLow =     LOW_DWORD (qwPhysicalAddress);
                        pSGListCurrentEntry->pciAddrHigh =   HIGH_DWORD (qwPhysicalAddress);
                        pSGListCurrentEntry->dwControl |=    (dwNotifySize - dwBytesSinceLastInterrupt) & NWD_S2CDESC_BYTECOUNT_MASK;
                        pSGListCurrentEntry->dwStatus |=     (dwNotifySize - dwBytesSinceLastInterrupt) & NWD_S2CDESC_BYTECOUNT_MASK;

                        // Wir zählen mit, wieviel von diesem Element bereits verarbeitet wurde
                        lAlreadySplittedLen += (pSGListCurrentEntry->dwControl & NWD_S2CDESC_BYTECOUNT_MASK);
                        currentEntryLength -= (pSGListCurrentEntry->dwControl & NWD_S2CDESC_BYTECOUNT_MASK);

                        // Eine ganze NotifySize passt hier rein, daher kommt am Ende immer der Interrupt
                        pSGListCurrentEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;
                        dwBytesSinceLastInterrupt = 0;

                        // !!! SW 130626:
                        // Wir merken uns diese 'festen' IRQ-Flags auch im length2-Feld.
                        // Damit können wir sie von den zeitweilig beim EOC-Modus dazukommenden IRQ-Flags unterscheiden.
                        pSGListCurrentEntry->length2 |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;
                        }


                    // ---------------------------------------------------------------------------------
                    // Eine NotifySize geht über mehrere Elemente
                    else 
                        {
                        pSGListCurrentEntry->pciAddrLow  =  LOW_DWORD (qwPhysicalAddress);
                        pSGListCurrentEntry->pciAddrHigh = HIGH_DWORD (qwPhysicalAddress);
                        pSGListCurrentEntry->dwControl |= (currentEntryLength & NWD_S2CDESC_BYTECOUNT_MASK);
                        pSGListCurrentEntry->dwStatus |=  (currentEntryLength & NWD_S2CDESC_BYTECOUNT_MASK);
                        currentEntryLength = 0;

                        //!!! SW 130610:
                        // Wir zählen mit, wieviel von diesem Element bereits verarbeitet wurde
                        lAlreadySplittedLen += (pSGListCurrentEntry->dwControl & NWD_S2CDESC_BYTECOUNT_MASK);

                        // SW: Descr. umfaßt Paketende - EOP.
                        //pSGListCurrentEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_EOP;

                        // einen Interrupt alle NotifySize einbauen
                        dwBytesSinceLastInterrupt += (pSGListCurrentEntry->dwControl & NWD_S2CDESC_BYTECOUNT_MASK);
                        if (dwBytesSinceLastInterrupt >= dwNotifySize)
                            {
                            pSGListCurrentEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;
                            dwBytesSinceLastInterrupt = 0;

                            // !!! SW 130626:
                            // Wir merken und diese 'festen' IRQ-Flags auch im length2-Feld.
                            // Damit können wir sie von den zeitweilig beim EOC-Modus dazukommenden IRQ-Flags unterscheiden.
                            pSGListCurrentEntry->length2 |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;
                            }
                        }


                    // Anhand der Eintrags-Nummer erkennen wir beim Wiederherstellen der SG-Liste das Ende der Liste
                    // und können die Länge aus length2 wiederherstellen
                    pSGListCurrentEntry->length2 |=      (pSGListCurrentEntry->dwControl & NWD_S2CDESC_BYTECOUNT_MASK);
                    pSGListCurrentEntry->entryNumber =   dwSGEntryNum;
                    pSGListCurrentEntry->entryNumber2 =  dwSGEntryNum++;

                    // Den aktuellen Eintrag merken wir uns als möglichen letzten Eintrag,
                    // bevor wir den Zeiger auf den nächsten Eintrag stellen.
                    // OR: Hierbei beachten wir den verfügbaren Buffer
                    if (qwTransferSize >= (pSGListCurrentEntry->dwControl & NWD_S2CDESC_BYTECOUNT_MASK))
                        {
                        qwTransferSize -= (pSGListCurrentEntry->dwControl & NWD_S2CDESC_BYTECOUNT_MASK);
                        pSGListLastEntry = pSGListCurrentEntry;
                        }
                    //
                    // SW: ??? Diese Stelle hier muss unbedingt nochmal überprüft werden !!!
                    //
                    else if( qwTransferSize > 0 )
                        {
                        // if the remaining amount of data is less than the size of the entry, we keep the data for later
                        // when RefreshSGList() is called
                        // otherwise we would run into problems because DMA would start and transfer half of the entry, then
                        // proceed to the next entry, while the user program adds the remaining data to the first entry,
                        // and that new data in the second half of the entry would never be transfered, causing data loss
                        pSGListCurrentEntry->dwControl &= ~NWD_S2CDESC_BYTECOUNT_MASK;
                        pSGListCurrentEntry->dwStatus  &= ~NWD_S2CDESC_BYTECOUNT_MASK;
                        }

                    // if the page isn't activated the page needs to look like being transferred
                    else
                        {
                        // SW ??? pSGListCurrentEntry->dwControl |=  currentEntryLength & NWD_S2CDESC_BYTECOUNT_MASK;
                        pSGListCurrentEntry->dwControl &= ~NWD_S2CDESC_BYTECOUNT_MASK;
                        pSGListCurrentEntry->dwStatus  &= ~NWD_S2CDESC_BYTECOUNT_MASK;
                        pSGListCurrentEntry->entryNumber = 0;

                        // we have unused descriptor/s in the chain ... 
                        if( isTransferUsingWholeBuffer )
                            isTransferUsingWholeBuffer = FALSE;
                        }

#ifdef DMA_BOUNCE_BUFFER
                    // force sync of user buffer to bounce buffer
                    if (pDmaParams->writeToDevice)
                        {
                        dma_sync_single_for_device (&pContext->pstPCIDevice->dev, QWORD_FROM_DWORD(pSGListCurrentEntry->pciAddrHigh, pSGListCurrentEntry->pciAddrLow), pSGListCurrentEntry->dwStatus & NWD_S2CDESC_BYTECOUNT_MASK, DMA_TO_DEVICE);
                        }
#endif

#ifdef WINVER
                    pSGListLastPhysEntry = pSGListCurrentEntry++;
#else
                    pSGListLastPhysEntry = pSGListCurrentEntry;
                    pSGListCurrentEntry = pstGetNWCSGListEntry (pNWCDmaParams, dwSGEntryNum - 1);
#endif
                    }
                }
            }
        }

    // Wir merken und die Zahl der tatsächlich entstandenen Blöcke.
    // (j-1,da am Ende der Listen-Funktion j nochmal incrementiert wurde!)
    pDmaParams->dmaSGListElements = --dwSGEntryNum;

    // Beim Starten haben wir noch keine wieder verfügbar gemachten Einträge.
    pDmaParams->dmaSGListRefreshedElements = 0;

    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "Our SGList has %d elements", pDmaParams->dmaSGListElements );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "Our SGList has %d elements\n", pDmaParams->dmaSGListElements);

    // build descriptor pointer
    dmaDescrPtr  = pDmaParams->dmaSGListPhysicalAddress.LowPart;

    // SW: Der letzte PHYSISCHE Eintrag der ganzen Liste braucht keinen INTATEND-Eintrag,
    // sondern eher der letzte BENUTZTE Eintrag, oder ???

    // OR: der letzte Eintrag hat immer ein INTATEND Bit
    //pSGListLastPhysEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;
    pSGListLastEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;

    // !!! SW 130626:
    // Wir merken und diese 'festen' IRQ-Flags auch im length2-Feld.
    // Damit können wir sie von den zeitweilig beim EOC-Modus dazukommenden IRQ-Flags unterscheiden.
    pSGListLastEntry->length2 |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;

    if( isTransferUsingWholeBuffer )
        {
        TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "Modus: END_OF_CHAIN" );
        SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "Modus: END_OF_CHAIN\n" );

        // Wenn die ganze SG-Liste bei diesem Transfer genutzt wird, 
        // hängen wir als Ende-Kennung eine '0' ans Ende der Liste und
        // nuzten dann nicht das NWD_SW_DESC_PTR register zum Starten des DMA !!!

        pSGListLastPhysEntry->nextEntryPhysLow = 0;

        // !!! SW 130626: Wir merken uns den EOC-Eintrag, der das Listenende enthält.
        pNWCDmaParams->pNwcSGListCurrentEOCEntry = pSGListLastPhysEntry;
        }
    else
        {
        // Wenn bei Start des Transfers nur ein Teil der SG-Liste genutzt wird, 
        // schließen wir die Kette der SGListen-Descriptoren, indem der letzte physische Eintrag 
        // auf den Listen/Anfang zeigt und 
        // nuzten dann das NWD_SW_DESC_PTR register zum Starten des DMA !!!

        TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "Modus: POINTER_IN_REGS" );
        SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "Modus: POINTER_IN_REGS\n" );

        pSGListLastPhysEntry->nextEntryPhysLow = dmaDescrPtr;

        // !!! SW 130626: Wir haben kein EOC-Eintrag.
        pNWCDmaParams->pNwcSGListCurrentEOCEntry = NULL;
        }

    // if user did not provide enough data to pass a notify size border, we delay the DMA start until more data has been added in RefreshSGList()
    pNWCDmaParams->bDelayDMAStart = isTransferUsingWholeBuffer && pDmaParams->writeToDevice && (qwTransferSize == qwInitialAvail);
    if (pNWCDmaParams->bDelayDMAStart)
        {
        SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "Delaying DMA start (TransferSize %llu == InitialAvail %llu)\n", qwTransferSize, qwInitialAvail);
        }

    //Für C2S nicht nutzbar, solange Descriptor-Felder für PLX-NAchbildung verwendet werden.
    //  pSGListLastPhysEntry->dwControl |=       NWD_S2CDESC_CNTRL_FLAG_EOP;

    // save pointer to current first and last entries, here we begin later to refresh the sgList
    pNWCDmaParams->pNwcSGListCurrentFirstTestEntry = pNWCDmaParams->pNwcSGListStartEntry;
    pNWCDmaParams->pNwcSGListCurrentLastEntry =      pSGListLastEntry;

    // the current first entry is the one behind the last entry
    if ((pSGListLastEntry->entryNumber == pDmaParams->dmaSGListElements) || pNWCDmaParams->bDelayDMAStart)
        pNWCDmaParams->pNwcSGListCurrentFirstEntry = pNWCDmaParams->pNwcSGListStartEntry;
    else
#   ifdef WINVER
        pNWCDmaParams->pNwcSGListCurrentFirstEntry = pSGListLastEntry + 1;
#   else
        pNWCDmaParams->pNwcSGListCurrentFirstEntry = pstGetNWCSGListEntry (pNWCDmaParams, pSGListLastEntry->entryNumber2); // no +1 (entryNumber2 is one-based)
#   endif

    pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry = NULL;
    //pNWCDmaParams->pNwcSGListCurrentEOCEntry = NULL;
    pNWCDmaParams->pNwcSGListCurrentLastRegModeEntry = NULL;

    // SW: Brauchen wir das mit der Busbreite noch ???

    // Wenn die Busbreite BUS_WIDTH_16_H ist, schalten wir den BYTE_LANE_MODE im
    // Big/Little Endian Descriptor um (Bits 16:31).
    // Ins MOD-Register schreiben wir aber trotzdem den Wert BUS_WIDTH_16_L (1),
    // um die Busbreite 16 eizustellen.
    if (pDmaParams->busWidth == BUS_WIDTH_16_H)
        {
        busWidth = BUS_WIDTH_16_L;
        }

    // OR: entweder 32 bit oder 16 bit und den Low Anteil. Hier müssen wir auf jeden Fall den
    // Bitlane Modus wieder zurückstellen sonst lesen wir immer nur den oberen teil bei 16 Bit
    else
        {
        //NoHW        PLX_WriteByOffset ((PCHAR)pDevExt->plxMappedAddr, PLX_DMABIGE, 0 );
        busWidth = pDmaParams->busWidth;
        }


    // start dma operation
    // OR: in case of write the transferSize is the initial number of bytes that has been put into the buffer, 
    // the rest is "transferred"
    if (pDmaParams->writeToDevice)
        {
        if (pNWCDmaParams->bDelayDMAStart)
            pDmaParams->qwBytesTransfered = pDmaParams->qwBufferSize - (qwInitialAvail - qwTransferSize);
        else
            pDmaParams->qwBytesTransfered = pDmaParams->qwBufferSize - qwInitialAvail;
        pDmaParams->qwBytesAlreadyFree = qwTransferSize;       // SW 130726
        }
    else
        {
        pDmaParams->qwBytesTransfered =   0;
        pDmaParams->qwBytesAlreadyFree =  0;                  // SW 130726
        }

    //_printSGList = TRUE;
    NwcSGListDump( pContext->dwBoardNumber, pDmaParams );
    //_printSGList = FALSE;


    pDmaParams->active = TRUE;

    // reset compl_desc-ptr
    NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_COMPLETED_DESC_PTR, 0);

    // set hw desc ptr (fill list into nwc)
    NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR, dmaDescrPtr);

    if( isTransferUsingWholeBuffer )
        {
        // set sw desc ptr 0 - don't use it 
        NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR, 0);
        }
    else
        {
        // init sw desc ptr 
        NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR, dmaDescrPtr);
        }

#ifdef DEBUG_RESTART
    SPCM4DRV_DebugPrintInfo( pContext, "Start (elements:%d)\n", pDmaParams->dmaSGListElements );
    SPCM4DRV_DebugPrintInfo( pContext, "firstDescAddr:%8x, lastDesc:%8x\n", 
                             pDmaParams->dmaSGListPhysicalAddress.LowPart,
                             pDmaParams->dmaSGListPhysicalAddress.LowPart + 
                             sizeof(NWCORE_SGLIST_ENTRY) * (pDmaParams->dmaSGListElements-1) );
#endif // DEBUG_RESTART

    // DMA engine enable
    // Wenn das NWD_SW_DESC_PTR register deaktiviert ist (0), wird DMA hierdurch gestartet
    if (!pNWCDmaParams->bDelayDMAStart)
        {
        INTERRUPT_LOCK(pContext); // SW 130726
        dwReg = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
        dwReg |= NWD_ENGCNTRL_DMA_EN;
        NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, dwReg);
        INTERRUPT_UNLOCK(pContext); // SW 130726
        }

    if( !isTransferUsingWholeBuffer )
        {
        // SW 130724: Wir fügen an dieser Stelle ein temporäres IRQ-Flag ein!
        pSGListLastEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;

        // SW 130724: Wir merken uns den aktuell letzten Eintrag im Reg-Modus.
        pNWCDmaParams->pNwcSGListCurrentLastRegModeEntry = pSGListLastEntry;

        // start DMA by set NWD_SW_DESC_PTR register
        NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR, pSGListLastEntry->nextEntryPhysLow);
        }

    return 1;
    }


//------ SPCM4DRV_StopTransfer ------
#ifdef WINVER
VOID SPCM4DRV_NWC_StopTransfer (IN PSPCM4DRV_DEVICE_CONTEXT pContext, PNWC_DMA_PARAMS pNWCDmaParams)
#else
void SPCM4DRV_NWC_StopTransfer (SPCM_ST_CARDINFO* pContext, NWC_DMA_PARAMS* pNWCDmaParams)
#endif
{
    COMMON_DMA_PARAMS* pDmaParams = &pNWCDmaParams->stCommon;
#ifdef WINVER
    PUCHAR pBar0Mem = pContext->memMappedAddress[0];
#else
    uint32* pBar0Mem = pContext->apdwMemMappedAddress[0];
    unsigned long dwLocalIRQLockFlags;
#endif
    PNWCORE_SGLIST_ENTRY pHwNextSGListtEntry;
    PNWCORE_SGLIST_ENTRY pSGListCurrentEntry;
     
    UINT32 dwReg;
    ULONG loops, i;
    ULONG reg_hw_next, reg_sw_desc;

    // OR: Sicherung eingebaut
    DMA_LOCK(pDmaParams);

    //_printSGList = TRUE;
    //NwcSGListDump( pContext->dwBoardNumber, pDmaParams );
    //_printSGList = FALSE;

    // clear sglist entries
    pSGListCurrentEntry  = pNWCDmaParams->pNwcSGListStartEntry;
    for( i = 0; i < pDmaParams->dmaSGListElements; ++i )
        {
        pSGListCurrentEntry->nextEntryPhysLow = 0;
        pSGListCurrentEntry->dwControl = 0;
#   ifdef WINVER
        pSGListCurrentEntry++;
#   else
        pSGListCurrentEntry = pstGetNWCSGListEntry (pNWCDmaParams, i + 1);
#   endif
        }
    // Control register lesen
    dwReg = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);

    // listen pointer register auslesen
    reg_hw_next = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR);
    reg_sw_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR);

    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "NWC_StopTransfer > dwEngAddrOffs:0x%x, dwReg:0x%x, hw_next:0x%x, sw_desc:0x%x", 
               pDmaParams->dwEngAddrOffs, dwReg, reg_hw_next, reg_sw_desc );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, 
                         "NWC_SPCM4DRV_StopTransfer > dwEngAddrOffs:0x%x, dwReg:0x%x, hw_next:0x%x, sw_desc:0x%x\n", 
                         pDmaParams->dwEngAddrOffs, dwReg, reg_hw_next, reg_sw_desc );

    // ENABLE flag löschen
    //if( (dwReg & NWD_ENGCNTRL_DMA_EN) == NWD_ENGCNTRL_DMA_EN)
        {
        dwReg &= ~NWD_ENGCNTRL_DMA_EN;
        dwReg |= NWD_ENGCNTRL_DMA_RESET_REQUEST; // SW: 120226 !!! 
        INTERRUPT_LOCK(pContext); // SW 130726
        NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, dwReg);
        INTERRUPT_UNLOCK(pContext); // SW 130726
        }

    // 10 µs warten
    USLEEP(10);

    // control register erneut auslesen
    dwReg = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);

    // auf RUNNING flag prüfen
    if( (dwReg & NWD_ENGCNTRL_DMA_RUNNING) == NWD_ENGCNTRL_DMA_RUNNING)
        {
        // Warten, ob letzter aktiver DMA-Descriptor abgearbeitet wird 
        loops = 0;
        do 
            {
            // 10 µs warten
            USLEEP(10);
            dwReg = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
        
            } while ( ((dwReg & NWD_ENGCNTRL_DMA_RUNNING) == NWD_ENGCNTRL_DMA_RUNNING) &&
                  (loops++ < 1000) );     // max. 10 ms

        if( loops > 100 )
            SPCM4DRV_DebugPrintList (WARNING, 1, "Stop0: DMA was RUNNING (loops:%d)!\n", loops);

        // Wenn RUNNING flag jetzt gelöscht ist, sind wir hier fertig
        if( (dwReg & NWD_ENGCNTRL_DMA_RUNNING) != NWD_ENGCNTRL_DMA_RUNNING )
            {
            pDmaParams->active = FALSE;

            DMA_UNLOCK(pDmaParams);
            return;

            //goto StopTransfer_DoReset;  // SW 130226
            }

        SPCM4DRV_DebugPrintList (WARNING, 1, "Stop1: DMA is still RUNNING!\n");

        // Wir prüfen die beiden listen pointer register

        // read hw_next register
        reg_hw_next = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR);
        // read sw_desc register
        reg_sw_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR);

        // Wenn der Inhalt beider Register gleich ist und das WAITING flag gesetzt, 
        // sind wir im REGISTER-Modus und die DMA-Engine wartet. 
        if( (reg_hw_next != 0) && 
            (reg_sw_desc == reg_hw_next) &&
            ((dwReg & NWD_ENGCNTRL_DMA_WAITING) == NWD_ENGCNTRL_DMA_WAITING ))
            {
            ULONG nextEntryPhysLow, numOfEntries;
#ifdef WINVER
            ULONG entryDiffToStart;
#endif

            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "NWC_StopTransfer - DMA_WAITING!" );
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "NWC_SPCM4DRV_StopTransfer - DMA_WAITING!\n" );

            // Wir versuchen, ein ENDOFCHAIN zu erzwingen und damit die DMA-Engine anzuhalten.

            // Im aktuellen SGList-Eintrag setzten wir die byteCount-Werte 0.
            // Außerdem setzen wir die Adresse des nächsten Eintrags auf 0 (End of chain).

            // !!! Möglicherweise muss für das Lostreten der DMA-Engine das ENABLE-Flag nochmal gesetzt werden !!!

            // get phys address of current next sglist entry
            nextEntryPhysLow = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR);
#ifdef WINVER
            entryDiffToStart = nextEntryPhysLow - pDmaParams->dmaSGListPhysicalAddress.LowPart;
            numOfEntries     = entryDiffToStart / sizeof(NWCORE_SGLIST_ENTRY);
            pHwNextSGListtEntry = (PNWCORE_SGLIST_ENTRY)pDmaParams->dmaSGListVirtualAddress;
            pHwNextSGListtEntry += numOfEntries;
#else
            // for each page with NWCORE_SGLIST_ENTRYs we check the physical start address to determine if nextEntryPhys is in this page
            // the array of pointers (ppstSGListPages) is contiguous, but the pages they point may not!
            numOfEntries = 0;
            while ((nextEntryPhysLow  < qwGetPhysAddr (pNWCDmaParams, numOfEntries))
                || (nextEntryPhysLow >= qwGetPhysAddr (pNWCDmaParams, numOfEntries) + PAGE_SIZE))
                numOfEntries += PAGE_SIZE / sizeof (NWCORE_SGLIST_ENTRY); // 128

            // if next physical address is in this page, we determine offset in page
            numOfEntries += (nextEntryPhysLow - qwGetPhysAddr (pNWCDmaParams, numOfEntries)) / sizeof (NWCORE_SGLIST_ENTRY);

            pHwNextSGListtEntry = pstGetNWCSGListEntry (pNWCDmaParams, numOfEntries);
#endif

            // set byteCount to 0
            pHwNextSGListtEntry->dwControl &= ~NWD_S2CDESC_BYTECOUNT_MASK;
            pHwNextSGListtEntry->dwStatus &= ~NWD_S2CDESC_BYTECOUNT_MASK;

            // set chain end in next sglist entry
            pHwNextSGListtEntry->nextEntryPhysLow = 0;

            // clear sw desc ptr 
            NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR, 0);

            loops = 0;
            do 
                {
                // 10 µs warten
                USLEEP(10);

                // control register erneut auslesen
                dwReg = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);

                } while ( ((dwReg & NWD_ENGCNTRL_DMA_WAITING) == NWD_ENGCNTRL_DMA_WAITING) &&
                      (loops++ < 1000) );     // max. 10 ms
            }

        if( loops < 1000 )
            SPCM4DRV_DebugPrintList (WARNING, 1, "Stop2: DMA was stopped! (loops:%d)\n", loops);
        else
            SPCM4DRV_DebugPrintList (WARNING, 1, "Stop2: DMA is still RUNNING_2! (loops:%d)\n", loops);

        // control register erneut auslesen
        dwReg = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);

        // Wenn RUNNING flag jetzt gelöscht ist, sind wir hier fertig
        if( (dwReg & NWD_ENGCNTRL_DMA_RUNNING) != NWD_ENGCNTRL_DMA_RUNNING )
            {
            pDmaParams->active = FALSE;

            DMA_UNLOCK(pDmaParams);
            return;

            //goto StopTransfer_DoReset;  // SW 130226
            }

        SPCM4DRV_DebugPrintList (WARNING, 1, "Stop3: trying RESET\n");

//StopTransfer_DoReset:

        // Wir senden ein Reset_Request an die User Logic, 
        // damit diese auf das gleich folgende DMA-Reset vorbereitet ist.
        dwReg |= NWD_ENGCNTRL_DMA_RESET_REQUEST;
        INTERRUPT_LOCK(pContext); // SW 130726
        NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, dwReg);
        INTERRUPT_UNLOCK(pContext); // SW 130726

        TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "StopTransfer - DMA_RESET!" );
        SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "SPCM4DRV_StopTransfer - DMA_RESET!\n" );

        // 50 µs warten
        USLEEP(50);

        // Wir führen jetzt ein DMA_Reset aus. 
        dwReg |= NWD_ENGCNTRL_DMA_RESET;
        INTERRUPT_LOCK(pContext); // SW 130726
        NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, dwReg);
        INTERRUPT_UNLOCK(pContext); // SW 130726

        // Nach dem Ende des Resets wird das RESET flag gelöscht, darauf warten wir.
        loops = 0;
        do 
            {
            // 10 µs warten
            USLEEP(10);

            // control register erneut auslesen
            dwReg = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);

            } while ( ((dwReg & NWD_ENGCNTRL_DMA_RESET) == NWD_ENGCNTRL_DMA_RESET) &&
                  (loops++ < 100000) );   // max 1 sec warten

        if( loops > 100 )
            SPCM4DRV_DebugPrintList (WARNING, 1, "Stop4: RESET tried - dwReg:0x%x, loops:%d!\n", dwReg, loops );
        }

    // control register erneut auslesen und nochmal ohne ENABLE flag schreiben.
    // Damit werden weitere Flags (SW_Abort_Error, Waiting_Persist, ..) zurückgesetzt.
    dwReg = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
    dwReg &= ~NWD_ENGCNTRL_DMA_EN;
    INTERRUPT_LOCK(pContext); // SW 130726
    NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, dwReg);
    INTERRUPT_UNLOCK(pContext); // SW 130726

    // control register und listen pointer register erneut auslesen
    dwReg = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
    reg_hw_next = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR);
    reg_sw_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR);

    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "NWC_StopTransfer < dwReg:0x%x, hw_next:0x%x, sw_desc:0x%x", 
               dwReg, reg_hw_next, reg_sw_desc );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, 
                             "SPCM4DRV_NWC_StopTransfer < dwReg:0x%x, hw_next:0x%x, sw_desc:0x%x\n", 
                         dwReg, reg_hw_next, reg_sw_desc );
    
    pDmaParams->active = FALSE;

    // OR: Sicherung eingebaut
    DMA_UNLOCK(pDmaParams);
    }

//------ SPCM4DRV_ClearData ------
#ifdef WINVER
VOID SPCM4DRV_NWC_ClearData (IN PSPCM4DRV_DEVICE_CONTEXT pContext, PNWC_DMA_PARAMS pNWCDmaParams)
#else
void SPCM4DRV_NWC_ClearData (SPCM_ST_CARDINFO* pContext, NWC_DMA_PARAMS* pNWCDmaParams)
#endif
    {
    ULONG i;
    COMMON_DMA_PARAMS* pDmaParams = &pNWCDmaParams->stCommon;
    PNWCORE_SGLIST_ENTRY pSGListCurrentEntry;
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
#       else // Intel/AMD or Clara with dGPU
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
                    uint64 qwPhysDMAAddress = (((uint64)pWinSGList->Elements[dwElementIdx].Address.HighPart) << 32) | pWinSGList->Elements[dwElementIdx].Address.LowPart;
                    struct page* pstUserPage = (struct page*)pWinSGList->Elements[dwElementIdx].pvUserPageOrPageList;
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
    pSGListCurrentEntry  = pNWCDmaParams->pNwcSGListStartEntry;
    for( i = 0; i < pDmaParams->dmaSGListElements; ++i )
        {
        pSGListCurrentEntry->nextEntryPhysLow = 0;
        pSGListCurrentEntry->dwControl = 0;
#   ifdef WINVER
        pSGListCurrentEntry++;
#   else
        pSGListCurrentEntry = pstGetNWCSGListEntry (pNWCDmaParams, i + 1);
#   endif
        }

    DMA_UNLOCK(pDmaParams);
    }

//------ SPCM4DRV_RefreshSGList ------
#ifdef WINVER
VOID SPCM4DRV_NWC_RefreshSGList (IN OUT PSPCM4DRV_DEVICE_CONTEXT pContext, IN PNWC_DMA_PARAMS pNWCDmaParams, IN ULONG64 qwNumberOfBytes, BOOLEAN bFlushSGList)
#else
void SPCM4DRV_NWC_RefreshSGList (SPCM_ST_CARDINFO* pContext, PNWC_DMA_PARAMS pNWCDmaParams, uint64 qwNumberOfBytes, bool bFlushSGList)
#endif
    {
    COMMON_DMA_PARAMS* pDmaParams = &pNWCDmaParams->stCommon;
    PNWCORE_SGLIST_ENTRY pSGListCurrentFirstEntry;
    PNWCORE_SGLIST_ENTRY pSGListCurrentLastEntry;
    PNWCORE_SGLIST_ENTRY pSGListNewLastEntry;
    PNWCORE_SGLIST_ENTRY pSGListFirstRefreshedEntry;
    ULONG64 qwBytesToShift;
    ULONG64 qwInitialBytesToShift;


#ifdef DEBUG_RESTART
#   ifdef WINVER
    PUCHAR pBar0Mem = pContext->memMappedAddress[0];
#   else
    uint32* pBar0Mem = pContext->apdwMemMappedAddress[0];
#   endif
    volatile UINT32 engCntl, reg_next_desc, reg_sw_desc, reg_compl_desc;
#endif

    // OR: komplette Funktion mit Spinlock gekapselt
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
                            pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2, 
                            pNWCDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2,
                            pNWCDmaParams->pNwcSGListCurrentFirstTestEntry->entryNumber2,
                            (pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry == NULL) ? 0 : pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry->entryNumber2);
#endif // DEBUG_RESTART

    pSGListCurrentFirstEntry =   pNWCDmaParams->pNwcSGListCurrentFirstEntry;
    pSGListCurrentLastEntry =    pNWCDmaParams->pNwcSGListCurrentLastEntry;
    pSGListNewLastEntry =        pSGListCurrentLastEntry;

    pSGListFirstRefreshedEntry = pSGListCurrentFirstEntry;

    qwBytesToShift = qwNumberOfBytes + pDmaParams->qwBytesAlreadyFree;
    qwInitialBytesToShift = qwBytesToShift;
    // SW 130726 bytesToShift = numberOfBytes;

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_DMA, "RefreshSGList > numOfBytes=%llu", qwNumberOfBytes );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "RefreshSGList > numOfBytes=%llu AlreadyFree=%llu\n", qwNumberOfBytes, pDmaParams->qwBytesAlreadyFree);

    //_printSGList = TRUE;
    NwcSGListDump( pContext->dwBoardNumber, pDmaParams );
    //_printSGList = FALSE;

    // Solange was zu schieben ist ... 
    // OR: und der zu schiebende Bereich nicht kleiner als der nächste Eintrag ist

    // !!! SW 130626:
    //while (qwBytesToShift >= pSGListCurrentFirstEntry->length2)
    while (qwBytesToShift >= (pSGListCurrentFirstEntry->length2 & NWD_S2CDESC_BYTECOUNT_MASK))
        {

        // ... wird die Blocklänge wieder eingetragen ...
        pSGListCurrentFirstEntry->dwControl |=  (pSGListCurrentFirstEntry->length2 & NWD_S2CDESC_BYTECOUNT_MASK);
        pSGListCurrentFirstEntry->dwStatus  |=  (pSGListCurrentFirstEntry->length2 & NWD_S2CDESC_BYTECOUNT_MASK);

        // ... und die Entrynummer wieder gesetzt
        pSGListCurrentFirstEntry->entryNumber = pSGListCurrentFirstEntry->entryNumber2;

        // ... Die Blocklänge wird von der Zahl verarbeiteten Daten abgezogen.

        // !!! SW 130626:
        //qwBytesToShift -= pSGListCurrentFirstEntry->length2;
        qwBytesToShift -= (pSGListCurrentFirstEntry->length2 & NWD_S2CDESC_BYTECOUNT_MASK);

#ifdef DMA_BOUNCE_BUFFER
        // force sync of user buffer to bounce buffer
        if (pDmaParams->writeToDevice)
            {
            dma_sync_single_for_device (&pContext->pstPCIDevice->dev, QWORD_FROM_DWORD(pSGListCurrentFirstEntry->pciAddrHigh, pSGListCurrentFirstEntry->pciAddrLow), pSGListCurrentFirstEntry->dwStatus & NWD_S2CDESC_BYTECOUNT_MASK, DMA_TO_DEVICE);
            }
#endif

        // Wenn alle verarbeiteten Daten wieder verfügbar sind, dann ist der eben
        // wieder verwendbar gemachte Block nun der neue letzte Block.
        pSGListNewLastEntry = pSGListCurrentFirstEntry;

        // Der Zeiger wird auf nächsten Block gesetzt.
        if (pSGListCurrentFirstEntry->entryNumber == pDmaParams->dmaSGListElements)
            pSGListCurrentFirstEntry = pNWCDmaParams->pNwcSGListStartEntry;
        else
#ifdef WINVER
            pSGListCurrentFirstEntry++;
#else
            pSGListCurrentFirstEntry = pstGetNWCSGListEntry (pNWCDmaParams, pSGListCurrentFirstEntry->entryNumber2); // no +1 because entryNumber2 is one-based
#endif

        // Wir zählen die wieder verfügbaren Blöcke mit.
        pDmaParams->dmaSGListRefreshedElements++;
        }

    if (bFlushSGList && qwBytesToShift != 0)
        {
        SPCM4DRV_DebugPrint(TRACE, pContext->dwBoardNumber, "Flush start - BytesToShift: %llu\n", qwBytesToShift);

        // ... wird die Blocklänge wieder eingetragen
        // nur die verbleibende Datenmenge, nicht die Länge des Eintrags
        pSGListCurrentFirstEntry->dwControl &= ~NWD_S2CDESC_BYTECOUNT_MASK;
        pSGListCurrentFirstEntry->dwControl |=  (qwBytesToShift & NWD_S2CDESC_BYTECOUNT_MASK);
        pSGListCurrentFirstEntry->dwStatus &= ~NWD_S2CDESC_BYTECOUNT_MASK;
        pSGListCurrentFirstEntry->dwStatus  |=  (qwBytesToShift & NWD_S2CDESC_BYTECOUNT_MASK);

        // ... und die Entrynummer wieder gesetzt
        pSGListCurrentFirstEntry->entryNumber = pSGListCurrentFirstEntry->entryNumber2;

        // ... Die Blocklänge wird von der Zahl verarbeiteten Daten abgezogen.
        qwBytesToShift -= (qwBytesToShift & NWD_S2CDESC_BYTECOUNT_MASK);


        // Wenn alle verarbeiteten Daten wieder verfügbar sind, dann ist der eben
        // wieder verwendbar gemachte Block nun der neue letzte Block.
        pSGListNewLastEntry = pSGListCurrentFirstEntry;

        // Der Zeiger wird auf nächsten Block gesetzt.
        if (pSGListCurrentFirstEntry->entryNumber == pDmaParams->dmaSGListElements)
            pSGListCurrentFirstEntry = pNWCDmaParams->pNwcSGListStartEntry;
        else
#ifdef WINVER
            pSGListCurrentFirstEntry++;
#else
            pSGListCurrentFirstEntry = pstGetNWCSGListEntry (pNWCDmaParams, pSGListCurrentFirstEntry->entryNumber2); // no +1 because entryNumber2 is one-based
#endif

        // Wir zählen die wieder verfügbaren Blöcke mit.
        pDmaParams->dmaSGListRefreshedElements++;
        }

    // Die Zahl der verarbeiteten Bytes wird von 'qwBytesTransfered' abgezogen. 
    // OR: dabei lassen wir die nicht mehr geschobenen Bytes stehen fürs nächste Mal
    pDmaParams->qwBytesTransfered -=  (qwNumberOfBytes - qwBytesToShift + pDmaParams->qwBytesAlreadyFree);
    // SW 130726 pDmaParams->qwBytesTransfered -= numberOfBytes;
    pDmaParams->qwBytesAlreadyFree = qwBytesToShift;

    // Die neuen Zeiger auf den ersten und letzten Eintrag der SG-Liste werden gesichert.
    pNWCDmaParams->pNwcSGListCurrentFirstEntry = pSGListCurrentFirstEntry;
    pNWCDmaParams->pNwcSGListCurrentLastEntry = pSGListNewLastEntry;

    // Wenn nötig wird auch der Zeiger auf den ersten aufgefüllten Eintrag gesichert.
    if( (pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry == NULL) &&
        (qwNumberOfBytes != 0 || bFlushSGList) )
        pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry = pSGListFirstRefreshedEntry;

    // !!! SW 130611: Die Liste enthält schon die IRQ-Flags - wir müssen hier keine weiteren hinzufügen!
    //pNWCDmaParams->pNwcSGListCurrentLastEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;

    TraceEvent(TRACE_LEVEL_VERBOSE, DBG_DMA, "NWC_RefreshSGList < transferred:%llu", 
               pDmaParams->qwBytesTransfered );
    SPCM4DRV_DebugPrint(TRACE, pContext->dwBoardNumber, 
                        "NWC_RefreshSGList < byteTransferred:%llu\n", 
                        pDmaParams->qwBytesTransfered);

    // _printSGList = TRUE;
    // NwcSGListDump( pContext->dwBoardNumber, pDmaParams );
    // _printSGList = FALSE;

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


    if (pNWCDmaParams->bDelayDMAStart)
        {
        // start DMA only when this refresh has at least filled one SG-entry
        if (qwBytesToShift != qwInitialBytesToShift)
            {
            UINT32 dwReg = 0;
#ifdef WINVER
            PCHAR pBar0Mem = (PCHAR)pContext->memMappedAddress[0];
#else
            uint32* pBar0Mem = pContext->apdwMemMappedAddress[0];
            unsigned long dwLocalIRQLockFlags;
#endif
            SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "Delayed start of DMA\n");

            // DMA engine enable
            // Wenn das NWD_SW_DESC_PTR register deaktiviert ist (0), wird DMA hierdurch gestartet
            INTERRUPT_LOCK(pContext);
            dwReg = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
            dwReg |= NWD_ENGCNTRL_DMA_EN;
            NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, dwReg);
            INTERRUPT_UNLOCK(pContext);

            pNWCDmaParams->bDelayDMAStart = FALSE;
            }
        else
            {
            SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "Delaying start of DMA further\n");
            }
        }
    else
        {
        // Falls DMA inzwischen fertig ist, müsssen wir es neu anstoßen!
        SPCM4DRV_NWC_DMA_Restart (pContext, pNWCDmaParams, FALSE);
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
VOID SPCM4DRV_NWC_DMA_Restart (IN OUT PSPCM4DRV_DEVICE_CONTEXT pContext, IN PNWC_DMA_PARAMS pNWCDmaParams, BOOLEAN fromDPC)
#else
void SPCM4DRV_NWC_DMA_Restart (SPCM_ST_CARDINFO* pContext, PNWC_DMA_PARAMS pNWCDmaParams, bool fromDPC)
#endif
    {
    COMMON_DMA_PARAMS* pDmaParams = &pNWCDmaParams->stCommon;
    volatile UINT32 engCntl, reg_next_desc, reg_sw_desc, reg_compl_desc;
#ifdef WINVER
    PUCHAR pBar0Mem = pContext->memMappedAddress[0];
#else
    uint32* pBar0Mem = pContext->apdwMemMappedAddress[0];
    unsigned long dwLocalIRQLockFlags;
#endif
    //ULONG loops = 0;

    volatile UINT32 engCntl2, engCntl3, engCntl4;

#ifdef DEBUG_RESTART
    CHAR pR = (fromDPC) ? 'S' : 's';

    if( !fromDPC && _DebugRestart && (pNWCDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2 != 1))
        DbgPrint("0:%d\n", pNWCDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2);
#endif // DEBUG_RESTART
    
    if( !pDmaParams->active )
        {
        SPCM4DRV_DebugPrint (ERROR, pContext->dwBoardNumber, "DMA_Restart: DMA not active!\n");
#ifdef WINVER
        //DbgBreakPoint();
#endif
        return;
        }

    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "SPCM4DRV_DMA_Restart (fromDPC: %c)\n", fromDPC? '1' : '0');

    // read hw_next register
    reg_next_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR);
    // read sw_desc register
    reg_sw_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR);
    // read compl_desc register
    reg_compl_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_COMPLETED_DESC_PTR);
    // read ENGCNTRL register
    engCntl4 = engCntl = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);

    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "DMA_Restart > engCntl:0x%x, reg_next_desc:0x%x, reg_sw_desc:0x%x, reg_compl_desc:0x%x", 
        engCntl, reg_next_desc, reg_sw_desc, reg_compl_desc );
    SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "DMA_Restart > engCntl:0x%x, reg_next_desc:0x%x, reg_sw_desc:0x%x, reg_compl_desc:0x%x, CurrentLastEntry:0x%x\n", 
        engCntl, reg_next_desc, reg_sw_desc, reg_compl_desc, pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2 );

#ifdef DEBUG_RESTART
    //if (!fromDPC )
        {
        SPCM4DRV_DebugPrintInfo( pContext, "%cA cr:%4x pl:%8x nx:%8x sw:%8x | l:%4d f:%4d t:%4d r:%4d | ", 
                                pR, engCntl, reg_compl_desc, reg_next_desc, reg_sw_desc, 
                                pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2, 
                                pNWCDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2,
                                pNWCDmaParams->pNwcSGListCurrentFirstTestEntry->entryNumber2,
                                (pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry == NULL) ? 0 : pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry->entryNumber2);
        }
#endif // DEBUG_RESTART

//Check_Restart: 

    if( (engCntl & NWD_ENGCNTRL_DMA_RUNNING) == NWD_ENGCNTRL_DMA_RUNNING )          // still running ?
        {
        // Im Register-Modus muss das ENABLE-Bit immmer gesetzt sein!
        // Wir prüfen das hier ...
        
        if( (pNWCDmaParams->pNwcSGListCurrentEOCEntry == NULL) && 
            ((engCntl & NWD_ENGCNTRL_DMA_EN) != NWD_ENGCNTRL_DMA_EN) )
            {
            engCntl &= NWD_ENGCNTRL_INTEN; // SW 130726
            engCntl |= NWD_ENGCNTRL_DMA_EN;
            INTERRUPT_LOCK(pContext); // SW 130726
            NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, engCntl);
            INTERRUPT_UNLOCK(pContext); // SW 130726

#ifdef WINVER
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "!!! Restart: ENABLE Bit nochmal gesetzt - 2 (0x%08x) !!!\n", engCntl & ~NWD_ENGCNTRL_DMA_EN);
#endif

            engCntl = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
#ifdef WINVER
            if( (engCntl & NWD_ENGCNTRL_DMA_EN) != NWD_ENGCNTRL_DMA_EN )
                DbgBreakPoint();
#endif
            }


        if( (engCntl & NWD_ENGCNTRL_DMA_WAITING) != NWD_ENGCNTRL_DMA_WAITING )      // waiting ?
            {
            // DMA ist aktiv und läuft noch!

            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, 
                "DMA_Restart: DMA ist noch aktiv! engCntl:0x%x, reg_next_desc:0x%x, reg_sw_desc:0x%x, reg_compl_desc:0x%x\n", 
                engCntl, reg_next_desc, reg_sw_desc, reg_compl_desc );

            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "DMA_Restart: DMA läuft!" );

#ifdef DEBUG_RESTART
            SPCM4DRV_DebugPrintInfo( pContext, "%c1 ",pR );
#endif // DEBUG_RESTART
            // !!! SW 130612: 
            // Auch wenn DMA noch läuft verschieben wir auch in diesem Fall das aktuelle Ende der DMA-Liste, wenn das möglich ist!
            // Anderenfalls kann die DMA-Engine stehen bleiben (s. log130612-07.txt)!

            // Wir machen nix und warten auf das Ende der aktuellen DMA 
            // return;
            }
        else
            {
            // DMA ist aktiv und wartet !

            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "DMA_Restart: DMA wartet!" );

#ifdef DEBUG_RESTART
            SPCM4DRV_DebugPrintInfo( pContext, "%c2(%d) ", pR, pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2 );
#endif // DEBUG_RESTART
            }

        // Wenn wir im EOC-Modus sind ändern wir nichts, sondern warten auf dessen Ende.
        if(pNWCDmaParams->pNwcSGListCurrentEOCEntry != NULL)
            {
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "DMA_Restart: EOC-Modus - wir warten auf das Ende\n" );

            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "DMA_Restart < EOC-Modus - wir warten auf das Ende!" );

            return;
            }


        // Wenn die Liste nicht aufgefüllt wurde, kann das Listenende auch nicht verschoben werden.
        // Dann sind wir hier fertig.
        if( pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry == NULL)
            {
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, 
                "DMA_Restart: DMA wartet - kein Neustart nötig! engCntl:0x%x, reg_next_desc:0x%x, reg_sw_desc:0x%x, reg_compl_desc:0x%x\n", 
                engCntl, reg_next_desc, reg_sw_desc, reg_compl_desc );
            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "DMA_Restart < Kein Speicher - Listenende wird nicht verschoben!" );

#ifdef DEBUG_RESTART
            SPCM4DRV_DebugPrintInfo( pContext, "%c3 ",pR );
#endif // DEBUG_RESTART

            return;
            }

        // Adresse des ersten und die Anzahl der wieder verfügbaren Einträge werden zurückgesetzt.
        pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry = NULL;
        pDmaParams->dmaSGListRefreshedElements = 0;

        // Zum Neustart der DMA-Engine bzw. zum Verschieben des Listenendes
        // müssen wir nur das SW_DESC_PTR-Register neu setzen!

        // Wenn die Adresse des Nachfolgers des neuen letzten Eintrags 
        // dem aktuellen Eintrag im HW_NEXT-Register entspricht,
        // würde das Nutzen des SW_DESC registers nicht funktionieren.
        // Daher nutzen wir für diesen Transfer den end-of-chain-Modus! 
        if( reg_next_desc == pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow )
            {
            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, ".. continue DMA with mode: END_OF_CHAIN (2)" );
            SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, 
                "DMA_Restart: Continue DMA with mode: END_OF_CHAIN (2)\n" );

#ifdef DEBUG_RESTART
            SPCM4DRV_DebugPrintInfo( pContext, "%c4: ",pR );
            SPCM4DRV_DebugPrintInfo( pContext, "%--> %d (%x) ", 
                pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2,
                pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow);
#endif // DEBUG_RESTART

            // set next ptr of last entry to 0 - end of chain
            pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow = 0;

            // !!! SW 130611: Wenn wir ein EOC in die Liste eintragen fügen wir an dieser Stelle auch ein IRQ-Flags ein!
            pNWCDmaParams->pNwcSGListCurrentLastEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;

            // !!! SW 130626: Wir merken uns den EOC-Eintrag, der das Listenende enthält.
            pNWCDmaParams->pNwcSGListCurrentEOCEntry = pNWCDmaParams->pNwcSGListCurrentLastEntry;

            // set sw desc ptr 0 - don't use it 
            NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR, 0);
            }
        else
            {
            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, ".. continue DMA with mode: REGISTER" );
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, 
                "DMA_Restart: Continue DMA with mode: REGISTER\n" );

            SPCM4DRV_DebugPrint(TRACE, pContext->dwBoardNumber, 
                    "### pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow: 0x%08x   CurrentLastEntry: 0x%08x\n", 
                     pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow,
                     pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2);

            /*
            SW 130726: Wir wechseln während des EOC-Modus nicht mehr zum REGISTER-Modus,
                       sondern warten auf das EOC-Ende!
                       Daher brauchen wir diesen Teil hier nicht mehr!
                       >>>
            
            // Wenn wir von END_OF_CHAIN Modus kommen wechseln wir zum REGISTER-Modus und
            // schließen die Desc-Liste.

            // !!! SW 130626: Wenn wir einen EOC-Eintrag haben schließen wir bei ihm die Liste.
            //if( pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow == 0)
            if( (pNWCDmaParams->pNwcSGListCurrentEOCEntry != NULL) &&
                (pNWCDmaParams->pNwcSGListCurrentEOCEntry->nextEntryPhysLow == 0) )
                {
                // Wenn die Liste beim aktuell letzten Eintrag ein EOC enthält
                // überchreiben wir das mit der Adresse des nächsten Eintrags oder
                // beim letzten Eintrag mit der Adresse des ersten Eintrags.
                
                ULONG nextEntryPhysLow = pDmaParams->dmaSGListPhysicalAddress.LowPart;

                // !!! SW 130626
                //if( pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2 != pDmaParams->dmaSGListElements )
                if( pNWCDmaParams->pNwcSGListCurrentEOCEntry->entryNumber2 != pDmaParams->dmaSGListElements )
                    {
                    // !!! SW 130626
                    //nextEntryPhysLow += sizeof(NWCORE_SGLIST_ENTRY) * pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2;
#ifdef WINVER
                    nextEntryPhysLow += sizeof(NWCORE_SGLIST_ENTRY) * pNWCDmaParams->pNwcSGListCurrentEOCEntry->entryNumber2;
#else
                    nextEntryPhysLow = __pa (pstGetNWCSGListEntry (pDmaParams, pNWCDmaParams->pNwcSGListCurrentEOCEntry->entryNumber2));
#endif
                    }

                // Wir schliessen die Desc-Liste. 

                TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, " .. DMA_Restart: Liste wird geschlossen (#%d-0x%08x)!",
                    pNWCDmaParams->pNwcSGListCurrentEOCEntry->entryNumber2, nextEntryPhysLow);

                // !!! SW 130626
                //pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow = nextEntryPhysLow;  
                pNWCDmaParams->pNwcSGListCurrentEOCEntry->nextEntryPhysLow = nextEntryPhysLow;  
                SPCM4DRV_DebugPrint(TRACE, pContext->dwBoardNumber, 
                    "### pNWCDmaParams->pNwcSGListCurrentEOCEntry->nextEntryPhysLow: 0x%08x\n", 
                     pNWCDmaParams->pNwcSGListCurrentEOCEntry->nextEntryPhysLow);

                // !!! SW 130626
                // Wenn das IRQ-Flag beim EOC-Eintrag kein 'festes' ist nehmen wir es wieder raus.
                if( (pNWCDmaParams->pNwcSGListCurrentEOCEntry->length2 & NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL) != NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL)
                    pNWCDmaParams->pNwcSGListCurrentEOCEntry->dwControl &= ~NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;

                // !!! SW 130626: Wir haben keinen EOC-Eintrag mehr.
                pNWCDmaParams->pNwcSGListCurrentEOCEntry = NULL;
                }
            <<< SW 130726
            */


#ifdef DEBUG_RESTART
            SPCM4DRV_DebugPrintInfo( pContext, "%c5 ",pR );
            SPCM4DRV_DebugPrintInfo( pContext, "%--> %d (%x) ", 
                pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2,
                pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow);
#endif // DEBUG_RESTART

            // SW 130724: Wenn das Int-Flag im bisherigen letzten Reg-Modus-Eintrag kein 'festes' ist
            // nehmen wir es wieder raus!
            if( (pNWCDmaParams->pNwcSGListCurrentLastRegModeEntry != NULL) &&
                ((pNWCDmaParams->pNwcSGListCurrentLastRegModeEntry->length2 & NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL) != NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL))
                pNWCDmaParams->pNwcSGListCurrentLastRegModeEntry->dwControl &= ~NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;

            // SW 130724: Wir fügen im aktuell letzten Reg-Modus-Eintrag ein temporäres IRQ-Flag ein!
            pNWCDmaParams->pNwcSGListCurrentLastEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;

            // SW 130724: Wir merken uns den aktuell letzten Eintrag im Reg-Modus.
            pNWCDmaParams->pNwcSGListCurrentLastRegModeEntry = pNWCDmaParams->pNwcSGListCurrentLastEntry;
            
            //SPCM4Print(("!!! new SW_DESC: 0x%08x (%d)\n", 
            //  pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow, pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2));

            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "DMA_Restart < neues SW:0x%08x)!",
                pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow);

            // Wir prüfen nochmal, ob das ENABLE-Bit gesetzt ist!
            engCntl = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
            if( (engCntl & NWD_ENGCNTRL_DMA_EN) != NWD_ENGCNTRL_DMA_EN )
                {
                engCntl &= NWD_ENGCNTRL_INTEN; // SW 130726
                engCntl |= NWD_ENGCNTRL_DMA_EN;
                INTERRUPT_LOCK(pContext); // SW 130726
                NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, engCntl);
                INTERRUPT_UNLOCK(pContext); // SW 130726

#ifdef WINVER
                SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "!!! Restart: ENABLE Bit nochmal gesetzt - 3 (0x%08x) !!!\n", engCntl & ~NWD_ENGCNTRL_DMA_EN);
#endif

                engCntl = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
#ifdef WINVER
                if( (engCntl & NWD_ENGCNTRL_DMA_EN) != NWD_ENGCNTRL_DMA_EN )
                    DbgBreakPoint();
#endif
                }

            // start DMA by setting NWD_SW_DESC_PTR register to the entry after last used entry
            NWD_WriteByOffset (pBar0Mem, 
                               pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR, 
                               pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow);
            }

        if( (pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2 == pDmaParams->dmaSGListElements) &&
            (pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow != 0) &&
            (pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow != pDmaParams->dmaSGListPhysicalAddress.LowPart) )
            {
#ifdef WINVER
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "!!!\n");
            NwcSGListDump2( pContext, pNWCDmaParams );
            DbgBreakPoint();
#endif
            }

#ifdef DEBUG_RESTART
        reg_next_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR);
        reg_sw_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR);
        reg_compl_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_COMPLETED_DESC_PTR);
        engCntl = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
        SPCM4DRV_DebugPrintInfo( pContext, "\n%cc cr:%4x pl:%8x nx:%8x sw:%8x | l:%4d f:%4d t:%4d r:%4d | ", 
            pR, engCntl, reg_compl_desc, reg_next_desc, reg_sw_desc, 
            pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2, 
            pNWCDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2,
            pNWCDmaParams->pNwcSGListCurrentFirstTestEntry->entryNumber2,
            (pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry == NULL) ? 0 : pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry->entryNumber2);
#endif // DEBUG_RESTART
        }
    else
        {
        // DMA ist fertig !

        ULONG firstEntryPhysLow;
        //ULONG lastListEntryPhysLow;
        BOOLEAN isTransferUsingWholeBuffer;

        reg_next_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR);
        reg_sw_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR);
        reg_compl_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_COMPLETED_DESC_PTR);
        //!!! 130725 engCntl = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);

        TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "DMA_Restart: DMA ist fertig: engCntl:0x%x, reg_next_desc:0x%x, reg_sw_desc:0x%x, reg_compl_desc:0x%x",
            engCntl, reg_next_desc, reg_sw_desc, reg_compl_desc );

#ifdef DEBUG_RESTART
        SPCM4DRV_DebugPrintInfo( pContext, "\n%cd cr:%4x pl:%8x nx:%8x sw:%8x | l:%4d f:%4d t:%4d r:%4d | ", 
            pR, engCntl, reg_compl_desc, reg_next_desc, reg_sw_desc, 
            pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2, 
            pNWCDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2,
            pNWCDmaParams->pNwcSGListCurrentFirstTestEntry->entryNumber2,
            (pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry == NULL) ? 0 : pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry->entryNumber2);
#endif // DEBUG_RESTART

        // Wenn wir keinen gespeicherten EOC-Eintrag haben stimmt etwas nicht!!!
        // Dann geht's nicht mehr weiter !!!
        if(pNWCDmaParams->pNwcSGListCurrentEOCEntry == NULL)
            {
            SPCM4DRV_DebugPrint (ERROR, pContext->dwBoardNumber, "DMA_Restart: EOC-Ende - wir haben keinen EOC-Eintrag !!!\n" );
            TraceEvent(TRACE_LEVEL_ERROR, DBG_DMA, "DMA_Restart < EOC-Ende - wir haben keinen EOC-Eintrag !!!" );

            return;
            }

        // Wenn die Liste nicht aufgefüllt wurde, kann DMA auch nicht neu gestartet werden.
        // Dann sind wir hier fertig.
        if( pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry == NULL)
            {
            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "DMA_Restart < Kein Speicher - kein Neustart!");

#ifdef DEBUG_RESTART
            SPCM4DRV_DebugPrintInfo( pContext, "%c6 ",pR );
#endif // DEBUG_RESTART

            return;
            }

        if( !pDmaParams->active )
            {
            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "DMA_Restart < !!! not active!!!");

            SPCM4DRV_DebugPrint (ERROR, pContext->dwBoardNumber, "DMA_Restart: DMA not active!\n");
#ifdef WINVER
            DbgBreakPoint();
#endif
            return;
            }

        // !!! SW 130626
        // Wenn das IRQ-Flag beim aktuellen EOC-Eintrag kein 'festes' ist nehmen wir es wieder raus.
        if( (pNWCDmaParams->pNwcSGListCurrentEOCEntry->length2 & NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL) != NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL)
            pNWCDmaParams->pNwcSGListCurrentEOCEntry->dwControl &= ~NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;


        // Wir starten einen neuen Transfer:

        // Wir brauchen die phys. Adresse des Eintrages nach dem letzten bearbeiteten Eintrag!
        // Wenn der letzte bearbeitete Eintrag der letzte Listen-Eintrag ist, nehmen wir die Adresse des Listenanfangs.
        // Ansonsten addieren wir zur Adresse des letzten bearbeiteten Eintrags die Größe eines Eintrags. 

        // !!! SW 130627 >>>
        // Dafür nutzen wir jetzt den neuen gespeicherten EOC-Eintrag !
        firstEntryPhysLow = pDmaParams->dmaSGListPhysicalAddress.LowPart;
        if( pNWCDmaParams->pNwcSGListCurrentEOCEntry->entryNumber2 != pDmaParams->dmaSGListElements )
#ifdef WINVER
            firstEntryPhysLow += sizeof(NWCORE_SGLIST_ENTRY) * pNWCDmaParams->pNwcSGListCurrentEOCEntry->entryNumber2;
#else
            firstEntryPhysLow = qwGetPhysAddr (pNWCDmaParams, pNWCDmaParams->pNwcSGListCurrentEOCEntry->entryNumber2);
#endif

        /*
#ifdef WINVER
        lastListEntryPhysLow = pDmaParams->dmaSGListPhysicalAddress.LowPart + 
                               sizeof(NWCORE_SGLIST_ENTRY) * (pDmaParams->dmaSGListElements-1);
#else
        lastListEntryPhysLow = __pa (pstGetNWCSGListEntry (pDmaParams, pDmaParams->dmaSGListElements - 1));
#endif
            if( reg_compl_desc == lastListEntryPhysLow )
                {
                printk ("%s - reg_compl_desc == lastListEntry\n", __FUNCTION__);
                firstEntryPhysLow = pDmaParams->dmaSGListPhysicalAddress.LowPart;
                }
// UE 130715
            else if (reg_compl_desc == 0)
                {
    printk ("%s - reg_compl_desc == 0\n", __FUNCTION__);
                NwcSGListDump2 (pContext, pDmaParams);            
    firstEntryPhysLow = pDmaParams->dmaSGListPhysicalAddress.LowPart;
                }
            else
                {
#ifdef WINVER
                //!!! SW 130610: 
                //firstEntryPhysLow = lastListEntryPhysLow + sizeof(NWCORE_SGLIST_ENTRY);
                firstEntryPhysLow = reg_compl_desc + sizeof(NWCORE_SGLIST_ENTRY);
#else

                // while not in page with entries, skip to next page with entries
                uint32 numOfEntries = 0;
                uint32 dwEntryPhysAddr = __pa (pstGetNWCSGListEntry (pDmaParams, numOfEntries));
                while (!(reg_compl_desc >= dwEntryPhysAddr && reg_compl_desc < dwEntryPhysAddr + PAGE_SIZE))
                    {
                    numOfEntries += PAGE_SIZE / sizeof (NWCORE_SGLIST_ENTRY); // 128
                    dwEntryPhysAddr = __pa (pstGetNWCSGListEntry (pDmaParams, numOfEntries));
                    }


                // if next physical address is in this page, we determine offset in page
                numOfEntries += (reg_compl_desc - dwEntryPhysAddr) / sizeof (NWCORE_SGLIST_ENTRY);

                // get entry after just processed entry
                numOfEntries++;

                firstEntryPhysLow = __pa (pstGetNWCSGListEntry (pDmaParams, numOfEntries));

#endif
                }
            // !!! SW 130627 <<<
            */



        // Wenn alle Listeneinträge wieder verfügbar sind, wurde die gesamte Liste aufgefüllt.
        isTransferUsingWholeBuffer = (pDmaParams->dmaSGListRefreshedElements == pDmaParams->dmaSGListElements);

        // Adresse des ersten und die Anzahl der wieder verfügbaren Einträge werden zurückgesetzt.
        pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry = NULL;
        pDmaParams->dmaSGListRefreshedElements = 0;

        // reset compl_desc-ptr
        // SW 130725 !!! NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_COMPLETED_DESC_PTR, 0);

        // set NWD_SW_DESC_PTR register to the same value like next_DESC
        NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR, firstEntryPhysLow);

        // set next_desc ptr (fill list into nwc)
        NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR, firstEntryPhysLow);

        // !!! SW 130611:
        // Wenn die ganze Liste wieder benutzt werden soll oder
        // wenn der aktuell letzte Listeneintrag mit dem neuen ersten ListenEintrag übereinstimmt,
        // dann nutzen wir den EOC-Modus!
        // Der REG-Modus würde nicht funktionieren, da NWD_SW_DESC_PTR == NWD_NEXT_DESC_PTR wäre.

        if( isTransferUsingWholeBuffer ||
            (pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow == firstEntryPhysLow) ) // <<< Neu
            {
            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, ".. restart DMA with mode: END_OF_CHAIN" );
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "DMA_Restart: restart DMA with mode: END_OF_CHAIN\n" );

            // set next ptr of last entry to 0 - END_OF_CHAIN
            pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow = 0;

            // !!! SW 130611: Wenn wir ein EOC in die Liste eintragen fügen wir an dieser Stelle auch ein IRQ-Flag ein!
            pNWCDmaParams->pNwcSGListCurrentLastEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;

            // !!! SW 130626: Wir merken uns den EOC-Eintrag, der das Listenende enthält.
            pNWCDmaParams->pNwcSGListCurrentEOCEntry = pNWCDmaParams->pNwcSGListCurrentLastEntry;

#ifdef DEBUG_RESTART
            SPCM4DRV_DebugPrintInfo( pContext, "%c7 ",pR );
            SPCM4DRV_DebugPrintInfo( pContext, "%--> %d (%x) ", 
                pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2,
                pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow);
#endif // DEBUG_RESTART
            }
        else
            {
            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, ".. restart DMA with mode: POINTER_REGS" );
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "DMA_Restart: restart DMA with mode: REGISTER\n" );

#ifdef DEBUG_RESTART
            SPCM4DRV_DebugPrintInfo( pContext, "%c8 ",pR );
            SPCM4DRV_DebugPrintInfo( pContext, "%--> %d (%x) ", 
                pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2,
                pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow);
#endif // DEBUG_RESTART

            // overwrite the endOfChain with phys. addr of next first descr
            // !!! SW 130726:
            pNWCDmaParams->pNwcSGListCurrentEOCEntry->nextEntryPhysLow = firstEntryPhysLow; 

#ifdef DEBUG_RESTART
            SPCM4DRV_DebugPrintInfo( pContext, "%c9 ",pR );
            SPCM4DRV_DebugPrintInfo( pContext, "%--> %d (%x) ", 
                // !!! SW 130626:
                //pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2,
                //pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow);
                pNWCDmaParams->pNwcSGListCurrentEOCEntry->entryNumber2,
                pNWCDmaParams->pNwcSGListCurrentEOCEntry->nextEntryPhysLow);
#endif // DEBUG_RESTART

            // !!! SW 130626: Wir haben keinen EOC-Eintrag mehr.
            pNWCDmaParams->pNwcSGListCurrentEOCEntry = NULL;

            // SW 130724: Wir fügen im aktuell letzten Reg-Modus-Eintrag ein temporäres IRQ-Flag ein!
            pNWCDmaParams->pNwcSGListCurrentLastEntry->dwControl |= NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL;

            // SW 130724: Wir merken uns den aktuell letzten Eintrag im Reg-Modus.
            pNWCDmaParams->pNwcSGListCurrentLastRegModeEntry = pNWCDmaParams->pNwcSGListCurrentLastEntry;
            }

        TraceEvent(TRACE_LEVEL_INFORMATION, DBG_DMA, "DMA_Restart < neues SW:0x%08x)!", pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow);

        // reset STATUS flags 
        // SW 130726 engCntl = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
        // SW 130726 NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, engCntl);

        // set DMA_EN flag 
        engCntl2 = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
        engCntl2 &= NWD_ENGCNTRL_INTEN; // SW 130726
        engCntl2 |= NWD_ENGCNTRL_DMA_EN;
        INTERRUPT_LOCK(pContext); // SW 130726
        NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, engCntl2);
        INTERRUPT_UNLOCK(pContext); // SW 130726

        // check DMA_EN flag
        //KeStallExecutionProcessor(10);
        engCntl3 = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
        if( (engCntl3 & NWD_ENGCNTRL_DMA_EN) != NWD_ENGCNTRL_DMA_EN )
            {
            engCntl3 &= NWD_ENGCNTRL_INTEN; // SW 130726
            engCntl3 |= NWD_ENGCNTRL_DMA_EN;
            INTERRUPT_LOCK(pContext); // SW 130726
            NWD_WriteByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, engCntl3);
            INTERRUPT_UNLOCK(pContext); // SW 130726

#ifdef WINVER
            SPCM4DRV_DebugPrint (TRACE, pContext->dwBoardNumber, "!!! Restart: ENABLE Bit nochmal gesetzt - 1 (o:0x%08x n:0x%08x) !!!\n", engCntl4, engCntl3 & ~NWD_ENGCNTRL_DMA_EN);
#endif

            engCntl3 = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
#ifdef WINVER
            if( (engCntl3 & NWD_ENGCNTRL_DMA_EN) != NWD_ENGCNTRL_DMA_EN )
                DbgBreakPoint();
#endif
            }

        // start DMA by setting SW_DESC to another value as NEXT_DESC
        NWD_WriteByOffset (pBar0Mem, 
                           pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR, 
                           pNWCDmaParams->pNwcSGListCurrentLastEntry->nextEntryPhysLow);


#ifdef DEBUG_RESTART
        reg_next_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR);
        reg_sw_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR);
        reg_compl_desc = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_COMPLETED_DESC_PTR);
        engCntl = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
        SPCM4DRV_DebugPrintInfo( pContext, "\n%ce cr:%4x pl:%8x nx:%8x sw:%8x | l:%4d f:%4d t:%4d r:%4d | ", 
            pR, engCntl, reg_compl_desc, reg_next_desc, reg_sw_desc, 
            pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2, 
            pNWCDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2,
            pNWCDmaParams->pNwcSGListCurrentFirstTestEntry->entryNumber2,
            (pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry == NULL) ? 0 : pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry->entryNumber2);
#endif // DEBUG_RESTART
        }

    return;
    }

// ----- Debug Funktion zum Liste Dumpen -----
#ifdef WINVER
VOID NwcSGListDump (ULONG dwBrdNr, COMMON_DMA_PARAMS* pDmaParams)
#else
void NwcSGListDump (uint32 dwBrdNr, COMMON_DMA_PARAMS* pDmaParams)
#endif
{
    ULONG i;
    NWC_DMA_PARAMS* pNWCDmaParams = (NWC_DMA_PARAMS*)pDmaParams;

    PNWCORE_SGLIST_ENTRY pSGList = pNWCDmaParams->pNwcSGListStartEntry;

    /*
    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_SGLIST, "***************************************");
    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_SGLIST, "%8s %8s %8s %8s %8s %8s %8s %8s %8s %8s",
               "Status", "length2", "Entry", "Control", "PCILow",
               "PCIHigh", "next", "length", "Entry2", "Flags");
    for (i=0; i<pDmaParams->dmaSGListElements; i++)
    {
        if (pSGList != NULL)
            TraceEvent(TRACE_LEVEL_INFORMATION, DBG_SGLIST, "%08x %08x %08x %08x %08x %08x %08x %08x %08x %s %s %s %s",
            pSGList->dwStatus, pSGList->length2, pSGList->entryNumber, 
            pSGList->dwControl, pSGList->pciAddrLow, pSGList->pciAddrHigh,
            pSGList->nextEntryPhysLow, pSGList->dwControl & NWD_S2CDESC_BYTECOUNT_MASK, pSGList->entryNumber2,
            (pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_SOP ? " SOP" : ""),
            (pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_EOP ? "EOP" : ""),
            (pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL ? "IntCmpl" : ""),
            (pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_IRQONERROR ? "IntErr" : ""));
        pSGList++;
    }
    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_SGLIST, "Elements:          %d", pDmaParams->dmaSGListElements);
    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_SGLIST, "currentFirst:      %d\n", pNWCDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2);
    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_SGLIST, "currentFirstTest:  %d\n", pNWCDmaParams->pNwcSGListCurrentFirstTestEntry->entryNumber2);
    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_SGLIST, "currentLast:       %d\n", pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2);
    TraceEvent(TRACE_LEVEL_INFORMATION, DBG_SGLIST, "***************************************\n");
    */

    if( !_printSGList )
        return;

#if (1)
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "***************************************\n");
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "%8s %8s %8s %8s %8s %8s %8s %8s %8s %8s\n",
                        "Status", "length2", "Entry", "Control", "PCILow",
                        "PCIHigh", "next", "length", "Entry2", "Flags");
    for (i=0; i<pDmaParams->dmaSGListElements; i++)
        {
        if (pSGList != NULL)
            //SPCM4DRV_DebugPrint (WARNING, dwBrdNr, "%08x %08x %08x %08x %08x %08x %08x %08x %08x %s %s %s %s\n",
            SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "%08x %08x %08x %08x %08x %08x %08x %08x %08x %s %s\n",
            pSGList->dwStatus, pSGList->length2, pSGList->entryNumber, 
            pSGList->dwControl, pSGList->pciAddrLow, pSGList->pciAddrHigh,
            pSGList->nextEntryPhysLow, pSGList->dwControl & NWD_S2CDESC_BYTECOUNT_MASK, pSGList->entryNumber2,
            //(pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_SOP ? " SOP" : ""),
            //(pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_EOP ? "EOP" : ""),
            (pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL ? "IntCmpl" : " "),
            (pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_IRQONERROR ? "IntErr" : " "));
#   ifdef WINVER
        pSGList++;
#   else
        pSGList = pstGetNWCSGListEntry (pNWCDmaParams, i + 1);
#   endif
        }
#endif

    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "Elements:          %d\n", pDmaParams->dmaSGListElements);
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "currentFirst:      %d\n", pNWCDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2);
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "currentLast:       %d\n", pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2);
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "currentFirstTest:  %d\n", pNWCDmaParams->pNwcSGListCurrentFirstTestEntry->entryNumber2);
    /*
    if( pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry != NULL )
        SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "currentFirstRefr:  %d\n", pNWCDmaParams->pNwcSGListCurrentFirstRefreshedEntry->entryNumber2);
    else
        SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "currentFirstRefr:  %d\n", 0);
    */
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "bytesTransferred:  %llu\n", pDmaParams->qwBytesTransfered);
    SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "bytesAlreadyFree:  %llu\n", pDmaParams->qwBytesAlreadyFree);
    //SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "***************************************\n");
    }

#ifdef WINVER
VOID NwcSGListDump2 (IN OUT PSPCM4DRV_DEVICE_CONTEXT pContext, PNWC_DMA_PARAMS pNWCDmaParams)
#else
void NwcSGListDump2 (SPCM_ST_CARDINFO* pContext, PNWC_DMA_PARAMS pNWCDmaParams)
#endif
{
  volatile UINT32 engCntl, reg_next_desc, reg_sw_desc, reg_compl_desc;
  //ULONG i;
  ULONG dwBrdNr = pContext->dwBoardNumber;

#ifdef WINVER
  PUCHAR pBar0Mem = pContext->memMappedAddress[0];
#else
  uint32* pBar0Mem = pContext->apdwMemMappedAddress[0];
#endif
  //PNWCORE_SGLIST_ENTRY pSGList = pNWCDmaParams->pNwcSGListStartEntry;

  // read hw_next register
  reg_next_desc = NWD_ReadByOffset (pBar0Mem, pNWCDmaParams->stCommon.dwEngAddrOffs + NWD_NEXT_DESC_PTR);
  // read sw_desc register
  reg_sw_desc = NWD_ReadByOffset (pBar0Mem, pNWCDmaParams->stCommon.dwEngAddrOffs + NWD_SW_DESC_PTR);
  // read compl_desc register
  reg_compl_desc = NWD_ReadByOffset (pBar0Mem, pNWCDmaParams->stCommon.dwEngAddrOffs + NWD_COMPLETED_DESC_PTR);
  // read ENGCNTRL register
  engCntl = NWD_ReadByOffset (pBar0Mem, pNWCDmaParams->stCommon.dwEngAddrOffs + NWD_ENGCNTRL);

  //SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "n:%4d, f:%4d, l:%4d, t:%4d, cn:%04x, pl:%x, nx:%x, sw:%x\n", 
  SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "num:%4d, t:%4d, cn:%04x, pl:%x, nx:%x, sw:%x\n", 
                           pNWCDmaParams->stCommon.dmaSGListElements,
                           //pNWCDmaParams->pNwcSGListCurrentFirstEntry->entryNumber2, 
                           //pNWCDmaParams->pNwcSGListCurrentLastEntry->entryNumber2, 
                           pNWCDmaParams->pNwcSGListCurrentFirstTestEntry->entryNumber2,
                           engCntl, reg_compl_desc, reg_next_desc, reg_sw_desc );

  /* 
  SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "***************************************\n");
  SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "%8s %8s %8s %8s %8s %8s %8s %8s %8s %8s\n",
  "Status", "length2", "Entry", "Control", "PCILow",
  "PCIHigh", "next", "length", "Entry2", "Flags");
    for (i=0; i<pDmaParams->dmaSGListElements; i++)
        {
        if (pSGList != NULL)
      //SPCM4DRV_DebugPrint (WARNING, dwBrdNr, "%08x %08x %08x %08x %08x %08x %08x %08x %08x %s %s %s %s\n",
      SPCM4DRV_DebugPrintList (WARNING, dwBrdNr, "%08x %08x %08x %08x %08x %08x %08x %08x %08x %s %s\n",
      pSGList->dwStatus, pSGList->length2, pSGList->entryNumber, 
      pSGList->dwControl, pSGList->pciAddrLow, pSGList->pciAddrHigh,
      pSGList->nextEntryPhysLow, pSGList->dwControl & NWD_S2CDESC_BYTECOUNT_MASK, pSGList->entryNumber2,
      //(pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_SOP ? " SOP" : ""),
      //(pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_EOP ? "EOP" : ""),
      (pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL ? "IntCmpl" : " "),
      (pSGList->dwControl & NWD_S2CDESC_CNTRL_FLAG_IRQONERROR ? "IntErr" : " "));
#ifdef WINVER
        pSGList++;
#else
        pSGList = pstGetNWCSGListEntry (pDmaParams, i + 1);
#endif
  }
  */
}


// ----- Debug Funktion zum DMA Dumpen -----
#ifdef WINVER
VOID NwcDMADump (PSPCM4DRV_DEVICE_CONTEXT pContext, UINT32 dwBrdNr, PCOMMON_DMA_PARAMS pDmaParams)
#else
void NwcDMADump (SPCM_ST_CARDINFO* pContext, uint32 dwBrdNr, PCOMMON_DMA_PARAMS pDmaParams)
#endif
    {
    UINT32 dwTemp;
#ifdef WINVER
    PUCHAR pBar0Mem = pContext->memMappedAddress[0];
#else
    uint32* pBar0Mem = pContext->apdwMemMappedAddress[0];
#endif

    SPCM4DRV_DebugPrint (WARNING, dwBrdNr, "DMA Dump:\n");

    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "+++\n");
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "dwEngAddrOffs: 0x%08x\n", pDmaParams->dwEngAddrOffs);

    dwTemp = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCAP);
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "NWD_ENGCAP: 0x%08x\n", dwTemp);

    dwTemp = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "NWD_ENGCNTRL: 0x%08x\n", dwTemp);

    dwTemp = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_NEXT_DESC_PTR);
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "NWD_NEXT_DESC_PTR: 0x%08x\n", dwTemp);

    dwTemp = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_SW_DESC_PTR);
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "NWD_SW_DESC_PTR: 0x%08x\n", dwTemp);

    dwTemp = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_COMPLETED_DESC_PTR);
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "NWD_COMPLETED_DESC_PTR: 0x%08x\n", dwTemp);

    dwTemp = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_ACTIVE_TIME);
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "NWD_ACTIVE_TIME: 0x%08x\n", dwTemp);

    dwTemp = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_WAIT_TIME);
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "NWD_WAIT_TIME: 0x%08x\n", dwTemp);

    dwTemp = NWD_ReadByOffset (pBar0Mem, pDmaParams->dwEngAddrOffs + NWD_COMPLETED_BYTE_COUNT);
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "NWD_COMPLETED_BYTE_COUNT: 0x%08x\n", dwTemp);

    dwTemp = NWD_ReadByOffset (pBar0Mem, NWD_COMMON_REGISTER_BLOCK);
    SPCM4DRV_DebugPrint (WARNING, pContext->dwBoardNumber, "NWD_COMMON_REGISTER_BLOCK: 0x%08x\n", dwTemp);
    }

