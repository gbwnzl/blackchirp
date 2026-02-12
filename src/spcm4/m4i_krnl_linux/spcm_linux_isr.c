/*
**************************************************************************

spcm_linux_isr.c                              (c) Spectrum GmbH,  08/2006

**************************************************************************

handles the interrupts

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
#include <linux/sched.h>
#include <linux/spinlock.h>

#if (LINUX_VERSION_CODE < 0x020600)
	#include <linux/tqueue.h>
#endif

// ----- public Spectrum driver includes -----
#include "../c_header/dlltyp.h"
#include "../c_header/regs.h"
#include "../c_header/spcerr.h"



// ----- kernel driver includes -----
#include "../m2i_krnl/spcm2_krnl_general.h"
#include "spcm_linux_wrapper.h"
#include "spcm_linux_debug.h"
#include "../m2i_krnl/spcm_linux_ioctl.h"
#include "spcm_linux_card.h"
#include "spcm_linux_isr.h"

#include "../m4i_krnl_common/nwdcore.h"
#include "../m4i_krnl_common/xdmacore.h"
#include "../m4i_krnl_common/dma-nwc.h"
#include "../m4i_krnl_common/dma-xdma.h"
#include "dmafunctions.h"


// SA_SHIRQ is permanently replaced by IRQF_SHARED beginning with kernel 2.6.24
#ifndef SA_SHIRQ
#   define SA_SHIRQ IRQF_SHARED
#endif

bool bCheckDMAChannel (SPCM_ST_CARDINFO* pstCard, void* pstDMAParams, bool bFromDPC);

#define QWORD_FROM_DWORD(dwHigh, dwLow)((((unsigned long long)dwHigh) << 32) | dwLow)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11))
    DEFINE_SPINLOCK(stIRQLock);
#else
    spinlock_t stIRQLock = SPIN_LOCK_UNLOCKED;
#endif
unsigned long dwIRQLockFlags;

/*
**************************************************************************
Interrupt service routine
**************************************************************************
*/

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))    
#   if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
        static irqreturn_t vInterruptService (int irq, void* dev_id)
#   else
        static irqreturn_t vInterruptService (int irq, void* dev_id, struct pt_regs *regs)
#   endif
#else
    static void vInterruptService (int irq, void* dev_id, struct pt_regs *regs)
#endif

	{
    SPCM_ST_CARDINFO*   pstCard = (SPCM_PST_CARDINFO) dev_id;
    bool                bQueueBottomHalf = false;
    bool                bAnswerInterrupt = false;

    DEBUGLOG (DBG_TRACESIR, "ISR - Start\n");

    if (pstCard->eDMACore == NWC)
        {
        // set pointer to BAR0 memory region
        uint32* pdwBAR0Mem = pstCard->apdwMemMappedAddress[0];

        //DEBUGLOG (DBG_TRACESIR, "%s - %p\n", __FUNCTION__, pdwBAR0Mem);
        

        // read common control and status register
        uint32 dwComReg = NWD_ReadByOffset (pdwBAR0Mem, NWD_COMMON_REGISTER_BLOCK);
        //DEBUGLOG (DBG_TRACESIR, "%s - %x\n", __FUNCTION__, dwComReg);

        // ----- user interrupt active -----
        if (dwComReg & NWD_COMREG_USER_INT)
            {
            uint32 dwChannel    = 0;
            uint32 dwCardStatus = 0;
            DEBUGLOG (DBG_TRACESIR, "ISR: USERINTACTIVE\n");

            // reset interrup by writing back with NWD_COMREG_USER_INT bit set
            NWD_WriteByOffset (pdwBAR0Mem, NWD_COMMON_REGISTER_BLOCK, dwComReg);

            pstCard->bUserIntActive = true;
            bQueueBottomHalf        = true;
            bAnswerInterrupt        = true;

            // read the card status: user logic memory part
            dwCardStatus = NWD_ReadByOffset (pstCard->apdwMemMappedAddress[1], BASEB_RD_MEM_STATUS);

            // is one of the c2s channel DMA-stop-interrupt active?
            for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_C2S_CHANNELS; ++dwChannel )
                {
                if (dwCardStatus & (RD_MS_DATA_DMA_STOPPED << dwChannel))
                    {
                    // if any transfer is running we have to request a dpc to do the rest
                    if (pstCard->astNWC_C2SDMAParams[dwChannel].stCommon.active)
                        pstCard->astNWC_C2SDMAParams[dwChannel].stCommon.somethingToDo = true;
                    }
                }
            }

        // ----- DMA interrupt active -----
        if (dwComReg & (NWD_COMREG_S2C_INT_ENG_MASK | NWD_COMREG_C2S_INT_ENG_MASK))
            {
            volatile uint32 dwReg;
            uint32 dwChannel;
            NWC_DMA_PARAMS* pDmaParams = NULL;

            // ----- one of c2s channel interrupts active -----
            for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_C2S_CHANNELS; ++dwChannel)
                {
                pDmaParams = pstCard->astNWC_C2SDMAParams + dwChannel;
                if (   ((dwChannel == 0) && (dwComReg & NWD_COMREG_C2S_INT_ENG0))
                    || ((dwChannel == 1) && (dwComReg & NWD_COMREG_C2S_INT_ENG1))
                    || ((dwChannel == 2) && (dwComReg & NWD_COMREG_C2S_INT_ENG2))
                    || ((dwChannel == 3) && (dwComReg & NWD_COMREG_C2S_INT_ENG3)))
                    {
                    DEBUGLOG (DBG_TRACESIR, "ISR: DMA C2S %u\n", dwChannel);

                    // ----- clear interrupt by reading and writing the corresponding engine register -----
                    spin_lock_irqsave (&stIRQLock, dwIRQLockFlags);
                    dwReg = NWD_ReadByOffset (pdwBAR0Mem, pDmaParams->stCommon.dwEngAddrOffs + NWD_ENGCNTRL);
                    NWD_WriteByOffset (pdwBAR0Mem, pDmaParams->stCommon.dwEngAddrOffs + NWD_ENGCNTRL, dwReg);
                    spin_unlock_irqrestore (&stIRQLock, dwIRQLockFlags);
                    

                    // ----- if any transfer is running we have to process the bottom half -----
                    if (pDmaParams->stCommon.active)
                        pDmaParams->stCommon.somethingToDo = true;

                    bQueueBottomHalf = true;
                    bAnswerInterrupt = true;
                    }
                }

            // ----- one of s2c channel interrupts active -----
            for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_S2C_CHANNELS; ++dwChannel)
                {
                pDmaParams = pstCard->astNWC_S2CDMAParams + dwChannel;
                if (   ((dwChannel == 0) && (dwComReg & NWD_COMREG_S2C_INT_ENG0))
                    || ((dwChannel == 1) && (dwComReg & NWD_COMREG_S2C_INT_ENG1))
                    || ((dwChannel == 2) && (dwComReg & NWD_COMREG_S2C_INT_ENG2))
                    || ((dwChannel == 3) && (dwComReg & NWD_COMREG_S2C_INT_ENG3)))
                    {
                    DEBUGLOG (DBG_TRACESIR, "ISR: DMA S2C %u\n", dwChannel);

                    // ----- clear interrupt by reading and writing the corresponding engine register -----
                    spin_lock_irqsave (&stIRQLock, dwIRQLockFlags);
                    dwReg = NWD_ReadByOffset (pdwBAR0Mem, pDmaParams->stCommon.dwEngAddrOffs + NWD_ENGCNTRL);
                    NWD_WriteByOffset (pdwBAR0Mem, pDmaParams->stCommon.dwEngAddrOffs + NWD_ENGCNTRL, dwReg);
                    spin_unlock_irqrestore (&stIRQLock, dwIRQLockFlags);

                    // ----- if any transfer is running we have to process the bottom half -----
                    if (pDmaParams->stCommon.active)
                        pDmaParams->stCommon.somethingToDo = true;

                    bQueueBottomHalf = true;
                    bAnswerInterrupt = true;
                    }
                }
            }
        }
    else
        {
        // set pointer to BAR1 memory region
        uint32* pdwBAR1Mem = pstCard->apdwMemMappedAddress[1];

        uint32 dwChIrqReq =  0;

        // ----- DMA interrupt active -----
        // Read ‘IRQ Block Channel Interrupt Request
        dwChIrqReq = XDMA_ReadByOffset (pdwBAR1Mem, XDMA_TARGET_IRQBLOCK + XDMA_REG_IRQBLOCK_CH_IRQ_REQUEST);
        if (dwChIrqReq & (XDMA_IRQREQ_H2C_INT_ENG_MASK | XDMA_IRQREQ_C2H_INT_ENG_MASK))
            {
            uint32 dwChannel;
            XDMA_DMA_PARAMS* pDmaParams = NULL;

            // ----- one of c2h channel interrupts active -----
            for (dwChannel = 0; dwChannel < SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS; ++dwChannel)
                {
                uint32 dwStatus = 0;
                pDmaParams = pstCard->astXDMA_C2SDMAParams + dwChannel;
                if (   ((dwChannel == 0) && (dwChIrqReq & XDMA_IRQREQ_C2H_INT_ENG0))
                    || ((dwChannel == 1) && (dwChIrqReq & XDMA_IRQREQ_C2H_INT_ENG1)))
                    {
                    DEBUGLOG (DBG_TRACESIR, "ISR: DMA C2H %u\n", dwChannel);

                    // ----- clear interrupt in the irqblock register -----
                    spin_lock_irqsave (&stIRQLock, dwIRQLockFlags);
                    // Mask corresponding channel interrupt writing to 0x2018 .
                    //XDMA_WriteByOffset (pdwBAR1Mem, XDMA_TARGET_IRQBLOCK + XDMA_REG_IRQBLOCK_CH_IRQ_EN_MASK_CLR, (XDMA_IRQREQ_C2H_INT_ENG0 << dwChannel));
                    spin_unlock_irqrestore (&stIRQLock, dwIRQLockFlags);
                    

                    // ----- if any transfer is running we have to process the bottom half -----
                    if (pDmaParams->stCommon.active)
                        pDmaParams->stCommon.somethingToDo = true;

                    bQueueBottomHalf = true;
                    bAnswerInterrupt = true;

        // TODO
        // Read corresponding ‘Status register and clear it
        dwStatus = XDMA_ReadByOffset (pdwBAR1Mem, pDmaParams->stCommon.dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_STATUS_CLEARONREAD);

                    }
                }

            // ----- one of h2c channel interrupts active -----
            for (dwChannel = 0; dwChannel < SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS; ++dwChannel)
                {
                uint32 dwStatus = 0;
                pDmaParams = pstCard->astXDMA_S2CDMAParams + dwChannel;
                if ((dwChannel == 0) && (dwChIrqReq & XDMA_IRQREQ_H2C_INT_ENG0))
                    {
                    DEBUGLOG (DBG_TRACESIR, "ISR: DMA H2C %u\n", dwChannel);

                    // ----- clear interrupt in the irqblock register -----
                    spin_lock_irqsave (&stIRQLock, dwIRQLockFlags);
                    // Mask corresponding channel interrupt writing to 0x2018 .
                    //XDMA_WriteByOffset (pdwBAR1Mem, XDMA_TARGET_IRQBLOCK + XDMA_REG_IRQBLOCK_CH_IRQ_EN_MASK_CLR, (XDMA_IRQREQ_H2C_INT_ENG0 << dwChannel));
                    spin_unlock_irqrestore (&stIRQLock, dwIRQLockFlags);

                    // ----- if any transfer is running we have to process the bottom half -----
                    if (pDmaParams->stCommon.active)
                        pDmaParams->stCommon.somethingToDo = true;

                    bQueueBottomHalf = true;
                    bAnswerInterrupt = true;

        // TODO
        // Read corresponding ‘Status register and clear it
        dwStatus = XDMA_ReadByOffset (pdwBAR1Mem, pDmaParams->stCommon.dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_STATUS_CLEARONREAD);

                    }
                }
            }

        }

    // ----- if requested we queue the bottom half -----
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
    // if requested we now call the bottom half to do the work
    if (pstCard->bCardOpen && bQueueBottomHalf)
        schedule_work (&pstCard->stISRWork);
#else
    if (pstCard->bCardOpen && bQueueBottomHalf)
        if((schedule_task(&pstCard->stISRTask)) == 0)
            DEBUGLOG (DBG_TRACESIR, "Task already in queue\n");
#endif

    // ----- kernel 2.6 get's handled flag back -----
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
    return IRQ_RETVAL(bQueueBottomHalf);
#endif
	}

// ----- ISR for XDMA User Interrupts -----
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))    
#   if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
        static irqreturn_t vInterruptService_XDMA_USR (int irq, void* dev_id)
#   else
        static irqreturn_t vInterruptService_XDMA_USR (int irq, void* dev_id, struct pt_regs *regs)
#   endif
#else
    static void vInterruptService_XDMA_USR (int irq, void* dev_id, struct pt_regs *regs)
#endif
    {
    SPCM_ST_CARDINFO*   pstCard = (SPCM_PST_CARDINFO) dev_id;

    DEBUGLOG (DBG_NONE, "XDMA USR ISR - Start\n");

    // ----- user interrupt active -----
    pstCard->bUserIntActive = true;

    // read the card status: user logic memory part
    // ONLY NECESSARY FOR LEGACY INTERRUPT
    //dwCardStatus = XDMA_ReadByOffset (pstCard->apdwMemMappedAddress[1], M5REG_RD_MEM_STATUS_ISR);

    // ----- if requested we queue the bottom half -----
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
    // if requested we now call the bottom half to do the work
    if (pstCard->bCardOpen)
        schedule_work (&pstCard->stISRWork);
#else
    if (pstCard->bCardOpen)
        if((schedule_task(&pstCard->stISRTask)) == 0)
            DEBUGLOG (DBG_TRACESIR, "Task already in queue\n");
#endif

    // ----- kernel 2.6 get's handled flag back -----
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
    return IRQ_HANDLED;
#endif
    }



/*
**************************************************************************
vISRBottomHalf: does the work scheduled by of the interrupt service
                routine
**************************************************************************
*/

// Kernel >= 2.6.20 using work_struct as call parameter
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
static void vISRBottomHalf (struct work_struct* pstWorkStruct)
    {
    SPCM_ST_CARDINFO* pstCard = container_of (pstWorkStruct, SPCM_ST_CARDINFO, stISRWork);

// Kernel prior to 2.6.20 using dedicated data paramter 
#else
static void vISRBottomHalf (void* pvParam)
    {
    SPCM_ST_CARDINFO* pstCard = (SPCM_PST_CARDINFO) pvParam;
#endif
    uint32 dwChannel = 0;

    if (pstCard->eDMACore == NWC)
        {
        // set pointer to BAR0 memory region
        NWC_DMA_PARAMS* pDmaParams = NULL;

        while (    pstCard->astNWC_C2SDMAParams[0].stCommon.somethingToDo
                || pstCard->astNWC_C2SDMAParams[1].stCommon.somethingToDo
                || pstCard->astNWC_C2SDMAParams[2].stCommon.somethingToDo
                || pstCard->astNWC_C2SDMAParams[3].stCommon.somethingToDo
                || pstCard->astNWC_S2CDMAParams[0].stCommon.somethingToDo
                || pstCard->astNWC_S2CDMAParams[1].stCommon.somethingToDo
                || pstCard->astNWC_S2CDMAParams[2].stCommon.somethingToDo
                || pstCard->astNWC_S2CDMAParams[3].stCommon.somethingToDo
                || pstCard->bUserIntActive)
            {
            // ----- check C2S channels -----
            for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_C2S_CHANNELS; ++dwChannel)
                {
                pDmaParams = pstCard->astNWC_C2SDMAParams + dwChannel;
                if (pDmaParams->stCommon.somethingToDo)
                    {
                    DEBUGLOG (DBG_TRACESIR, "%s - C2S Ch %u\n", __FUNCTION__, dwChannel);

                    spin_lock_irqsave (&stIRQLock, dwIRQLockFlags);
                    pDmaParams->stCommon.somethingToDo = false;
                    spin_unlock_irqrestore (&stIRQLock, dwIRQLockFlags);

                    if (bCheckDMAChannel (pstCard, pDmaParams, true))
                        pstCard->bC2SDMAActive = true;
                    }
                }

            // ----- check S2C channels -----
            for (dwChannel = 0; dwChannel < SPCM4DRV_DMA_NUMBER_OF_S2C_CHANNELS; ++dwChannel)
                {
                pDmaParams = pstCard->astNWC_S2CDMAParams + dwChannel;
                if (pDmaParams->stCommon.somethingToDo)
                    {
                    DEBUGLOG (DBG_TRACESIR, "%s - S2C Ch %u\n", __FUNCTION__, dwChannel);

                    spin_lock_irqsave (&stIRQLock, dwIRQLockFlags);
                    pDmaParams->stCommon.somethingToDo = false;
                    spin_unlock_irqrestore (&stIRQLock, dwIRQLockFlags);

                    if (bCheckDMAChannel (pstCard, pDmaParams, true))
                        pstCard->bS2CDMAActive = true;
                    }
                }

            // ----- wake up user thread by C2S event -----
            if (pstCard->bC2SDMAActive)
                {
                pstCard->bC2SDMAActive = false;
                
                // ----- signal C2S DMA -----
    //            if (pstCard->wqC2SEvent) // TODO: Windows hat hier ein if()
                //wake_up (&pstCard->wqC2SEvent);
                pstCard->bWakeUp = 1;
                wake_up_interruptible (&pstCard->wqKernelEvent);
                }

            // ----- wake up user thread by S2C event -----
            if (pstCard->bS2CDMAActive)
                {
                pstCard->bS2CDMAActive = false;
                
                // ----- signal S2C DMA -----
    //            if (pstCard->wqS2CEvent) // TODO: Windows hat hier ein if()
                //wake_up (&pstCard->wqS2CEvent);
                pstCard->bWakeUp = 1;
                wake_up_interruptible (&pstCard->wqKernelEvent);
                } 

            // ----- user interrupt -----
            if (pstCard->bUserIntActive)
                {
                pstCard->bUserIntActive = false;

    //            if (pstCard->wqUserEvent) // TODO: Windows hat hier ein if()
                //wake_up (&pstCard->wqUserEvent);

                pstCard->bWakeUp = 1;
                wake_up_interruptible (&pstCard->wqKernelEvent);
                }
            }
        }
    else
        {
        XDMA_DMA_PARAMS* pDmaParams = NULL;

        while (    pstCard->astXDMA_C2SDMAParams[0].stCommon.somethingToDo
                || pstCard->astXDMA_C2SDMAParams[1].stCommon.somethingToDo
                || pstCard->astXDMA_S2CDMAParams[0].stCommon.somethingToDo
                || pstCard->bUserIntActive)
            {
            // ----- check C2S channels -----
            for (dwChannel = 0; dwChannel < SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS; ++dwChannel)
                {
                pDmaParams = pstCard->astXDMA_C2SDMAParams + dwChannel;
                if (pDmaParams->stCommon.somethingToDo)
                    {
                    DEBUGLOG (DBG_TRACESIR, "%s - C2H Ch %u\n", __FUNCTION__, dwChannel);

                    spin_lock_irqsave (&stIRQLock, dwIRQLockFlags);
                    pDmaParams->stCommon.somethingToDo = false;
                    spin_unlock_irqrestore (&stIRQLock, dwIRQLockFlags);

                    pDmaParams->lCnt = 0;
                    if (bCheckDMAChannel (pstCard, pDmaParams, true))
                        pstCard->bC2SDMAActive = true;
                    }
                }

            // ----- check H2C channels -----
            for (dwChannel = 0; dwChannel < SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS; ++dwChannel)
                {
                pDmaParams = pstCard->astXDMA_S2CDMAParams + dwChannel;
                if (pDmaParams->stCommon.somethingToDo)
                    {
                    DEBUGLOG (DBG_TRACESIR, "%s - H2C Ch %u\n", __FUNCTION__, dwChannel);

                    spin_lock_irqsave (&stIRQLock, dwIRQLockFlags);
                    pDmaParams->stCommon.somethingToDo = false;
                    spin_unlock_irqrestore (&stIRQLock, dwIRQLockFlags);

                    pDmaParams->lCnt = 0;
                    if (bCheckDMAChannel (pstCard, pDmaParams, true))
                        pstCard->bS2CDMAActive = true;
                    }
                }

            // ----- wake up user thread by C2S event -----
            if (pstCard->bC2SDMAActive)
                {
                pstCard->bC2SDMAActive = false;
                
                // ----- signal C2S DMA -----
    //            if (pstCard->wqC2SEvent) // TODO: Windows hat hier ein if()
                //wake_up (&pstCard->wqC2SEvent);
                pstCard->bWakeUp = 1;
                wake_up_interruptible (&pstCard->wqKernelEvent);
                }

            // ----- wake up user thread by S2C event -----
            if (pstCard->bS2CDMAActive)
                {
                pstCard->bS2CDMAActive = false;
                
                // ----- signal S2C DMA -----
    //            if (pstCard->wqS2CEvent) // TODO: Windows hat hier ein if()
                //wake_up (&pstCard->wqS2CEvent);
                pstCard->bWakeUp = 1;
                wake_up_interruptible (&pstCard->wqKernelEvent);
                } 

            // ----- user interrupt -----
            if (pstCard->bUserIntActive)
                {
                pstCard->bUserIntActive = false;

    //            if (pstCard->wqUserEvent) // TODO: Windows hat hier ein if()
                //wake_up (&pstCard->wqUserEvent);

                pstCard->bWakeUp = 1;
                wake_up_interruptible (&pstCard->wqKernelEvent);
                }
            }
        }
    }




/*
**************************************************************************
ConnectISR
**************************************************************************
*/

int nConnectISR (SPCM_ST_CARDINFO* pstCard)
    {
    int     nErr;

    if (pstCard->eDMACore == XDMA)
        {
        // MSI-X
        int i = 0, j = 0, lVector;
        XDMA_DMA_PARAMS* pDMAParam = NULL;

        // H2C
        for (i = 0; i < SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS; ++i)
            {
            DEBUGLOG(DBG_TRACE, "Connecting H2C ISR\n");

#   if (LINUX_VERSION_CODE >=KERNEL_VERSION(4,8,0))
            lVector = pci_irq_vector (pstCard->pstPCIDevice, i);
#else
            lVector = pstCard->astMSIXEntries[i].vector;
#endif
            nErr = request_irq (lVector, vInterruptService, 0, SPCM_DEVICENAME, pstCard);
            if (nErr < 0)
                {
                DEBUGLOG (DBG_ERROR, "Interrupt Handler could not be installed for H2C %d i irq %d\n", i, lVector);
                return nErr;
                }
            pDMAParam = pstCard->astXDMA_S2CDMAParams + i;
            pDMAParam->stCommon.lIRQ = lVector;
            }

        // C2H
        j = SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS;
        for (i = 0; i < SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS; ++i)
            {
            DEBUGLOG(DBG_TRACE, "Connecting C2H ISR\n");
#   if (LINUX_VERSION_CODE >=KERNEL_VERSION(4,8,0))
            lVector = pci_irq_vector (pstCard->pstPCIDevice, j + i);
#else
            lVector = pstCard->astMSIXEntries[j + i].vector;
#endif
            nErr = request_irq (lVector, vInterruptService, 0, SPCM_DEVICENAME, pstCard);
            if (nErr < 0)
                {
                DEBUGLOG (DBG_ERROR, "Interrupt Handler could not be installed for C2H %d irq %d\n", i, lVector);
                return nErr;
                }
            pDMAParam = pstCard->astXDMA_C2SDMAParams + i;
            pDMAParam->stCommon.lIRQ = lVector;
            }

        // user
        j = SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS + SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS;
        for (i = 0; i < SPCM4DRV_XDMA_NUMBER_OF_USR_IRQ; ++i)
            {
            DEBUGLOG(DBG_TRACE, "Connecting USR ISR\n");
#   if (LINUX_VERSION_CODE >=KERNEL_VERSION(4,8,0))
            lVector = pci_irq_vector (pstCard->pstPCIDevice, j + i);
#else
            lVector = pstCard->astMSIXEntries[j + i].vector;
#endif
            nErr = request_irq (lVector, vInterruptService_XDMA_USR, 0, SPCM_DEVICENAME, pstCard); // use custom ISR for user interrupts
            if (nErr < 0)
                {
                DEBUGLOG (DBG_ERROR, "Interrupt Handler could not be installed for USR %d irq %d\n", i, lVector);
                return nErr;
                }
            pstCard->adwInstalledIRQ[i] = lVector;
            }
        }
    else
        {
        // connect interrupt service routine with interrupt
        DEBUGLOG (DBG_TRACE, "Install interrupt handler for irq %d\n", pstCard->pstPCIDevice->irq);

        // connect the ISR
        pstCard->adwInstalledIRQ[0] = pstCard->pstPCIDevice->irq;
        nErr = request_irq (pstCard->adwInstalledIRQ[0], vInterruptService, SA_SHIRQ, SPCM_DEVICENAME, (void*) pstCard);
        if (nErr < 0)
            {
            DEBUGLOG (DBG_ERROR, "Interrupt Handler could not be installed for irq %d\n", pstCard->adwInstalledIRQ[0]);
            return nErr;
            }
        }

    // kernel 2.6.20 using new work queue installation
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)) 
     INIT_WORK (&pstCard->stISRWork, vISRBottomHalf);

    // kernel 2.6 prior to 2.6.20 using old work queue installtion
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
    INIT_WORK (&pstCard->stISRWork, vISRBottomHalf, (void*) pstCard);

    // kernel 2.4 using task queue
#else
	pstCard->stISRTask.sync = 0;
	pstCard->stISRTask.routine = vISRBottomHalf;
	pstCard->stISRTask.data = (void*) pstCard;
#endif


    if (pstCard->eDMACore == NWC)
        NWD_EnableLocalInt ((unsigned char*)(pstCard->apdwMemMappedAddress[0]));
    else
        {
        XDMA_EnableLocalInt ((unsigned char*)(pstCard->apdwMemMappedAddress[1]));
        XDMA_WriteByOffset (pstCard->apdwMemMappedAddress[1], XDMA_TARGET_IRQBLOCK + XDMA_REG_IRQBLOCK_CH_VECTOR_NUMBER_30,  ((2U << 16) | (1U << 8) | (0U << 0))); // DMA interrupts are 0, 1, 2
        XDMA_WriteByOffset (pstCard->apdwMemMappedAddress[1], XDMA_TARGET_IRQBLOCK + XDMA_REG_IRQBLOCK_USR_VECTOR_NUMBER_30, ((5U << 16) | (4U << 8) | (3U << 0))); // USR interrupts are 3, 4, 5
        }

    return nErr;
    }


/*
**************************************************************************
DisconnectISR
**************************************************************************
*/

void vDisConnectISR (SPCM_ST_CARDINFO* pstCard)
    {

    if (pstCard->eDMACore == XDMA)
        {
        int i = 0;

        XDMA_DisableInt ((unsigned char*)pstCard->apdwMemMappedAddress[1]);

        // MSI-X

        // H2C
        for (i = 0; i < SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS; ++i)
            {
            XDMA_DMA_PARAMS* pDMAParam = pstCard->astXDMA_S2CDMAParams + i;
            DEBUGLOG (DBG_TRACE, "Free interrupt handler for irq %d\n", pDMAParam->stCommon.lIRQ);
            free_irq (pDMAParam->stCommon.lIRQ, pstCard);
            }

        // C2H
        for (i = 0; i < SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS; ++i)
            {
            XDMA_DMA_PARAMS* pDMAParam = pstCard->astXDMA_C2SDMAParams + i;
            DEBUGLOG (DBG_TRACE, "Free interrupt handler for irq %d\n", pDMAParam->stCommon.lIRQ);
            free_irq (pDMAParam->stCommon.lIRQ, pstCard);
            }

        // user
        for (i = 0; i < SPCM4DRV_XDMA_NUMBER_OF_USR_IRQ; ++i)
            {
            DEBUGLOG (DBG_TRACE, "Free interrupt handler for irq %d\n", pstCard->adwInstalledIRQ[i]);
            free_irq (pstCard->adwInstalledIRQ[i], (void*) pstCard);
            }
        }
    else
        {
        DEBUGLOG (DBG_TRACE, "Free interrupt handler for irq %d\n", pstCard->adwInstalledIRQ[0]);

        // disable interrupts on card
        NWD_DisableInt ((unsigned char*)pstCard->apdwMemMappedAddress[0]);

        free_irq (pstCard->adwInstalledIRQ[0], (void*) pstCard);
        }
    }


/*
**************************************************************************
CheckDMAChannel
**************************************************************************
*/

bool bCheckDMAChannel (SPCM_ST_CARDINFO* pstCard, void* pvDMAParams, bool bFromDPC)
    {
    bool bWakeUp = false;
    uint32 dwBlockNumber;

    if (pstCard->eDMACore == NWC)
        {
        NWCORE_SGLIST_ENTRY* pNwcSGListCurrentEntry = NULL;

        NWC_DMA_PARAMS* pstDMAParams = pvDMAParams;

        // set pointer to BAR0 memory region
        uint32* pdwBAR0Mem = pstCard->apdwMemMappedAddress[0];
        volatile uint32 dwEngCntl;

        if (down_interruptible (&pstDMAParams->stCommon.semAccess))
            return false;

        if (!pstDMAParams->stCommon.active)
            {
            up (&pstDMAParams->stCommon.semAccess);
            return false;
            }

        // current buffer consists of <dwBlockNumber> blocks
        dwBlockNumber = pstDMAParams->stCommon.dmaSGListElements;

        // init pointer to currently first entry in list
        pNwcSGListCurrentEntry = pstDMAParams->pNwcSGListCurrentFirstTestEntry;

        // read ENGCNTRL register
        dwEngCntl = NWD_ReadByOffset (pdwBAR0Mem, pstDMAParams->stCommon.dwEngAddrOffs + NWD_ENGCNTRL);

        // search for first entry with COMPLETE flag
        while ((pNwcSGListCurrentEntry->dwStatus & NWD_S2CDESC_STATUS_FLAG_COMPLETE) != NWD_S2CDESC_STATUS_FLAG_COMPLETE)
            {
            if (pNwcSGListCurrentEntry->entryNumber2 == pstDMAParams->stCommon.dmaSGListElements)
                pNwcSGListCurrentEntry = pstDMAParams->pNwcSGListStartEntry;
            else
                pNwcSGListCurrentEntry = pstGetNWCSGListEntry (pstDMAParams, pNwcSGListCurrentEntry->entryNumber2); // no +1 because entryNumer2 starts with 1, andSGListEntry is zero-based

            // search list only once
            if (pNwcSGListCurrentEntry == pstDMAParams->pNwcSGListCurrentFirstTestEntry)
                break;
            }

        // ----- check number of blocks that have been completely transfered (NWD_S2CDESC_STATUS_FLAG_COMPLETE) -----
        // ----- and are ready to read or re-write -----
        // ----- check dwBlockNumber at max! -----
        // ----- delete block number of emptied blocks to mark it as checked -----
        //SGListDump2 (pstCard, pstDMAParams);

        while (dwBlockNumber--)
            {

            // if data of current block is incomplete, we can abort
            if ((pNwcSGListCurrentEntry->dwStatus & NWD_S2CDESC_STATUS_FLAG_COMPLETE) != NWD_S2CDESC_STATUS_FLAG_COMPLETE)
                {
                break;
                }
            

            // ----- if block is not marked as checked, add its length to number of available bytes and mark block as checked -----
            if (pNwcSGListCurrentEntry->entryNumber != 0)
                {
                pstDMAParams->stCommon.qwBytesTransfered += pNwcSGListCurrentEntry->dwStatus & NWD_S2CDESC_BYTECOUNT_MASK;

                // mark as empty
                pNwcSGListCurrentEntry->entryNumber = 0;

                // reset COMPLETE
                pNwcSGListCurrentEntry->dwStatus &= ~NWD_S2CDESC_STATUS_FLAG_COMPLETE;

#ifdef DMA_BOUNCE_BUFFER
                if (!pstDMAParams->stCommon.writeToDevice)
                    {
                    // force sync of bounce buffer to user buffer
                    dma_sync_single_for_cpu (&pstCard->pstPCIDevice->dev, QWORD_FROM_DWORD(pNwcSGListCurrentEntry->pciAddrHigh, pNwcSGListCurrentEntry->pciAddrLow), pNwcSGListCurrentEntry->dwStatus & NWD_S2CDESC_BYTECOUNT_MASK, DMA_FROM_DEVICE);
                    }
#endif
                }

            // move pointer to next entry
            if (pNwcSGListCurrentEntry->entryNumber2 == pstDMAParams->stCommon.dmaSGListElements)
                pNwcSGListCurrentEntry = pstDMAParams->pNwcSGListStartEntry;
            else
                pNwcSGListCurrentEntry = pstGetNWCSGListEntry (pstDMAParams, pNwcSGListCurrentEntry->entryNumber2); // no +1 because entryNumber2 starts with 1, andSGListEntry is zero-based
            }

        // store new check start entry
        pstDMAParams->pNwcSGListCurrentFirstTestEntry = pNwcSGListCurrentEntry;

        // check if DMA is done and we need to restart it
        SPCM4DRV_NWC_DMA_Restart (pstCard, pstDMAParams, true);

        if (pstDMAParams->stCommon.qwBytesTransfered > 0)
            bWakeUp = true;

        // release lock
        up (&pstDMAParams->stCommon.semAccess);
        }
    else
        {

        XDMA_DMA_PARAMS* pstXDMADMAParams = pvDMAParams;
        // set pointer to BAR1 memory region
        uint32* pdwBAR1Mem = pstCard->apdwMemMappedAddress[1];
        XDMA_SGLIST_ENTRY* pXDMASGListCurrentEntry = NULL;

        if (bFromDPC) // coming from DMA_Restart we already have this lock
            {
            if (down_interruptible (&pstXDMADMAParams->stCommon.semAccess))
                return false;
            }

        // init pointer to currently first entry in list
        pXDMASGListCurrentEntry = pstXDMADMAParams->pXDMASGListCurrentSGListStart;

        if (pstXDMADMAParams->dwNumProcessedCompletedDesc != pstXDMADMAParams->stCommon.dmaSGListElements)
            {
            // Read channel ‘completed descriptor count’
            // contains the number of descriptors since the last start, NOT the last read of this register!
            uint32 dwCompleteDescCnt = XDMA_ReadByOffset (pdwBAR1Mem, pstXDMADMAParams->stCommon.dwEngCtrlAddrOffs + XDMA_REG_CHANNEL_COMPL_DESC_CNT);

            uint32 dwFirstTestIdx = dwGetIndexOfXDMASGListEntry (pstXDMADMAParams, pstXDMADMAParams->pXDMASGListCurrentFirstTestEntry);
            uint32 dwCurrentFirstEntryIdx = dwGetIndexOfXDMASGListEntry (pstXDMADMAParams, pXDMASGListCurrentEntry);
            DEBUGLOG (DBG_TRACE, "DPC: NumDesc: %u NumComplDesc: %u FirstTestIdx: %u\n", dwCompleteDescCnt, pstXDMADMAParams->dwNumProcessedCompletedDesc, dwFirstTestIdx);
            while (dwCompleteDescCnt--)
                {
                // when this descriptor is newly completed
                //DEBUGLOG (DBG_NONE, "CurEntry: %p  FirstTestEntry: %p\n", pXDMASGListCurrentEntry, pstXDMADMAParams->pXDMASGListCurrentFirstTestEntry);
                if (pXDMASGListCurrentEntry == pstXDMADMAParams->pXDMASGListCurrentFirstTestEntry)
                    {
                    //DEBUGLOG (DBG_NONE, "Desc: %u  Len: %u\n", dwCompleteDescCnt + 1, pXDMASGListCurrentEntry->dwLength);

                    // for S2C we use the length of the SG descriptor
                    if (pstXDMADMAParams->stCommon.writeToDevice)
                        pstXDMADMAParams->stCommon.qwBytesTransfered += pXDMASGListCurrentEntry->dwLength;
                    else
                        {
                        // for C2S we might have incomplete descriptors when the transfer has finished (e.g. small number of timestamps)
                        // so we use the meta data writeback mechanism
                        XDMA_C2H_WRITEBACK* pWriteBack = pstGetXDMAWriteBackEntry (pstXDMADMAParams, dwFirstTestIdx);
                        pstXDMADMAParams->stCommon.qwBytesTransfered += pWriteBack->dwLength;

#ifdef DMA_BOUNCE_BUFFER
                        // force sync of bounce buffer to user buffer
                        dma_sync_single_for_cpu (&pstCard->pstPCIDevice->dev, QWORD_FROM_DWORD(pXDMASGListCurrentEntry->dwDestAddrHigh, pXDMASGListCurrentEntry->dwDestAddrLow), pXDMASGListCurrentEntry->dwLength, DMA_FROM_DEVICE);
#endif
                        }

                    pstXDMADMAParams->dwNumProcessedCompletedDesc++;
                    if (pstXDMADMAParams->pXDMASGListCurrentFirstTestEntry == pstXDMADMAParams->pXDMASGListLastEntry)
                        {
                        pstXDMADMAParams->pXDMASGListCurrentFirstTestEntry = pstXDMADMAParams->pXDMASGListStartEntry;
                        dwFirstTestIdx = 0;
                        }
                    else
                        {
                        dwFirstTestIdx++;
                        pstXDMADMAParams->pXDMASGListCurrentFirstTestEntry = pstGetXDMASGListEntry (pstXDMADMAParams, dwFirstTestIdx);
                        }
                    }

                // move pointer to next entry
                if (pXDMASGListCurrentEntry == pstXDMADMAParams->pXDMASGListLastEntry)
                    {
                    pXDMASGListCurrentEntry = pstXDMADMAParams->pXDMASGListStartEntry;
                    dwCurrentFirstEntryIdx = 0;
                    }
                else
                    {
                    dwCurrentFirstEntryIdx++;
                    pXDMASGListCurrentEntry = pstGetXDMASGListEntry (pstXDMADMAParams, dwCurrentFirstEntryIdx);
                    }
                }
            }
        DEBUGLOG (DBG_TRACE, "DPC: BytesTransfered: %llu \n", pstXDMADMAParams->stCommon.qwBytesTransfered);

        // check if DMA is done and we need to restart it
        SPCM4DRV_XDMA_DMA_Restart (pstCard, pstXDMADMAParams, true);

        if (pstXDMADMAParams->stCommon.qwBytesTransfered > 0)
            bWakeUp = true;

        // release lock
        if (bFromDPC)
            up (&pstXDMADMAParams->stCommon.semAccess);
        }

    return bWakeUp;
    }


