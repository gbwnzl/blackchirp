/*
**************************************************************************

spcm_linux_card.c                              (c) Spectrum GmbH,  07/2006

**************************************************************************

handles all card access of the Spectrum Kernel Driver. These functions are
called from the main file and don't care for the Kernel <-> User interface

**************************************************************************
*/


// ----- Linux Kernel includes -----
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/spinlock.h>



// ----- public Spectrum driver includes -----
#include "../c_header/dlltyp.h"
#include "../c_header/regs.h"
#include "../c_header/spcerr.h"


// ----- internal Spectrum driver includes -----
#include "../drv_comm/plx9060.h"
#include "../drv_comm/plxmap.h"


// ----- kernel driver includes -----
#include "../m2i_krnl/spcm2_krnl_general.h"
#include "spcm_linux_wrapper.h"
#include "spcm_linux_debug.h"
#include "../m2i_krnl/spcm_linux_ioctl.h"
#include "spcm_linux_card.h"
#include "spcm_linux_isr.h"
#include "../m4i_krnl_common/dma-nwc.h"
#include "../m4i_krnl_common/dma-xdma.h"
#include "dmafunctions.h"




/*
**************************************************************************
we define our own msleep function as this was finally added to the
kernel in version 2.6.8. For older versions we use the busy-waiting mdelay
**************************************************************************
*/

#if (LINUX_VERSION_CODE >= 0x020608)
#   define MSLEEP(ms) msleep (ms);
#else
#   define MSLEEP(ms) mdelay (ms);
#endif


void vDebugDumpCardInfo (SPCM_ST_CARDINFO* pstCardInfo)
    {
    if (pstCardInfo == NULL)
        return;

    printk ("----- Card Info Dump -----\n");
    printk ("PCIDev: %p\n", pstCardInfo->pstPCIDevice);
    printk ("Idx:    %d\n", pstCardInfo->lIdx);
    printk ("Open:   %d\n", pstCardInfo->bCardOpen);
    printk ("WakeUp: %d\n", pstCardInfo->bWakeUp);
    printk ("-----  -----  -----  -----\n");
    }


/*
**************************************************************************
card_lNew: allocate card structure and fill base information
**************************************************************************
*/

SPCM_ST_CARDINFO* card_pstNew (struct pci_dev *pstDevice, int lIdx)
    {
    SPCM_ST_CARDINFO* pstCard;
    pstCard = kmalloc (sizeof (SPCM_ST_CARDINFO), GFP_KERNEL);
    memset (pstCard, 0, sizeof(SPCM_ST_CARDINFO));
    
    // default setup of card structure
    pstCard->pstPCIDevice =     pstDevice;
    pstCard->lIdx =             lIdx;
    pstCard->bCardOpen =        0;
    sema_init (&pstCard->semDrvAccess, 1);
    pstCard->bWakeUp =          0;
//	init_waitqueue_head (&pstCard->wqC2SEvent);
//	init_waitqueue_head (&pstCard->wqS2CEvent);
//	init_waitqueue_head (&pstCard->wqUserEvent);
	init_waitqueue_head (&pstCard->wqKernelEvent);
    
    // enable the interrupts for use
    pstCard->bLocalIRQEnable =  1;
    pstCard->bDMAIRQEnable =    1;
    
    pstCard->pvContMemBuffer =      0;
    pstCard->qwContMemLen =         0;
    pstCard->qwContMemUserStart =   0;
    pstCard->qwContMemUserLen =     0;

    
    //vDMAObjectInit (pstCard);


    return pstCard;
    }



/*
**************************************************************************
card_lOpen: open function called from outside. Resets all internal stuff
**************************************************************************
*/

int card_lOpen (SPCM_ST_CARDINFO* pstCard)
    {
    int8 byStatus = 0;
    if (pstCard->eDMACore == NWC)
        byStatus = byNWCDMAObjectInit (pstCard);
    else
        byStatus = byXDMADMAObjectInit (pstCard);
    if (byStatus < 0)
        return byStatus;

    pstCard->bCardOpen = 1;

    return 0;
    }



/*
**************************************************************************
card_lClose: release function called from outside
**************************************************************************
*/

int card_lClose (SPCM_ST_CARDINFO* pstCard)
    {
    uint32 dwChannel = 0;

    pstCard->bCardOpen = 0;

    if (pstCard->eDMACore == NWC)
        {
        // stop the DMA engines
        for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_C2S_CHANNELS; ++dwChannel)
            SPCM4DRV_NWC_StopTransfer (pstCard, pstCard->astNWC_C2SDMAParams + dwChannel);
        for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_S2C_CHANNELS; ++dwChannel)
            SPCM4DRV_NWC_StopTransfer (pstCard, pstCard->astNWC_S2CDMAParams + dwChannel);

        // allow some time for the DMA to be stopped. 
        // May otherwise cause problems on fast systems as free is called when DMA is still running
        MSLEEP (10);
        // clear the SGList to get memory free
        for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_C2S_CHANNELS; ++dwChannel)
            SPCM4DRV_NWC_ClearData (pstCard, pstCard->astNWC_C2SDMAParams + dwChannel);
        for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_S2C_CHANNELS; ++dwChannel)
            SPCM4DRV_NWC_ClearData (pstCard, pstCard->astNWC_S2CDMAParams + dwChannel);

        // clear the SGList space
        for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_C2S_CHANNELS; ++dwChannel)
            {
            SPCM4DRV_FreeSGListMemory (pstCard, &((pstCard->astNWC_C2SDMAParams + dwChannel)->stCommon));
            }
        for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_S2C_CHANNELS; ++dwChannel)
            {
            SPCM4DRV_FreeSGListMemory (pstCard, &((pstCard->astNWC_S2CDMAParams + dwChannel)->stCommon));
            }
        }
    else
        {
        // stop the DMA engines
        for (dwChannel = 0; dwChannel < SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS; ++dwChannel)
            SPCM4DRV_XDMA_StopTransfer (pstCard, pstCard->astXDMA_C2SDMAParams + dwChannel);
        for (dwChannel = 0; dwChannel < SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS; ++dwChannel)
            SPCM4DRV_XDMA_StopTransfer (pstCard, pstCard->astXDMA_S2CDMAParams + dwChannel);

        // allow some time for the DMA to be stopped. 
        // May otherwise cause problems on fast systems as free is called when DMA is still running
        MSLEEP (10);
        // clear the SGList to get memory free
        for (dwChannel = 0; dwChannel < SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS; ++dwChannel)
            SPCM4DRV_XDMA_ClearData (pstCard, pstCard->astXDMA_C2SDMAParams + dwChannel);
        for (dwChannel = 0; dwChannel < SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS; ++dwChannel)
            SPCM4DRV_XDMA_ClearData (pstCard, pstCard->astXDMA_S2CDMAParams + dwChannel);

        // clear the SGList space
        for (dwChannel = 0; dwChannel < SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS; ++dwChannel)
            {
            SPCM4DRV_FreeSGListMemory (pstCard, &((pstCard->astXDMA_C2SDMAParams + dwChannel)->stCommon));
            }
        for (dwChannel = 0; dwChannel < SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS; ++dwChannel)
            {
            SPCM4DRV_FreeSGListMemory (pstCard, &((pstCard->astXDMA_S2CDMAParams + dwChannel)->stCommon));
            }
        }

    return 0;
    }



/*
**************************************************************************
card_vRead/WriteSingle: single access to card
**************************************************************************
*/

void card_vReadSingle (SPCM_ST_CARDINFO* pstCard, SPCM_IOCTL_RWSINGLE* pstRWSingle)
    {
    switch (pstRWSingle->dwCmd)
        {
        case ReadDMAReg:
            if (pstCard->eDMACore == NWC)
                pstRWSingle->dwValue = NWD_ReadByOffset (pstCard->apdwMemMappedAddress[0], pstRWSingle->dwOffset);
            else
                pstRWSingle->dwValue = XDMA_ReadByOffset (pstCard->apdwMemMappedAddress[1], pstRWSingle->dwOffset);
            break;

         case ReadLocal:
            if (pstCard->eDMACore == NWC)
                pstRWSingle->dwValue = NWD_ReadByOffset (pstCard->apdwMemMappedAddress[1], pstRWSingle->dwOffset);
            else
                pstRWSingle->dwValue = XDMA_ReadByOffset (pstCard->apdwMemMappedAddress[0], pstRWSingle->dwOffset);
            break;

        case ReadPciConfig:
            pci_read_config_dword (pstCard->pstPCIDevice, pstRWSingle->dwOffset, &pstRWSingle->dwValue);
            break;

        default:
            DEBUGLOG (DBG_ERROR, "vReadSingle: unknown Command %d\n", pstRWSingle->dwCmd);
            break;
        }
    }

// ***********************************************************************

void card_vWriteSingle (SPCM_ST_CARDINFO* pstCard, SPCM_IOCTL_RWSINGLE* pstRWSingle)
    {
    switch (pstRWSingle->dwCmd)
        {
        case WriteDMAReg:
            //DEBUGLOG (DBG_ERROR, "vWriteSingle: WriteDMAReg DMACore: %d\n", pstCard->eDMACore);
            if (pstCard->eDMACore == NWC)
                NWD_WriteByOffset (pstCard->apdwMemMappedAddress[0], pstRWSingle->dwOffset, pstRWSingle->dwValue);
            else
                XDMA_WriteByOffset (pstCard->apdwMemMappedAddress[1], pstRWSingle->dwOffset, pstRWSingle->dwValue);
            break;

        case WriteLocal:
            //DEBUGLOG (DBG_ERROR, "vWriteSingle: WriteLocal DMACore: %d\n", pstCard->eDMACore);
            if (pstCard->eDMACore == NWC)
                NWD_WriteByOffset (pstCard->apdwMemMappedAddress[1], pstRWSingle->dwOffset, pstRWSingle->dwValue);
            else
                XDMA_WriteByOffset (pstCard->apdwMemMappedAddress[0], pstRWSingle->dwOffset, pstRWSingle->dwValue);
            break;

        case WritePciConfig:
            pci_write_config_dword (pstCard->pstPCIDevice, pstRWSingle->dwOffset, pstRWSingle->dwValue);
            break;

        case Delay_us:
            if (pstRWSingle->dwValue < 1000)
                udelay (pstRWSingle->dwValue);
            else
                MSLEEP (pstRWSingle->dwValue / 1000);
            break;

        default:
            DEBUGLOG (DBG_ERROR, "vWriteSingle: unknown Command %d\n", pstRWSingle->dwCmd);
            break;
        }
    }



/*
**************************************************************************
card_vReadWriteList: multiple single accesses in a list
**************************************************************************
*/

void card_vReadWriteList (SPCM_ST_CARDINFO* pstCard, SPCM_IOCTL_RWLIST* pstRWList)
    {
    uint32  i;

    for (i = 0; i < pstRWList->dwListLen; ++i)
        {
        switch (pstRWList->pdwCmdList[i])
            {
            case ReadDMAReg:
                if (pstCard->eDMACore == NWC)
                    pstRWList->pdwDataList[i] = NWD_ReadByOffset (pstCard->apdwMemMappedAddress[0], pstRWList->pdwOffsetList[i]);
                else
                    pstRWList->pdwDataList[i] = XDMA_ReadByOffset (pstCard->apdwMemMappedAddress[1], pstRWList->pdwOffsetList[i]);
                break;
    
            case ReadLocal:
                if (pstCard->eDMACore == NWC)
                    pstRWList->pdwDataList[i] = NWD_ReadByOffset (pstCard->apdwMemMappedAddress[1], pstRWList->pdwOffsetList[i]);
                else
                    pstRWList->pdwDataList[i] = XDMA_ReadByOffset (pstCard->apdwMemMappedAddress[0], pstRWList->pdwOffsetList[i]);
                break;
    
            case ReadPciConfig:
                pci_read_config_dword (pstCard->pstPCIDevice, pstRWList->pdwOffsetList[i], &pstRWList->pdwDataList[i]);
                break;

            case WriteDMAReg:
                if (pstCard->eDMACore == NWC)
                    NWD_WriteByOffset (pstCard->apdwMemMappedAddress[0], pstRWList->pdwOffsetList[i], pstRWList->pdwDataList[i]);
                else
                    XDMA_WriteByOffset (pstCard->apdwMemMappedAddress[1], pstRWList->pdwOffsetList[i], pstRWList->pdwDataList[i]);
                break;
    
            case WriteLocal:
                if (pstCard->eDMACore == NWC)
                    NWD_WriteByOffset (pstCard->apdwMemMappedAddress[1], pstRWList->pdwOffsetList[i], pstRWList->pdwDataList[i]);
                else
                    XDMA_WriteByOffset (pstCard->apdwMemMappedAddress[0], pstRWList->pdwOffsetList[i], pstRWList->pdwDataList[i]);
                break;
    
            case WritePciConfig:
                pci_write_config_dword (pstCard->pstPCIDevice, pstRWList->pdwOffsetList[i], pstRWList->pdwDataList[i]);
                break;
    
            case Delay_us:
                if (pstRWList->pdwDataList[i] < 1000)
                    udelay (pstRWList->pdwDataList[i]);
                else
                    MSLEEP (pstRWList->pdwDataList[i] / 1000);
                break;
    
            case ListEnd:
                // abort list processing
                i = pstRWList->dwListLen;
                break;

            default:
                DEBUGLOG (DBG_ERROR, "vReadWriteList: unknown Command %d\n", pstRWList->pdwCmdList[i]);
                break;
            }
        }
    }


/*
**************************************************************************
card_lReadCardStatus: read status of card and dma buffers
**************************************************************************
*/

int card_lReadCardStatus (SPCM_ST_CARDINFO* pstCard, uint32 dwDMAChannel, SPCM_IOCTL_CARDSTATUS* pstCardStatus)
    {
    bool bWriteToDevice   = false;
    uint32* pdwCardStatus = NULL;
    uint32 dwByteCount    = 0;

    if (pstCard->eDMACore == NWC)
        {
        COMMON_DMA_PARAMS* pDmaParams = card_pstGetDMAParams (pstCard, dwDMAChannel);
        if (pDmaParams == NULL)
            {
            DEBUGLOG (DBG_ERROR, "CARDSTATUS failed (invalid channel param: %d)\n", dwDMAChannel);
            return -EFAULT;
            }

        bWriteToDevice = card_bDMAChannelDirIsWrite (dwDMAChannel);

        // read card status: user logic memory part
        pdwCardStatus = (uint32*)((char*)pstCard->apdwMemMappedAddress[1] + BASEB_RD_MEM_STATUS);
        pstCardStatus->dwCardStatus = *pdwCardStatus;

        // read the fillsize of the various memories and convert it to M2/3 compatible value
        dwByteCount = NWD_ReadByOffset (pstCard->apdwMemMappedAddress[1], BASEB_RD_MEM_BYTE_COUNT);

        // Not 4 GB memory?
        if (pstCardStatus->qwDMA0UsedBuf)
            {
    #ifdef __aarch64__
            // the GCC for nVidia Jetson did not like the floating point math
            dwByteCount = (ULONG) ((150ULL * dwByteCount / pstCardStatus->qwDMA0UsedBuf) + 5) / 10;
    #else
            // and on the other hand using the AARCH64 code above causes problems on 32bit linux systems which don't implement __udivdi3 (division of 64bit values)
            dwByteCount = (ULONG) ((15.0 * dwByteCount / (double) pstCardStatus->qwDMA0UsedBuf) + 0.5);
    #endif // arm
            if (dwByteCount > 0xf)
                dwByteCount = 0xf;
            dwByteCount <<= 28;
            }
        pstCardStatus->dwCardStatus |= (dwByteCount & 0xf0000000);


        if (bWriteToDevice)
            {
            // ----- S2C -----
            pDmaParams = &pstCard->astNWC_S2CDMAParams[0].stCommon; // only one DMA channel in PC->Card direction

            if (pDmaParams->active)
                {
                if (down_interruptible (&pDmaParams->semAccess))
                    return -EBUSY;
                pstCardStatus->qwDMA0UsedBuf = pDmaParams->qwBytesTransfered - pDmaParams->qwBytesAlreadyFree; // DATA
                // SW 130726 pstCardStatus->dwDMA0UsedBuf = pDmaParams->bytesTransfered; // DATA
                up (&pDmaParams->semAccess);
                }
            else
                {
                DEBUGLOG (DBG_TRACE, "CARDSTATUS: (s2c_0 is NOT active)\n");
                pstCardStatus->qwDMA0UsedBuf = 0;
                }
            pstCardStatus->qwDMA1UsedBuf = 0; // TS
            pstCardStatus->qwFIFOStatus = 0; // ABA
            }
        else
            {
           // ----- C2S -----

            // DATA
            pDmaParams = &pstCard->astNWC_C2SDMAParams[0].stCommon;
            if (pDmaParams->active)
                {
                if (down_interruptible (&pDmaParams->semAccess))
                    return -EBUSY;
                pstCardStatus->qwDMA0UsedBuf = pDmaParams->qwBytesTransfered - pDmaParams->qwBytesAlreadyFree; // DATA
                // SW 130726 pstCardStatus->dwDMA0UsedBuf = pDmaParams->bytesTransfered; // DATA
                up (&pDmaParams->semAccess);
                }
            else
                {
                pstCardStatus->qwDMA0UsedBuf = 0;
                }
            
            // TS
            pDmaParams = &pstCard->astNWC_C2SDMAParams[1].stCommon;
            if (pDmaParams->active)
                {
                if (down_interruptible (&pDmaParams->semAccess))
                    return -EBUSY;
                pstCardStatus->qwDMA1UsedBuf= pDmaParams->qwBytesTransfered - pDmaParams->qwBytesAlreadyFree; // TS 
                // SW 130726 pstCardStatus->dwDMA1UsedBuf = pDmaParams->bytesTransfered; // TS
                up (&pDmaParams->semAccess);
                }
            else
                {
                pstCardStatus->qwDMA1UsedBuf = 0;
                }

            // ABA
            pDmaParams = &pstCard->astNWC_C2SDMAParams[2].stCommon;
            if (pDmaParams->active)
                {
                if (down_interruptible (&pDmaParams->semAccess))
                    return -EBUSY;
                pstCardStatus->qwFIFOStatus = pDmaParams->qwBytesTransfered - pDmaParams->qwBytesAlreadyFree; // ABA
                // SW 130726 pstCardStatus->dwFIFOStatus = pDmaParams->bytesTransfered; // ABA
                up (&pDmaParams->semAccess);
                }
            else
                {
                pstCardStatus->qwFIFOStatus = 0;
                }
            }
        }
    else // XDMA
        {
        volatile uint32* pdwCardStatus = (uint32*)((char*)pstCard->apdwMemMappedAddress[0] + BASEB_RD_MEM_STATUS);

        COMMON_DMA_PARAMS* pDmaParams = card_pstGetDMAParams (pstCard, dwDMAChannel);
        if (pDmaParams == NULL)
            {
            DEBUGLOG (DBG_ERROR, "CARDSTATUS failed (invalid channel param: %d)\n", dwDMAChannel);
            return -EFAULT;
            }

        // read the fillsize of the various memories and convert it to M2/3 compatible value
        // the read value is "bytes / 4096"!
        dwByteCount = XDMA_ReadByOffset (pstCard->apdwMemMappedAddress[0], M5REG_RD_MEM_BYTE_COUNT);

        if (pstCardStatus->qwDMA0UsedBuf)
            {
            // the GCC for nVidia Jetson did not like the floating point math, so we don't use it
            // multiply with 4k to get real bytes
    #ifdef __aarch64__
            // the GCC for nVidia Jetson did not like the floating point math
            dwByteCount = (ULONG) (((150ULL * dwByteCount * KILO_B(4)) / pstCardStatus->qwDMA0UsedBuf) + 5) / 10;
    #else
            // and on the other hand using the AARCH64 code above causes problems on 32bit linux systems which don't implement __udivdi3 (division of 64bit values)
            dwByteCount = (ULONG) ((15.0 * dwByteCount * KILO_B(4) / (double) pstCardStatus->qwDMA0UsedBuf) + 0.5);
    #endif // arm
            if (dwByteCount > 0xf)
                dwByteCount = 0xf;
            dwByteCount <<= 28;
            }
        pstCardStatus->dwCardStatus = *pdwCardStatus;
        pstCardStatus->dwCardStatus |= (dwByteCount & 0xf0000000);

        bWriteToDevice = card_bDMAChannelDirIsWrite (dwDMAChannel);

        if (bWriteToDevice)
            {
            // ----- S2C -----
            pDmaParams = &pstCard->astXDMA_S2CDMAParams[0].stCommon; // only one DMA channel in PC->Card direction

            if (pDmaParams->active)
                {
                if (down_interruptible (&pDmaParams->semAccess))
                    return -EBUSY;
                pstCardStatus->qwDMA0UsedBuf = pDmaParams->qwBytesTransfered - pDmaParams->qwBytesAlreadyFree; // DATA
                up (&pDmaParams->semAccess);
                }
            else
                {
                DEBUGLOG (DBG_TRACE, "CARDSTATUS: (s2c_0 is NOT active)\n");
                pstCardStatus->qwDMA0UsedBuf = 0;
                }
            pstCardStatus->qwDMA1UsedBuf = 0; // TS
            pstCardStatus->qwFIFOStatus =  0; // ABA
            }
        else
            {
            // ----- C2S -----

            // DATA
            pDmaParams = &pstCard->astXDMA_C2SDMAParams[0].stCommon;
            if (pDmaParams->active)
                {
                if (down_interruptible (&pDmaParams->semAccess))
                    return -EBUSY;
                pstCardStatus->qwDMA0UsedBuf = pDmaParams->qwBytesTransfered - pDmaParams->qwBytesAlreadyFree; // DATA
                up (&pDmaParams->semAccess);
                }
            else
                {
                pstCardStatus->qwDMA0UsedBuf = 0;
                }
            
            // TS
            pDmaParams = &pstCard->astXDMA_C2SDMAParams[1].stCommon;
            if (pDmaParams->active)
                {
                if (down_interruptible (&pDmaParams->semAccess))
                    return -EBUSY;
                pstCardStatus->qwDMA1UsedBuf= pDmaParams->qwBytesTransfered - pDmaParams->qwBytesAlreadyFree; // TS 
                up (&pDmaParams->semAccess);
                }
            else
                {
                pstCardStatus->qwDMA1UsedBuf = 0;
                }

            // ABA
            // no support for ABA, so no third C2S DMA channel
            pstCardStatus->qwFIFOStatus = 0;
            }
        }

    return 0;
    }

COMMON_DMA_PARAMS* card_pstGetDMAParams (SPCM_ST_CARDINFO* pstCard, uint32 dwDMAChannel)
    {
    if (pstCard->eDMACore == NWC)
        {
        switch (dwDMAChannel)
            {
            case DMA_C2S_CHANNEL0: return &pstCard->astNWC_C2SDMAParams[0].stCommon;
            case DMA_C2S_CHANNEL1: return &pstCard->astNWC_C2SDMAParams[1].stCommon;
            case DMA_C2S_CHANNEL2: return &pstCard->astNWC_C2SDMAParams[2].stCommon;
            case DMA_C2S_CHANNEL3: return &pstCard->astNWC_C2SDMAParams[3].stCommon;
            case DMA_S2C_CHANNEL0: return &pstCard->astNWC_S2CDMAParams[0].stCommon;
            case DMA_S2C_CHANNEL1: return &pstCard->astNWC_S2CDMAParams[1].stCommon;
            case DMA_S2C_CHANNEL2: return &pstCard->astNWC_S2CDMAParams[2].stCommon;
            case DMA_S2C_CHANNEL3: return &pstCard->astNWC_S2CDMAParams[3].stCommon;

            // Card -> GPU
            // this is no real "channel", but simply used to be able to tell card-to-PC and card-to-GPU apart
            // the card's DMA channels are the same as above
            case DMA_C2G_CHANNEL0: return &pstCard->astNWC_C2SDMAParams[0].stCommon;
            case DMA_C2G_CHANNEL1: return &pstCard->astNWC_C2SDMAParams[1].stCommon;
            case DMA_C2G_CHANNEL2: return &pstCard->astNWC_C2SDMAParams[2].stCommon;
            case DMA_C2G_CHANNEL3: return &pstCard->astNWC_C2SDMAParams[3].stCommon;

            // GPU -> Card
            case DMA_G2C_CHANNEL0: return &pstCard->astNWC_S2CDMAParams[0].stCommon;
            case DMA_G2C_CHANNEL1: return &pstCard->astNWC_S2CDMAParams[1].stCommon;
            case DMA_G2C_CHANNEL2: return &pstCard->astNWC_S2CDMAParams[2].stCommon;
            case DMA_G2C_CHANNEL3: return &pstCard->astNWC_S2CDMAParams[3].stCommon;
            default:               return NULL;
            }
        }
    else
        {
        switch (dwDMAChannel)
            {
            case DMA_C2S_CHANNEL0: return &pstCard->astXDMA_C2SDMAParams[0].stCommon;
            case DMA_C2S_CHANNEL1: return &pstCard->astXDMA_C2SDMAParams[1].stCommon;
            case DMA_S2C_CHANNEL0: return &pstCard->astXDMA_S2CDMAParams[0].stCommon;

            // Card -> GPU
            // this is no real "channel", but simply used to be able to tell card-to-PC and card-to-GPU apart
            // the card's DMA channels are the same as above
            case DMA_C2G_CHANNEL0: return &pstCard->astXDMA_C2SDMAParams[0].stCommon;
            case DMA_C2G_CHANNEL1: return &pstCard->astXDMA_C2SDMAParams[1].stCommon;

            // GPU -> Card
            case DMA_G2C_CHANNEL0: return &pstCard->astXDMA_S2CDMAParams[0].stCommon;
            default:               return NULL;
            }
        }
    }


bool card_bDMAChannelDirIsWrite (uint32 dwDMAChannel)
    {
    switch (dwDMAChannel)
        {
        case DMA_S2C_CHANNEL0:
        case DMA_S2C_CHANNEL1:
        case DMA_S2C_CHANNEL2:
        case DMA_S2C_CHANNEL3:
        case DMA_G2C_CHANNEL0:
        case DMA_G2C_CHANNEL1:
        case DMA_G2C_CHANNEL2:
        case DMA_G2C_CHANNEL3:
            return true;
        default:
            return false;
        }
    
    }

