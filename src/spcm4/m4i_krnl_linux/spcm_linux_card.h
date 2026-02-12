#ifndef SPCM_LINUX_CARD_H
#define SPCM_LINUX_CARD_H

/*
**************************************************************************

spcm_linux_card.h                             (c) Spectrum GmbH,  07/2006

**************************************************************************

Card information structure and card access functions

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
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/major.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#ifdef HAVE_UNLOCKED_IOCTL
#   if (LINUX_VERSION_CODE < 0x020627) // 2.6.39
#       include <linux/smp_lock.h>
#   endif
#endif

#include "../c_header/dlltyp.h"

#include "../m2i_krnl/spcm_linux_ioctl.h"
#include "../m4i_krnl_common/dma-nwc.h"
#include "../m4i_krnl_common/dma-xdma.h"

#ifdef USE_CUDA_RDMA
#   include "../m4i_krnl_common/spcm_cuda.h"
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION (2,6,18))
typedef unsigned long resource_size_t;
#endif

// ----- defines for the driver -----
#define SPCM_DEVICENAME "spcm"

// Same values from DLL-driver project: user-logic register addresses and values
#define BASEB_WR_CONFIG_MEM             0x0100
#define BASEB_MEM                       BASEB_WR_CONFIG_MEM

#define BASEB_RD_MEM_STATUS         (BASEB_MEM + 16 *  0)
#define     RD_MS_DATA_DMA_STOPPED      0x00100000
#define     RD_MS_TS_DMA_STOPPED        0x00200000
#define     RD_MS_ABA_DMA_STOPPED       0x00400000

#define BASEB_RD_MEM_BYTE_COUNT     (BASEB_MEM + 16 *  2)

#define M5REG_RD_MEM_STATUS_ISR     (BASEB_MEM + 4 * 1)
#define M5REG_RD_MEM_BYTE_COUNT     (BASEB_MEM + 4 * 2)

typedef enum {eDMA_Empty, eDMA_Mapped, eDMA_Active, eDMA_ISRWork, eDMA_Stop} EDMASTATE;

typedef enum { NWC, XDMA } DMA_CORE;

// ----- information structure for one device -----
typedef struct
    {
    struct pci_dev*         pstPCIDevice;           // the corresponding PCI device
    struct semaphore        semDrvAccess;           // semaphore to avoid concurrent access to kernel driver
    int8                    bWakeUp;                // flag for the kernel event
	wait_queue_head_t	    wqKernelEvent;
//	wait_queue_head_t	    wqC2SEvent;
//	wait_queue_head_t	    wqS2CEvent;
//	wait_queue_head_t	    wqUserEvent;
    int8                    bCardOpen;              // card is open and can process interrupts

    int32                   lIdx;                   // index of card (minor number)
    //PSTPLX9656CFG           pstPLXAdr;              // PLX configuration address (remapped)
    uint32*                 pdwLocalAdr;            // local memory address
    int8                    bLocalIRQEnable;        // local IRQ used
    int8                    bDMAIRQEnable;          // DMA IRQ used
#if (LINUX_VERSION_CODE >= 0x020600)
    struct work_struct      stISRWork;              // workqueue for bottom half of interrupt service
#else
	struct tq_struct        stISRTask;
#endif
    bool                    bC2SDMAActive;
    bool                    bS2CDMAActive;
    bool                    bUserIntActive;

    // dma data for all c2s and s2c channels
    DMA_CORE                eDMACore;
    NWC_DMA_PARAMS astNWC_C2SDMAParams[SPCM4DRV_DMA_MAX_CHANNEL_NUM_PER_DIR];
    NWC_DMA_PARAMS astNWC_S2CDMAParams[SPCM4DRV_DMA_MAX_CHANNEL_NUM_PER_DIR];

    XDMA_DMA_PARAMS astXDMA_C2SDMAParams[SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS];
    XDMA_DMA_PARAMS astXDMA_S2CDMAParams[SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS];

    // card information for display with procmem
    int32                   lDevSpcmNumber;         // the X in /dev/spcmX
    int32                   lCardType;              // translated card type
    int32                   lSN;                    // serial number
    uint64                  qwMem;                  // installed memory as Bytes
    int32                   lProdWeek, lProdYear;   // production date and month
    uint32                  dwBoardNumber;

    // list of used resources for clean-up
    uint32                  adwInstalledIRQ[SPCM4DRV_XDMA_NUMBER_OF_USR_IRQ];         // installed IRQ for removing (entry in PCIDevice is overwritten with rerouting)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0))
    struct msix_entry       astMSIXEntries[SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS + SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS + SPCM4DRV_XDMA_NUMBER_OF_USR_IRQ];
#endif
    uint32                  dwMemResCnt;
    resource_size_t	        adwMemPhysAddress[6];
    uint32                  adwMemLength[6];
    uint32*                 apdwMemMappedAddress[6];

    // misc details, control and status
    uint32                  dwOpenProcessId;        // process id that has opened the driver and has now exclusive access

    // continuous memory
    void*                   pvContMemBuffer;        // continuous buffer for data transfer
    uint64                  qwContMemLen;           // ... length of it
#ifdef SPCM4_USE_CMA
    dma_addr_t              hContMem;               // handle to continuous buffer for dma_free_coherent
#endif
    uint64                  qwContMemUserStart;     // start adr of the corresponding user address space (for SGListFill)
    uint64                  qwContMemUserLen;       // length of the corresponding user address space

    uint8                   byPCIBus;
    uint8                   byPCIDev;
    uint8                   byPCIFunc;

#ifdef USE_CUDA_RDMA
    struct nvidia_p2p_page_table* pstCudaPageTable;
    struct nvidia_p2p_dma_mapping* pstCudaDmaMap;
#endif

    } SPCM_ST_CARDINFO, *SPCM_PST_CARDINFO;


void vDebugDumpCardInfo (SPCM_ST_CARDINFO* pstCardInfo);


// ----- card functions that do all the work based on the card structure -----
SPCM_ST_CARDINFO* card_pstNew (struct pci_dev *pstDevice, int lIdx);
int card_lOpen              (SPCM_ST_CARDINFO* pstCard);
int card_lClose             (SPCM_ST_CARDINFO* pstCard);

void card_vReadSingle       (SPCM_ST_CARDINFO* pstCard, SPCM_IOCTL_RWSINGLE* pstRWSingle);
void card_vWriteSingle      (SPCM_ST_CARDINFO* pstCard, SPCM_IOCTL_RWSINGLE* pstRWSingle);
void card_vReadWriteList    (SPCM_ST_CARDINFO* pstCard, SPCM_IOCTL_RWLIST* pstRWList);
int card_lReadCardStatus   (SPCM_ST_CARDINFO* pstCard, uint32 dwDMAChannel, SPCM_IOCTL_CARDSTATUS* pstCardStatus);
COMMON_DMA_PARAMS* card_pstGetDMAParams (SPCM_ST_CARDINFO* pstCard, uint32 dwDMAChannel);
bool card_bDMAChannelDirIsWrite (uint32 dwDMAChannel);

#endif // SPCM_LINUX_CARD_H

