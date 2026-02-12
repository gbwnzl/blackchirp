/*
**************************************************************************

spcm_linux_main.c                              (c) Spectrum GmbH,  07/2006

**************************************************************************

main file of the Spectrum Linux Kernel driver for all M4i/M4x/M2p/M5i cards.
Handles the kernel driver interface and all ioctl or read/write calls.

**************************************************************************

The complete Spectrum Drivers including the Linux Kernel driver are
under copyright of Spectrum Instrumentation GmbH,
Germany. The source code is NOT under the GPL. It is only allowed to use
this source code or parts of this source code to operate Spectrum cards
under Linux with it.

The customer is not authorized to make the Source Code or parts of it
available to other people or to publish any Source Code data. In addition
the customer is not allowed to use any Source Code data or parts of it for
private use. The customer accepts these conditions. He is obliged to delete
all data as soon as the project is finished and to handle all information
obtained under this agreement confidentially even if the project is finished.

Any modifications to this souce code are done on own risk and are not
supported by the Spectrum Support Team. Please contact Support@spec.de if
you encounter a problem with the driver.

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
#else
// HAVE_UNLOCKED_IOCTL has only been removed in vanilla kernel starting with 5.9
// but openSUSE Leap 15.3 does not define HAVE_UNLOCKED_IOCTL, although it uses kernel 5.3
// so we define it ourselves on kernel 5.0 upwards
#   if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
#       define HAVE_UNLOCKED_IOCTL 1
#   endif
#endif
#if (LINUX_VERSION_CODE >= 0x030A00) // 3.10.0
#   include <linux/seq_file.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0))
#   include <linux/uaccess.h>
#else
#   include <asm/uaccess.h>
#endif
#include <asm/page.h> /* PAGE_SIZE */
#include <asm/io.h>


// ----- public Spectrum driver includes -----
#include "../c_header/dlltyp.h"
#include "../c_header/regs.h"
#include "../c_header/spcerr.h"


// ----- internal Spectrum driver includes -----
#include "../drv_comm/plx9060.h"
#include "../drv_comm/plxmap.h"
#include "../drv_comm/spcdevid.h"
#include "../m2i_tools/spcm_eeprom_codes.h"


// ----- kernel driver includes -----
#include "../m2i_krnl/spcm2_krnl_general.h"
#include "spcm_linux_wrapper.h"
#include "spcm_linux_debug.h"
#include "../m2i_krnl/spcm_linux_ioctl.h"
#include "spcm_linux_card.h"
#include "spcm_linux_isr.h"
#include "../m2i_krnl/spcm_drv_interface.h"
#include "../m2i_krnl/spcm4_kdrv_lx_version.h"

#include "../m4i_krnl_common/dma-nwc.h"
#include "../m4i_krnl_common/dma-xdma.h"
#include "dmafunctions.h"

#define NAMEBUFFER_LENGTH (4 + ((MAXBRD/100 == 0)? 2 : 3) + 1) // "spcm", two or three digits depending on MAXBRD, one for \0

// function to check if kernel supports filesystem
extern struct file_system_type* get_fs_type(const char *fs_name);

// ----- global variables for the driver -----
static int g_nCardCount =   0;
static int g_nUseSysfs =    0;
static int g_nMajor =       0;
static SPCM_ST_CARDINFO* g_ppstCard[MAXBRD];
static struct miscdevice* g_ppstMiscDev[MAXBRD];
static char* g_ppszNameBuffer[MAXBRD];

typedef struct _MINOR_INDEX_MAP
    {
    int lMinor;
    int lIndex;
    } MINOR_INDEX_MAP, *PMINOR_INDEX_MAP;
PMINOR_INDEX_MAP g_ppstMinorIndexMap[MAXBRD];

extern spinlock_t stIRQLock;


// ----- table of supported PCI devices, linked externally as this is generated automatically -----
#include "spcm_pci_devices.h"
extern struct pci_device_id g_stPCIIds[];
MODULE_DEVICE_TABLE(pci, g_stPCIIds);

MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("Spectrum GmbH, Germany");
MODULE_DESCRIPTION("Universal driver for all Spectrum M4i, M4x, M2p and M5i cards.");
#if (LINUX_VERSION_CODE < 0x020600)
    #define MODULE_VERSION RSC_STR_SPCM4_KDRV_VERSION
#else
    MODULE_VERSION(RSC_STR_SPCM4_KDRV_VERSION);
#endif


// ----- add module params -----
static int contmem_mb = 0;      // continuous memory in MB

#if (LINUX_VERSION_CODE < 0x020605)
    MODULE_PARM(contmem_mb,"i");
#else
    module_param(contmem_mb,  int, 0);
#endif
MODULE_PARM_DESC(contmem_mb, " Continuous memory in MB per card that should be allocated by the kernel module. Default: 0 MB");



/*
*****************************************************************************
file operations map
*****************************************************************************
*/

int spcm4_lOpen (struct inode *pstInode, struct file *pstFilp);
int spcm4_lClose (struct inode *pstInode, struct file *pstFilp);
#if defined (HAVE_UNLOCKED_IOCTL)
long spcm4_lIoCtl_Unlocked (struct file *pstFilp, unsigned int dwCmd, unsigned long dwArg);
#endif
int spcm4_lIoCtl (struct inode *pstInode, struct file *pstFilp, unsigned int dwCmd, unsigned long dwArg);
int spcm4_lMMap (struct file *pstFilp, struct vm_area_struct* pstVma);

struct file_operations g_stSpcmFops =
    {
    llseek:     0,
    read:       0,
    write:      0,
#if (LINUX_VERSION_CODE < 0x030B00)
    readdir:    0,
#endif
    poll:       0,
#if defined (HAVE_UNLOCKED_IOCTL)
    unlocked_ioctl: spcm4_lIoCtl_Unlocked,
#else
    ioctl:      spcm4_lIoCtl,
#endif
    mmap:       spcm4_lMMap,
    open:       spcm4_lOpen,
    flush:      0,
    release:    spcm4_lClose,
    fasync:     0
    };

#define NUM_PCI_RESOURCES 3 // for M4i eval board

/*
**************************************************************************
lProbe function: invoked if the driver is loaded for one device. Does
                 global initialisation and requests memory and io areas
**************************************************************************
*/

static int spcm4_lProbe (struct pci_dev *pstPCIDevice, const struct pci_device_id *pstDevId)
    {
    int     i, nCardIdx;
    int     nRet = 0;
    uint16  wDevId;
    SPCM_ST_CARDINFO* pstCard = NULL;

    DEBUGLOG (DBG_TRACE, "Kernel Driver Probe %p\n", pstPCIDevice);

    // first we search for an empty card slot and allocate all the information structure
    for (nCardIdx = 0; nCardIdx < MAXBRD; nCardIdx++)
        {
        if (!g_ppstCard[nCardIdx])
            {
            g_ppstCard[nCardIdx] = card_pstNew (pstPCIDevice, nCardIdx);
            pstCard = g_ppstCard[nCardIdx];
            break;
            }
        }

    // some error has occured
    if (!pstCard)
        {
        DEBUGLOG (DBG_ERROR, "card structure allocation failed\n");
        return -ENODEV;
        }


    // go through the ressources and request the memory regions
    for (i = 0; i < NUM_PCI_RESOURCES; i++)
        {
        resource_size_t dwStart = 0;
        uint32 dwFlag, dwLen;
        dwStart = pci_resource_start (pstPCIDevice, i);
        dwLen = pci_resource_end (pstPCIDevice, i) - dwStart + 1;
        dwFlag = pci_resource_flags (pstPCIDevice, i);

        // we only use the memory areas, i/o areas are ignored
        if (dwFlag & IORESOURCE_MEM)
            {
#ifdef _LINUX64
            DEBUGLOG (DBG_TRACE, "request_mem_region %llx\n", dwStart);
#else
            DEBUGLOG (DBG_TRACE, "request_mem_region %lx\n", dwStart);
#endif
            if (!request_mem_region (dwStart, dwLen, SPCM_DEVICENAME))
                {
#ifdef _LINUX64
                DEBUGLOG (DBG_ERROR, "request_mem_region %llx failed!\n", dwStart);
#else
                DEBUGLOG (DBG_ERROR, "request_mem_region %lx failed!\n", dwStart);
#endif
                kfree (pstCard);
                g_ppstCard[nCardIdx] = NULL;
                return -ENODEV;
                }

            // store the information for later release
            pstCard->adwMemPhysAddress[pstCard->dwMemResCnt]    = dwStart;
            pstCard->adwMemLength[pstCard->dwMemResCnt]         = dwLen;
            pstCard->apdwMemMappedAddress[pstCard->dwMemResCnt] = ioremap (dwStart, dwLen);
            pstCard->dwMemResCnt++;
            }
        }

    // read out some information and log it
    pci_read_config_word (pstPCIDevice, 0x02, &wDevId);

    pstCard->byPCIBus  = pstPCIDevice->bus->number;
    pstCard->byPCIDev  = PCI_SLOT(pstPCIDevice->devfn);
    pstCard->byPCIFunc = PCI_FUNC(pstPCIDevice->devfn);

    // search for card type from global driver list
    for (i = 0; pstDeviceTable[i].wVendorId != 0xffff; i++)
        if (pstDeviceTable[i].wDeviceId == wDevId)
            {
            pstCard->lCardType = pstDeviceTable[i].lBrdType;
            break;
            }

   switch (pstCard->lCardType & TYP_SERIESMASK) 
        {
        case TYP_M5IEXPSERIES: pstCard->eDMACore = XDMA; break;
        default:               pstCard->eDMACore = NWC;  break;
        }

    // we enable the PCI device (must be done before requesting the interrupt!)
    if ((nRet = pci_enable_device (pstPCIDevice)))
        {
        DEBUGLOG (DBG_ERROR, "pci_enable_device failed!");
        }
    else
        {


        if (pstCard->eDMACore == XDMA)
            {
            int lNumIRQ = SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS + SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS + SPCM4DRV_XDMA_NUMBER_OF_USR_IRQ;
            // enable MSI-X
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,8,0))
            nRet = pci_alloc_irq_vectors (pstPCIDevice, lNumIRQ, lNumIRQ, PCI_IRQ_MSIX);
            if (nRet < 0)
                DEBUGLOG(DBG_ERROR, "ERROR: pci_alloc_irq_vectors failed: %d\n", nRet);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0))
            for (i = 0; i < lNumIRQ; ++i)
                pstCard->astMSIXEntries[i].entry = i;
                
            nRet = pci_enable_msix_range (pstPCIDevice, pstCard->astMSIXEntries, lNumIRQ, lNumIRQ);
            if (nRet < 0)
                DEBUGLOG(DBG_ERROR, "ERROR: pci_enable_msix_range failed: %d\n", nRet);
            DEBUGLOG(DBG_ERROR, "pci_enable_msix_range -> : %d\n", nRet);
#else
            DEBUGLOG(DBG_ERROR, "no support for MSI-X in kernel\n");
            nRet = -1;
#endif
            }

        if (nRet >= 0)
            {
            pci_set_master (pstPCIDevice);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,18,0))
            if (dma_set_mask (&pstPCIDevice->dev, DMA_BIT_MASK(64)))
                {
                // kernel disables DAC mode for VIA PCI bridges in pci_dma.c
                DEBUGLOG (DBG_WARN, "DAC not available, trying normal mode\n");
                if (dma_set_mask (&pstPCIDevice->dev, DMA_BIT_MASK(32)))
                    DEBUGLOG (DBG_ERROR, "pci_set_dma_mask failed\n");
                }
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
            if (pci_set_dma_mask (pstPCIDevice, DMA_BIT_MASK(64)))
                {
                // kernel disables DAC mode for VIA PCI bridges in pci_dma.c
                DEBUGLOG (DBG_WARN, "DAC not available, trying normal mode\n");
                if (pci_set_dma_mask (pstPCIDevice, DMA_BIT_MASK(32)))
                    DEBUGLOG (DBG_ERROR, "pci_set_dma_mask failed\n");
                }
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22))
            if (pci_set_dma_mask (pstPCIDevice, DMA_64BIT_MASK))
                DEBUGLOG (DBG_ERROR, "pci_set_dma_mask failed\n");
#endif

            // attach the ISR
            nRet = nConnectISR (pstCard);
            }
        }

    // clean up in case of error
    if (nRet)
        {
        for (i = 0; i < (int) pstCard->dwMemResCnt; i++)
            release_mem_region (pstCard->adwMemPhysAddress[i], pstCard->adwMemLength[i]);

        kfree (pstCard);
        g_ppstCard[nCardIdx] = NULL;
        }

    // we count the cards that we've found and fill the matching miscdevice structure with meaningfull data
    else
        {
        if(g_nUseSysfs)
            {

            // ----- sysfs
            // allocate memory for miscdevice structure
            g_ppstMiscDev[g_nCardCount] = (struct miscdevice*) kmalloc( sizeof(struct miscdevice), GFP_KERNEL);
            if(g_ppstMiscDev[g_nCardCount] == NULL)
                {
                DEBUGLOG (DBG_ERROR, "allocation of miscdevice structure failed");
                return -ENODEV;
                }
            memset(g_ppstMiscDev[g_nCardCount], 0 , sizeof(struct miscdevice));

            //allocate memory for name buffer
            g_ppszNameBuffer[g_nCardCount] = (char*) kmalloc(NAMEBUFFER_LENGTH * sizeof(char), GFP_KERNEL);
            if(g_ppszNameBuffer[g_nCardCount] == NULL)
                {
                DEBUGLOG (DBG_ERROR, "allocation of buffer failed");
                kfree(g_ppstMiscDev[g_nCardCount]);
                return -ENODEV;
                }
            memset(g_ppszNameBuffer[g_nCardCount], 0 , NAMEBUFFER_LENGTH * sizeof(char));

            // allocate memory for mapping structure
            g_ppstMinorIndexMap[g_nCardCount] = (MINOR_INDEX_MAP*) kmalloc (sizeof(MINOR_INDEX_MAP), GFP_KERNEL);
            if(g_ppstMinorIndexMap[g_nCardCount] == NULL)
                {
                DEBUGLOG (DBG_ERROR, "allocation of Index Map failed");
                kfree(g_ppstMiscDev[g_nCardCount]);
                kfree(g_ppszNameBuffer[g_nCardCount]);
                return -ENODEV;
                }
            memset(g_ppstMinorIndexMap[g_nCardCount], 0 , sizeof(MINOR_INDEX_MAP));

            // set Miscdevice structure (name will be set right before misc_register() call)
            g_ppstMiscDev[g_nCardCount]->minor =    MISC_DYNAMIC_MINOR;
            g_ppstMiscDev[g_nCardCount]->fops =     &g_stSpcmFops;
            }

        // allocation of the continuous memory buffer if requested
        DEBUGLOG (DBG_TRACE, "contmem_mb call parameter = %d MB\n", contmem_mb);
        pstCard->qwContMemLen =         (uint64) contmem_mb * MEGA_B(1);
        pstCard->pvContMemBuffer =      NULL;
        pstCard->qwContMemUserStart =   0;
        pstCard->qwContMemUserLen =     0;
        if (pstCard->qwContMemLen)
            {
#ifdef SPCM4_USE_CMA
            dma_set_mask_and_coherent (&pstPCIDevice->dev, DMA_BIT_MASK(64));
#endif
            while ((pstCard->pvContMemBuffer == NULL) && (pstCard->qwContMemLen >= PAGE_SIZE))
                {
#ifdef SPCM4_USE_CMA
                pstCard->pvContMemBuffer = dma_alloc_coherent (&pstPCIDevice->dev, pstCard->qwContMemLen, &pstCard->hContMem, GFP_KERNEL);
#else
                uint32 dwPagesOrder = get_order ((unsigned long) pstCard->qwContMemLen);
                pstCard->pvContMemBuffer = (void*) __get_free_pages (GFP_KERNEL, dwPagesOrder);
#endif
                if (!pstCard->pvContMemBuffer)
                    pstCard->qwContMemLen /= 2;
                }
            if (!pstCard->pvContMemBuffer)
                {
                pstCard->qwContMemLen = 0;
                DEBUGLOG (DBG_ERROR, "Failed to allocate Continuous Memory\n");
                // TODO: note to user: set SPCM4_USE_CMA in Makefile, and pass cma=xyz as kernel boot option to reserve and use the CMA memory.
                //                     This allows larger continuous buffers, but probably only with root privileges for the calling process
                }
            else
                {
                //reserve the pages to allow mapping to user space
                // "long" is 64bit on x86_64!
                unsigned long dwVirtAdr;
                for (dwVirtAdr = (unsigned long) pstCard->pvContMemBuffer; dwVirtAdr < (unsigned long) (pstCard->pvContMemBuffer + pstCard->qwContMemLen); dwVirtAdr += PAGE_SIZE)
                    SetPageReserved (virt_to_page (dwVirtAdr));

                DEBUGLOG (DBG_ERROR, "Continuous Mem of %d MB sucessfully allocated\n", (uint32) (pstCard->qwContMemLen >> 20));
                } 
            }

        g_nCardCount++;
        }

    return nRet;
    }




/*
**************************************************************************
vRemove: invoked if a device (driver) is unloaded. Clean up
**************************************************************************
*/

static void spcm4_vRemove(struct pci_dev *pstPCIDevice)
    {
    int i;
    SPCM_ST_CARDINFO* pstCard = NULL;

    DEBUGLOG (DBG_TRACE, "Kernel Driver Remove %p\n", pstPCIDevice);

    // search for the matching entry in the global device list and free miscdevice structure and name buffer
    for (i=0; i<MAXBRD; i++)
        if (g_ppstCard[i])
            {
            if (g_ppstCard[i]->pstPCIDevice == pstPCIDevice)
                {
                pstCard = g_ppstCard[i];
                g_ppstCard[i] = NULL;

                if(g_nUseSysfs)
                    {

                    //kfree checks for NULL itself, although these pointers should not be NULL
                    kfree(g_ppstMiscDev[i]);
                    kfree(g_ppszNameBuffer[i]);
                    kfree(g_ppstMinorIndexMap[i]);
                    }
                break;
                }
            }


    // free all resources
    if (pstCard)
        {

        // free the interrupt handler
        vDisConnectISR (pstCard);

        // free MSI-X
        if ((pstCard->lCardType & TYP_SERIESMASK) == TYP_M5IEXPSERIES)
            {
#   if (LINUX_VERSION_CODE >=KERNEL_VERSION(4,8,0))
            pci_free_irq_vectors (pstPCIDevice);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0))
            pci_disable_msix (pstPCIDevice);
#endif
            }

        for (i = 0; i < (int) pstCard->dwMemResCnt; i++)
            {
            iounmap (pstCard->apdwMemMappedAddress[i]);
            release_mem_region (pstCard->adwMemPhysAddress[i], pstCard->adwMemLength[i]);
            }

        // free the continuous memory buffer if it was allocated
        if (pstCard->pvContMemBuffer)
            {
#ifndef SPCM4_USE_CMA
            uint32 dwPagesOrder = get_order ((uint32) pstCard->qwContMemLen);
#endif

            // clear the page reservation
            // "long" is 64bit on x86_64!
            unsigned long dwVirtAdr;
            for (dwVirtAdr = (unsigned long) pstCard->pvContMemBuffer; dwVirtAdr < (unsigned long) (pstCard->pvContMemBuffer + pstCard->qwContMemLen); dwVirtAdr += PAGE_SIZE)
                ClearPageReserved (virt_to_page (dwVirtAdr));

#ifdef SPCM4_USE_CMA
            dma_free_coherent (&pstPCIDevice->dev, pstCard->qwContMemLen, pstCard->pvContMemBuffer, pstCard->hContMem);
#else
            free_pages ((unsigned long) pstCard->pvContMemBuffer, dwPagesOrder);
#endif
            }

        kfree (pstCard);
        }

    // error: card object not found in global list ???
    else
        DEBUGLOG (DBG_ERROR, "card object not found for remove\n");

    pci_disable_device (pstPCIDevice);
    }



/*
**************************************************************************
global PCI driver structure that links all together
**************************************************************************
*/

static struct pci_driver g_stPCIDriver =
    {
    .name = "spcm4_cards",
    .id_table = g_stPCIIds,
    .probe = spcm4_lProbe,
    .remove = spcm4_vRemove,
    };



/*
**************************************************************************
Procmem: module information under /proc/spcm4_cards
**************************************************************************
*/

// ----- proc setup has been changed -----
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
static int spcm4_read_procmem (struct seq_file* pstFile, void* v)
    {
    int i = 0;

    seq_printf (pstFile, "\nSpectrum spcm driver interface for M4i/M4x/M2p/M5i cards\n");
    seq_printf (pstFile, "----------------------------------------------------------\n");
    seq_printf (pstFile, "Driver version:          %s\n", RSC_STR_SPCM4_KDRV_VERSION);
    if(g_nUseSysfs)
        seq_printf (pstFile, "Driver major number:     %d\n", MISC_MAJOR);
    else
        seq_printf (pstFile, "Driver major number:     %d\n", g_nMajor);

    // card information
    for (i = 0; i < MAXBRD; i++)
        {
        if (g_ppstCard[i])
            {
            seq_printf (pstFile, "\n/dev/spcm%d\n", g_ppstCard[i]->lDevSpcmNumber);
            switch (g_ppstCard[i]->lCardType & TYP_SERIESMASK)
                {
                case TYP_M4IEXPSERIES: seq_printf (pstFile, "     Card type:       M4i.%02xxx-x8 / M4x.%02xxx-x4\n", (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK) >> 8, (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK) >> 8); break;
                case TYP_M2PEXPSERIES: seq_printf (pstFile, "     Card type:       M2p.%02xxx-x4\n",  (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK) >> 8); break;
                case TYP_M5IEXPSERIES: seq_printf (pstFile, "     Card type:       M5i.%02xxx-x16\n", (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK) >> 8); break;
                default:               seq_printf (pstFile, "     Card type:       %08x\n",         (uint32)  g_ppstCard[i]->lCardType); break;
                }
/*
            // these informations are not available for the kernel driver anymore :(
            seq_printf (pstFile, "     Serial number:   %05d\n", g_ppstCard[i]->lSN);
            seq_printf (pstFile, "     Installed mem:   %d MByte\n", (int32) (g_ppstCard[i]->qwMem / 1024 / 1024));
            seq_printf (pstFile, "     Production week: %d of %d\n", g_ppstCard[i]->lProdWeek, g_ppstCard[i]->lProdYear);
*/
            }
        }

    return 0;
    }

static int spcm4_open_procmem (struct inode* pstInode, struct file* pstFile)
    {
    return single_open (pstFile, spcm4_read_procmem, NULL);
    }

#   if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0))
static const struct proc_ops g_stProcOps = 
    {
    .proc_open    = spcm4_open_procmem,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release
    };
#   else
static const struct file_operations g_stProcFileOps = 
    {
    .owner   = THIS_MODULE,
    .open    = spcm4_open_procmem,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release
    };
#   endif


#else
int spcm4_read_procmem (char *buf, char **start, off_t offset, int len, int* eof, void *data)
    {
    int i;

    len = 0;

    len += sprintf (buf + len, "\nSpectrum spcm driver interface for M4i/M4x/M2p/M5i cards\n");
    len += sprintf (buf + len, "------------------------------------------------\n");
    len += sprintf (buf + len, "Driver version:          %s\n", RSC_STR_SPCM4_KDRV_VERSION);
    if(g_nUseSysfs)
        len += sprintf (buf + len, "Driver major number:     %d\n", MISC_MAJOR);
    else
        len += sprintf (buf + len, "Driver major number:     %d\n", g_nMajor);

    // card information
    for (i = 0; i < MAXBRD; i++)
        if (g_ppstCard[i])
            {
            len += sprintf (buf + len, "\n/dev/spcm%d\n", g_ppstCard[i]->lDevSpcmNumber);
            switch (g_ppstCard[i]->lCardType & TYP_SERIESMASK)
                {
                case TYP_M4IEXPSERIES: len += sprintf (buf + len, "     Card type:       M4i.%02xxx-x8 / M4x.%02xxx-x4\n", (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK) >> 8, (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK) >> 8); break;
                case TYP_M2PEXPSERIES: len += sprintf (buf + len, "     Card type:       M2p.%02xxx-x4\n", (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK) >> 8); break;

                case TYP_M5IEXPSERIES: len += sprintf (buf + len, "     Card type:       M5i.%02xxx-x16\n", (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK) >> 8); break;
                default:               len += sprintf (buf + len, "     Card type:       %08x\n",           (uint32)  g_ppstCard[i]->lCardType); break;
                }
/*
            // these informations are not available for the kernel driver anymore :(
            len += sprintf (buf + len, "     Serial number:   %05d\n", g_ppstCard[i]->lSN);
            len += sprintf (buf + len, "     Installed mem:   %d MByte\n", (int32) (g_ppstCard[i]->qwMem / 1024 / 1024));
            len += sprintf (buf + len, "     Production week: %d of %d\n", g_ppstCard[i]->lProdWeek, g_ppstCard[i]->lProdYear);
*/
            }

    (*eof) = 1;

    return len;
    }
#endif


/*
**************************************************************************
Module initialisation and exit function
**************************************************************************
*/

static int __init spcm4_lInit (void)
    {
    int nErrCount;
    int i;
    int nResult = 0;
    DEBUGLOG (DBG_TRACE, "---------------------------------------\n");
    DEBUGLOG (DBG_TRACE, "Kernel Driver Init\n");

    // check whether kernel supports sysfs
    if (get_fs_type("sysfs"))
        {
        DEBUGLOG(DBG_TRACE, "kernel supports sysfs\n");
        g_nUseSysfs = 1;
        }

#ifdef _LINUX64
        DEBUGLOG (DBG_BOOT, "Spectrum 64 bit driver loaded. Version %s.\n", RSC_STR_SPCM4_KDRV_VERSION);
#   ifdef USE_CUDA_RDMA
        DEBUGLOG (DBG_BOOT, "Driver supports CUDA RDMA.");
#   endif
#else
        DEBUGLOG (DBG_BOOT, "Spectrum 32 bit driver loaded. Version %s.\n", RSC_STR_SPCM4_KDRV_VERSION);
#endif

    // init the global card structure, miscdevice structure and name buffer
    for (i=0; i<MAXBRD; i++)
        {
        g_ppstCard[i] = NULL;
        if(g_nUseSysfs)
            {
            g_ppstMiscDev[i] =      NULL;
            g_ppszNameBuffer[i] =   NULL;
            }
        }

    // try to register the PCI driver (that calls the probe function for every card matching a table entry)
    nErrCount = pci_register_driver (&g_stPCIDriver);
    if (nErrCount < 0)
        {
        DEBUGLOG (DBG_ERROR, "pci_register_driver failed error=%d\n", nErrCount);
        return -ENODEV;
        }

    // on kernel 2.6 the pci_register_driver function doesn't return the number of device
#if (LINUX_VERSION_CODE >= 0x020600)
    nErrCount = g_nCardCount;
#endif

    if (nErrCount == 0)
        {
        DEBUGLOG (DBG_ERROR, "no Spectrum M4i/M4x/M2p/M5i card found in the system\n");
        pci_unregister_driver (&g_stPCIDriver);
        return -ENODEV;
        }

    if (g_nUseSysfs)
        {

        // ----- sysfs
        // try to register the matching misc driver for each card
        int lDevSpcmNumber = 0;
        char szSysClassMisc[23];
        for (i = 0; i < g_nCardCount; i++)
            {
            if (nErrCount > 0)
                {
                // this driver and the M2i/M3i driver both register their devices as spcmXY, therefore we have to try and find the first available name
                struct file *fp;
                nResult = -1;
                while (nResult < 0 && lDevSpcmNumber <= 16)
                    {
                    sprintf (g_ppszNameBuffer[i], "%s%d", SPCM_DEVICENAME, lDevSpcmNumber);
                    g_ppstMiscDev[i]->name = g_ppszNameBuffer[i];

                    // check if entry in /sys exists for M2i driver's spcmX devices
                    sprintf (szSysClassMisc, "/sys/class/misc/%s", g_ppszNameBuffer[i]);
                    fp = filp_open (szSysClassMisc, O_RDONLY, 0);
                    if (IS_ERR(fp))
                        nResult = misc_register (g_ppstMiscDev[i]);
                    else
                        {
                        DEBUGLOG (DBG_TRACE, "%s - %s exists\n", __FUNCTION__, szSysClassMisc);
                        filp_close (fp, 0);
                        lDevSpcmNumber++;
                        }
                    }

                if (nResult < 0 )
                    {
                    DEBUGLOG (DBG_ERROR, "misc_register failed\n");
                    nErrCount = -EINVAL;
                    }
                else
                    {

                    // set map structure, needed because the automatic assignment of minor numbers does not start at zero and is not
                    g_ppstMinorIndexMap[i]->lMinor = g_ppstMiscDev[i]->minor;
                    g_ppstMinorIndexMap[i]->lIndex = i;
                    g_ppstCard[i]->lDevSpcmNumber = lDevSpcmNumber;

                    DEBUGLOG (DBG_TRACE, "misc_register successful, major: %d, minor: %i\n", MISC_MAJOR, g_ppstMiscDev[i]->minor);

                    switch (g_ppstCard[i]->lCardType & TYP_SERIESMASK)
                        {
                        case TYP_M4IEXPSERIES:
                            DEBUGLOG (DBG_BOOT, "/dev/spcm%d: M4i.%02xxx-x8\n",  g_ppstCard[i]->lDevSpcmNumber, (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK)>>8);
                            break;
                        case TYP_M4XEXPSERIES:
                            DEBUGLOG (DBG_BOOT, "/dev/spcm%d: M4x.%02xxx-x4\n",  g_ppstCard[i]->lDevSpcmNumber, (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK)>>8);
                            break;
                        case TYP_M2PEXPSERIES:
                            DEBUGLOG (DBG_BOOT, "/dev/spcm%d: M2p.%02xxx-x4\n",  g_ppstCard[i]->lDevSpcmNumber, (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK)>>8);
                            break;
                        case TYP_M5IEXPSERIES:
                            DEBUGLOG (DBG_BOOT, "/dev/spcm%d: M5i.%02xxx-x16\n", g_ppstCard[i]->lDevSpcmNumber, (uint32) (g_ppstCard[i]->lCardType & TYP_FAMILYMASK)>>8);
                            break;
                        default:
                            DEBUGLOG (DBG_BOOT, "/dev/spcm%d: unknown type %x\n", g_ppstCard[i]->lDevSpcmNumber, (uint32) g_ppstCard[i]->lCardType);
                            break;
                        }
                    }

                // clean up in case of error
                if (nErrCount <= 0)
                    {
                    if (nResult > 0)
                        misc_deregister (g_ppstMiscDev[i]);

                    pci_unregister_driver (&g_stPCIDriver);
                    break;
                    }
                }
            }
        }
    else
        {

        // no sysfs support
        // try to register the matching character driver
        if (nErrCount > 0)
            {
            g_nMajor = register_chrdev (0, SPCM_DEVICENAME, &g_stSpcmFops);
            if (g_nMajor < 0)
                {
                DEBUGLOG (DBG_ERROR, "register_chrdev failed");
                nErrCount = -EINVAL;
                }

            // clean up in case of error
            if (nErrCount <= 0)
                {
                if (g_nMajor > 0)
                    unregister_chrdev (g_nMajor, SPCM_DEVICENAME);
                pci_unregister_driver (&g_stPCIDriver);
                }
            }
        }

    // give some information in the driver
    if (nErrCount > 0)
        {
#if (LINUX_VERSION_CODE >=KERNEL_VERSION(5,6,0))
        if (!proc_create ("spcm4_cards", 0, NULL, &g_stProcOps))
            return -ENOMEM;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
        if (!proc_create ("spcm4_cards", 0, NULL, &g_stProcFileOps))
            return -ENOMEM;
#else
        create_proc_read_entry ("spcm4_cards", 0, 0, spcm4_read_procmem, 0);
#endif
        }

    return (nErrCount > 0 ? 0 : (nErrCount == 0 ? -ENODEV : nErrCount));
    }


/*
**************************************************************************
Module exit function
**************************************************************************
*/

static void __exit spcm4_vExit (void)
    {
    int i;
    DEBUGLOG (DBG_TRACE, "Kernel Driver Exit\n");

    remove_proc_entry ("spcm4_cards", 0);

    if(g_nUseSysfs)
        {
        for(i=0; i< g_nCardCount; i++)
            {
#   if (LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)) // 4.3
            if(misc_deregister(g_ppstMiscDev[i]) != 0)
                DEBUGLOG (DBG_ERROR, "misc_deregister failed for spcm%i\n", i);
#else
            misc_deregister(g_ppstMiscDev[i]);
#endif
            }
        }
    else
        {
        if (g_nMajor > 0)
            unregister_chrdev (g_nMajor, SPCM_DEVICENAME);
        }
    pci_unregister_driver(&g_stPCIDriver);
    }



module_init(spcm4_lInit);
module_exit(spcm4_vExit);


/*
**************************************************************************
open: open the driver and check for exclusive access
**************************************************************************
*/

int spcm4_lOpen (struct inode *pstInode, struct file *pstFilp)
    {
    SPCM_ST_CARDINFO* pstCard = NULL;
    int lRet, i;
    int32 lMinor = -1;

    if(g_nUseSysfs)
        {

        // if udev & sysfs is used to assign minor numbers, it is not safe to assume that first assigned minor number is zero
        // if udev and sysfs is used lMinor holds the index to g_ppstCard, not the minor number
        // otherwise the minor number and the index are identical
        for(i=0; i < MAXBRD; i++)
            {
            if(g_ppstMinorIndexMap[i]->lMinor == (pstInode->i_rdev & 0xff))
                {
                lMinor = g_ppstMinorIndexMap[i]->lIndex;
                break;
                }
            }
        }
    else
        lMinor = (pstInode->i_rdev & 0xff);

    DEBUGLOG (DBG_TRACE, "spcm4_lOpen %d - Start\n", lMinor);

    // check for correct minor number
    if ((lMinor < 0) || (lMinor >= MAXBRD))
        {
        DEBUGLOG (DBG_ERROR, "%s - invalid minor: %d\n", __FUNCTION__, lMinor);
        return -EINVAL;
        }

    if (!g_ppstCard[lMinor])
        {
        DEBUGLOG (DBG_ERROR, "%s - no card struct for minor %d\n", __FUNCTION__, lMinor);
        return -EINVAL;
        }

    // if the file mode is read only we are called from other (M2i) kernel module to check if device exists
    if ((pstFilp->f_mode & FMODE_WRITE) == 0)
        {
        DEBUGLOG (DBG_TRACE, "%s - Opened by M2i driver\n", __FUNCTION__);
        return 0;
        }


    pstCard = g_ppstCard[lMinor];

    // check whether this process has access rights to the driver
    // each process can open each card only once, otherwise a kernel OOPS occurs
    if (pstCard->dwOpenProcessId != 0)
        {
        DEBUGLOG (DBG_ERROR, "%s - card already opened by PID %u\n", __FUNCTION__, pstCard->dwOpenProcessId);
        return -EBUSY;
        }
    // do not set dwOpenProcessId until it is clear that this function will succeed

    // link the private data to the card info structure
    pstFilp->private_data = pstCard;

    // call the card open function that makes all the work
    lRet = card_lOpen (pstCard);

    DEBUGLOG (DBG_TRACE, "spcm4_lOpen %d - End -> %d\n", lMinor, lRet);
#if defined(LINUX_2_4)
    MOD_INC_USE_COUNT;
#endif

    // when the card has been opened successfully we store the ID of the calling process
    if (lRet == 0)
        pstCard->dwOpenProcessId = (uint32) current->pid;
    return lRet;
    }


#if defined (HAVE_UNLOCKED_IOCTL)
/*
**************************************************************************
ioctl_unlocked: check the details and send the ioctl structure to card.
                Since kernel 2.6.36 ioctl with BKL is not available any more,
                every driver has to implement its own locking
                Since kernel 2.6.39 BKL has been removed. spcm4_lIoCtl takes
                care of locking.
**************************************************************************
*/
long spcm4_lIoCtl_Unlocked (struct file* pstFilp, unsigned int dwCmd, unsigned long dwArg)
    {
    int lRet = 0;
#   if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)) // 3.19
    struct inode* pstInode = pstFilp->f_dentry->d_inode;
#   else
    struct inode* pstInode = pstFilp->f_path.dentry->d_inode;
#   endif

#   if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)) // 2.6.39
    lock_kernel ();
#   endif
    lRet = spcm4_lIoCtl (pstInode, pstFilp, dwCmd, dwArg);
#   if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)) // 2.6.39
    unlock_kernel ();
#   endif

    return lRet;
    }
#endif

/*
**************************************************************************
ioctl: check the details and send the ioctl structure to card
**************************************************************************
*/

int spcm4_lIoCtl (struct inode* pstInode, struct file* pstFilp, unsigned int dwCmd, unsigned long dwArg)
    {
    SPCM_ST_CARDINFO* pstCard = (SPCM_ST_CARDINFO*) pstFilp->private_data;


    if (!pstCard || (_IOC_TYPE(dwCmd) != SPCM_IOC_MAGIC) || (_IOC_NR(dwCmd) > SPCM_IOC_MAXNR))
        return -EINVAL;


    // select the matching ioctl function
    switch (dwCmd)
        {
        // ----- get kernel driver infos (type and number of devices) -----
        case SPCM_IOC_GETINFO:
            {
            SPCM_IOCTL_GETINFO stGetInfo = { SPCM_KERNELTYPE_M4, g_nCardCount };
            if (copy_to_user ((void*) dwArg, &stGetInfo, sizeof (SPCM_IOCTL_GETINFO)))
                return -EINVAL;
            break;
            }

        // ----- get version -----
        case SPCM_IOC_GETVERSION:
            {
            SPCM_IOCTL_GETVERSION stGetVersion;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - GetVersion\n", (pstInode->i_rdev & 0xff));
            stGetVersion.dwKernelVersion =      SPCM4_KRNLDRIVER_VERSION;
            stGetVersion.dwInterfaceVersion =   SPCM_DRV_INTERFACE_M4I;
            if (copy_to_user ((void*) dwArg, &stGetVersion, sizeof (SPCM_IOCTL_GETVERSION)))
                return -EINVAL;
            break;
            }

        // ----- get properties -----
        case SPCM_IOC_GETPROPERTIES:
            {
            SPCM_IOCTL_GETPROPERTIES stGetProperties;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - GetProperties\n", (pstInode->i_rdev & 0xff));
            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;
            stGetProperties.dwPCIBus  = (uint32)pstCard->byPCIBus;
            stGetProperties.dwPCISlot = 0; // not available
            stGetProperties.dwPCIDev  = (uint32)pstCard->byPCIDev;
            stGetProperties.dwPCIFunc = (uint32)pstCard->byPCIFunc;

            up (&pstCard->semDrvAccess);
            if (copy_to_user ((void*) dwArg, &stGetProperties, sizeof (SPCM_IOCTL_GETPROPERTIES)))
                return -EINVAL;
            break;
            }

        // ----- read single -----
        case SPCM_IOC_READSINGLE:
            {
            SPCM_IOCTL_RWSINGLE stRWSingle;

            DEBUGLOG (DBG_TRACEALL, "spcm_lIoCtl %d - ReadSingle\n", (pstInode->i_rdev & 0xff));
            if (copy_from_user (&stRWSingle, (void*) dwArg, sizeof (SPCM_IOCTL_RWSINGLE)))
                return -EINVAL;

            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;
            card_vReadSingle (pstCard, &stRWSingle);
            up (&pstCard->semDrvAccess);
            if (copy_to_user ((void*) dwArg, &stRWSingle, sizeof (SPCM_IOCTL_RWSINGLE)))
                return -EINVAL;
            break;
            }

        // ----- write single -----
        case SPCM_IOC_WRITESINGLE:
            {
            SPCM_IOCTL_RWSINGLE stRWSingle;

            DEBUGLOG (DBG_TRACEALL, "spcm_lIoCtl %d - WriteSingle\n", (pstInode->i_rdev & 0xff));
            if (copy_from_user (&stRWSingle, (void*) dwArg, sizeof (SPCM_IOCTL_RWSINGLE)))
                return -EINVAL;

            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;

            card_vWriteSingle (pstCard, &stRWSingle);
            up (&pstCard->semDrvAccess);

            if (copy_to_user ((void*) dwArg, &stRWSingle, sizeof (SPCM_IOCTL_RWSINGLE)))
                return -EINVAL;
            break;
            }



        // ----- read/write list -----
        case SPCM_IOC_RWLIST:
            {
            SPCM_IOCTL_RWLIST stRWList, stTmpList;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - ReadWriteList\n", (pstInode->i_rdev & 0xff));

            if (copy_from_user (&stTmpList, (void*) dwArg, sizeof (SPCM_IOCTL_RWLIST)))
                return -EINVAL;

            // alloc new structures for the lists
            stRWList = stTmpList;
            stRWList.pdwCmdList    = kmalloc (sizeof (uint32) * stRWList.dwListLen, GFP_KERNEL);
            stRWList.pdwOffsetList = kmalloc (sizeof (uint32) * stRWList.dwListLen, GFP_KERNEL);
            stRWList.pdwDataList   = kmalloc (sizeof (uint32) * stRWList.dwListLen, GFP_KERNEL);

            // check for allocation error
            if (!stRWList.pdwCmdList || !stRWList.pdwOffsetList || !stRWList.pdwDataList)
                return -EFAULT;

            // copy the list from the user space
            if (copy_from_user (stRWList.pdwCmdList,    stTmpList.pdwCmdList,    sizeof (uint32) * stRWList.dwListLen) ||
                copy_from_user (stRWList.pdwOffsetList, stTmpList.pdwOffsetList, sizeof (uint32) * stRWList.dwListLen) ||
                copy_from_user (stRWList.pdwDataList,   stTmpList.pdwDataList,   sizeof (uint32) * stRWList.dwListLen))
                return -EINVAL;

            // that's the command itself
            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;

            card_vReadWriteList (pstCard, &stRWList);
            up (&pstCard->semDrvAccess);

            // copy data back to user space (only DataList can contain valid data)
            if (copy_to_user (stTmpList.pdwDataList, stRWList.pdwDataList, sizeof (uint32) * stRWList.dwListLen))
                return -EINVAL;

            // and clean up
            kfree (stRWList.pdwCmdList);
            kfree (stRWList.pdwOffsetList);
            kfree (stRWList.pdwDataList);

            break;
            }



        // ----- read card status -----
        case SPCM_IOC_CARDSTATUS:
            {
            uint32 dwDMAChannel = 0;
            int lErr = 0;

            SPCM_IOCTL_CARDSTATUS stCardStatus;
            if (copy_from_user (&stCardStatus, (void*) dwArg, sizeof (SPCM_IOCTL_CARDSTATUS)))
                return -EINVAL;

            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;

            dwDMAChannel = stCardStatus.dwCardStatus; // first double word
            lErr = card_lReadCardStatus (pstCard, dwDMAChannel, &stCardStatus);
            up (&pstCard->semDrvAccess);
            if (lErr)
                return lErr;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - ReadCardStatus: Stat:%08x DMA0:%llu DMA1:%llu DMA2:%llu\n", (pstInode->i_rdev & 0xff), stCardStatus.dwCardStatus, stCardStatus.qwDMA0UsedBuf, stCardStatus.qwDMA1UsedBuf, stCardStatus.qwFIFOStatus);

            if (copy_to_user ((void*) dwArg, &stCardStatus, sizeof (SPCM_IOCTL_CARDSTATUS)))
                return -EINVAL;
            break;
            }

        // ----- DMA config -----
        case SPCM_IOC_DMACONFIG:
            {
            SPCM_IOCTL_DMACONFIG stDMAConfig;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - DMAConfig\n", (pstInode->i_rdev & 0xff));
            if (copy_from_user (&stDMAConfig, (void*) dwArg, sizeof (SPCM_IOCTL_DMACONFIG)))
                return -EINVAL;

            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;

// TODO
// probably not necessary anymore
// but still called from library?
/*
            if (!bDMAConfig (pstCard, pstCard->pstDMAObject[stDMAConfig.dwDMAChannel], stDMAConfig.dwBusWidth, 1))
                {
                up (&pstCard->semDrvAccess);
                return -EFAULT;
                }
*/
            up (&pstCard->semDrvAccess);

            break;
            }

        // -----DMA SetBuffer -----
        case SPCM_IOC_DMASETBUF:
            {
            SPCM_IOCTL_DMASETBUFFER     stDMASetBuffer;
            int                         lRet;
            COMMON_DMA_PARAMS*          pDmaParams = NULL;
            bool b64BitAddress      = false;
            bool bGPUUsed           = false;
            bool bPageAlignedBuffer = false;

            if (copy_from_user (&stDMASetBuffer, (void*) dwArg, sizeof (SPCM_IOCTL_DMASETBUFFER)))
                return -EINVAL;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - DMA %d SetBuffer Len=%d\n", (pstInode->i_rdev & 0xff), stDMASetBuffer.dwDMAChannel, (uint32) stDMASetBuffer.qwBufLen);


            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;

            pDmaParams = card_pstGetDMAParams (pstCard, stDMASetBuffer.dwDMAChannel);
            if (pDmaParams == NULL)
                {
                DEBUGLOG (DBG_ERROR, "spcm4_lIoCtl %d - DMA %d SetBuffer failed (invalid channel param)\n", (pstInode->i_rdev & 0xff), stDMASetBuffer.dwDMAChannel);
                up (&pstCard->semDrvAccess);
                return -EFAULT;
                }

            bGPUUsed = (stDMASetBuffer.dwDMAChannel >= 8);

            // ----- set transfer direction and new transfer size -----
            bPageAlignedBuffer = (((uint64)(stDMASetBuffer.pvUserBuffer) % PAGE_SIZE) == 0);
            if (pstCard->eDMACore == NWC)
                {
                NWC_DMA_PARAMS* pNWCDmaParams = (NWC_DMA_PARAMS*)pDmaParams;

                lRet = byNWCAllocateMemoryForSGList (pstCard, pNWCDmaParams, bPageAlignedBuffer, stDMASetBuffer.qwBufLen, stDMASetBuffer.dwDMAPageSize);
                if (lRet < 0)
                    {
                    up (&pstCard->semDrvAccess);
                    return lRet;
                    }

                pDmaParams->qwBufferSize = stDMASetBuffer.qwBufLen;
                pDmaParams->writeToDevice = card_bDMAChannelDirIsWrite (stDMASetBuffer.dwDMAChannel);
                NWD_SetDMA_Direction (pNWCDmaParams, pDmaParams->writeToDevice);
                lRet = SPCM4DRV_NWC_BuildSGList (pstCard, stDMASetBuffer.pvUserBuffer, stDMASetBuffer.qwBufLen, !stDMASetBuffer.bDirWrite_64BitUsed, bGPUUsed, stDMASetBuffer.dwDMAPageSize, pNWCDmaParams, &b64BitAddress);
                if (lRet < 0)
                    {
                    up (&pstCard->semDrvAccess);
                    return lRet;
                    }
                }
            else
                {
                XDMA_DMA_PARAMS* pXDMADmaParams = (XDMA_DMA_PARAMS*)pDmaParams;

                lRet = byXDMAAllocateMemoryForSGList (pstCard, pXDMADmaParams, bPageAlignedBuffer, stDMASetBuffer.qwBufLen, stDMASetBuffer.dwDMAPageSize);
                if (lRet < 0)
                    {
                    up (&pstCard->semDrvAccess);
                    return lRet;
                    }

                pDmaParams->qwBufferSize = stDMASetBuffer.qwBufLen;
                pDmaParams->writeToDevice = card_bDMAChannelDirIsWrite (stDMASetBuffer.dwDMAChannel);
                lRet = SPCM4DRV_XDMA_BuildSGList (pstCard, stDMASetBuffer.pvUserBuffer, stDMASetBuffer.qwBufLen, !stDMASetBuffer.bDirWrite_64BitUsed, bGPUUsed, stDMASetBuffer.dwDMAPageSize, pXDMADmaParams, &b64BitAddress);
                if (lRet < 0)
                    {
                    up (&pstCard->semDrvAccess);
                    return lRet;
                    }
                }

            // warning of 64 bit adresses used
            stDMASetBuffer.bDirWrite_64BitUsed = b64BitAddress;
            DEBUGLOG (DBG_TRACE, "- 64 bit adresses used = %d\n", stDMASetBuffer.bDirWrite_64BitUsed);

            if (copy_to_user ((void*) dwArg, &stDMASetBuffer, sizeof (SPCM_IOCTL_DMASETBUFFER)))
                {
                up (&pstCard->semDrvAccess);
                return -EINVAL;
                }

            up (&pstCard->semDrvAccess);

            break;
            }

        // ----- DMA Start -----
        case SPCM_IOC_DMASTART:
            {
            COMMON_DMA_PARAMS* pDmaParams = NULL;
            uint64 qwTransferSize  = 0;
            uint32* pdwBAR0Mem     = NULL;
            uint32 dwReg           = 0;

            SPCM_IOCTL_DMASTARTSTOP stDMAStartStop;
            if (copy_from_user (&stDMAStartStop, (void*) dwArg, sizeof (SPCM_IOCTL_DMASTARTSTOP)))
                return -EINVAL;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - DMA %d Start\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel);

            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;

            pDmaParams = card_pstGetDMAParams (pstCard, stDMAStartStop.dwDMAChannel);
            if (pDmaParams == NULL)
                {
                DEBUGLOG (DBG_ERROR, "spcm4_lIoCtl %d - DMA %d Start failed (invalid channel param)\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel);
                up (&pstCard->semDrvAccess);
                return -EFAULT;
                }

            qwTransferSize = stDMAStartStop.qwByteLen;
            if (pstCard->eDMACore == NWC)
                {
                NWC_DMA_PARAMS* pNWCDmaParams = (NWC_DMA_PARAMS*)pDmaParams;

                if ((qwTransferSize == 0) && !card_bDMAChannelDirIsWrite (stDMAStartStop.dwDMAChannel))
                    qwTransferSize = pDmaParams->qwBufferSize;

                pdwBAR0Mem = pstCard->apdwMemMappedAddress[0];

                DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - DMA %d Start enable interrupt\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel);


                // enable PCI interrupt by dma channel
                dwReg = NWD_ReadByOffset (pdwBAR0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
                dwReg |= NWD_ENGCNTRL_INTEN;
                NWD_WriteByOffset (pdwBAR0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, dwReg);

                // global IR enable
                dwReg = NWD_ReadByOffset (pdwBAR0Mem, NWD_COMMON_REGISTER_BLOCK);
                dwReg |= NWD_COMREG_GLOBAL_INTEN;
                NWD_WriteByOffset (pdwBAR0Mem, NWD_COMMON_REGISTER_BLOCK, dwReg);

                SPCM4DRV_NWC_StartTransfer (pstCard, pNWCDmaParams, stDMAStartStop.qwByteOffset, qwTransferSize, stDMAStartStop.dwDMAPageSize);
                }
            else
                {
                XDMA_DMA_PARAMS* pXDMADmaParams = (XDMA_DMA_PARAMS*)pDmaParams;

                if ((qwTransferSize == 0) && !card_bDMAChannelDirIsWrite (stDMAStartStop.dwDMAChannel))
                    qwTransferSize = pDmaParams->qwBufferSize;

                DEBUGLOG (DBG_NONE, "spcm4_lIoCtl %d - DMA %d Start TODO: enable interrupt\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel);

                //XDMA_WriteByOffset (pdwBAR1Mem, XMDA_TARGET_IRQBLOCK + XDMA_REG_IRQBLOCK_CH_VECTOR_NUMBER_30, ((2U << 16) | (1U << 8) | (0U << 0)));

                SPCM4DRV_XDMA_StartTransfer (pstCard, pXDMADmaParams, stDMAStartStop.qwByteOffset, qwTransferSize, stDMAStartStop.dwDMAPageSize);
                }

            up (&pstCard->semDrvAccess);

            break;
            }

        // ----- DMA Stop -----
        case SPCM_IOC_DMASTOP:
            {
            COMMON_DMA_PARAMS* pDmaParams = NULL;
            uint32* pdwBAR0Mem     = NULL;
            uint32  dwReg          = 0;
            unsigned long dwFlags  = 0;

            SPCM_IOCTL_DMASTARTSTOP stDMAStartStop;
            if (copy_from_user (&stDMAStartStop, (void*) dwArg, sizeof (SPCM_IOCTL_DMASTARTSTOP)))
                return -EINVAL;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - DMA %d Stop\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel);

            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;

            pDmaParams = card_pstGetDMAParams (pstCard, stDMAStartStop.dwDMAChannel);
            if (pDmaParams == NULL)
                {
                DEBUGLOG (DBG_ERROR, "spcm4_lIoCtl %d - DMA %d Stop failed (invalid channel param)\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel);
                up (&pstCard->semDrvAccess);
                return -EFAULT;
                }

            if (pstCard->eDMACore == NWC)
                {
                NWC_DMA_PARAMS* pNWCDmaParams = (NWC_DMA_PARAMS*)pDmaParams;

                SPCM4DRV_NWC_StopTransfer (pstCard, pNWCDmaParams);

                pdwBAR0Mem = pstCard->apdwMemMappedAddress[0];

                // disable PCI interrupt by dma channel
                dwReg = NWD_ReadByOffset (pdwBAR0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL);
                dwReg &= ~NWD_ENGCNTRL_INTEN;
                spin_lock_irqsave (&stIRQLock, dwFlags);
                NWD_WriteByOffset (pdwBAR0Mem, pDmaParams->dwEngAddrOffs + NWD_ENGCNTRL, dwReg);
                spin_unlock_irqrestore (&stIRQLock, dwFlags);
                }
            else
                {
                XDMA_DMA_PARAMS* pXDMADmaParams = (XDMA_DMA_PARAMS*)pDmaParams;

                SPCM4DRV_XDMA_StopTransfer (pstCard, pXDMADmaParams);

                DEBUGLOG(DBG_ERROR, "TODO: DMASTOP XDMA: disable interrupt?\n");
                }

            up (&pstCard->semDrvAccess);

            break;
            }

        // ----- DMA Clear -----
        case SPCM_IOC_DMACLEAR:
            {
            void* pDmaParams = NULL;

            SPCM_IOCTL_DMASTARTSTOP stDMAStartStop;
            if (copy_from_user (&stDMAStartStop, (void*) dwArg, sizeof (SPCM_IOCTL_DMASTARTSTOP)))
                return -EINVAL;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - DMA %d Clear\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel);

            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;

            pDmaParams = card_pstGetDMAParams (pstCard, stDMAStartStop.dwDMAChannel);
            if (pDmaParams == NULL)
                {
                DEBUGLOG (DBG_ERROR, "spcm4_lIoCtl %d - DMA %d Clear failed (invalid channel param)\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel);
                up (&pstCard->semDrvAccess);
                return -EFAULT;
                }


            if (pstCard->eDMACore == NWC)
                SPCM4DRV_NWC_ClearData (pstCard, (NWC_DMA_PARAMS*)pDmaParams);
            else
                SPCM4DRV_XDMA_ClearData (pstCard, (XDMA_DMA_PARAMS*)pDmaParams);

            up (&pstCard->semDrvAccess);
            break;
            }

        // ----- DMA AvailUserLen -----
        case SPCM_IOC_DMAAVAIL:
            {
            COMMON_DMA_PARAMS* pDmaParams = NULL;
            bool bFlushSGList = false;

            SPCM_IOCTL_DMASTARTSTOP stDMAStartStop;
            if (copy_from_user (&stDMAStartStop, (void*) dwArg, sizeof (SPCM_IOCTL_DMASTARTSTOP)))
                return -EINVAL;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - DMA %d Avail %d\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel, (uint32) stDMAStartStop.qwByteLen);

            if (down_interruptible (&pstCard->semDrvAccess))
                return -EBUSY;

            pDmaParams = card_pstGetDMAParams (pstCard, stDMAStartStop.dwDMAChannel);
            if (pDmaParams == NULL)
                {
                DEBUGLOG (DBG_ERROR, "spcm4_lIoCtl %d - DMA %d Avail failed (invalid channel param)\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel);
                up (&pstCard->semDrvAccess);
                return -EFAULT;
                }

            bFlushSGList = (stDMAStartStop.dwDMAPageSize != 0); // re-use existing struct entry to avoid kernel interface change
            if (pstCard->eDMACore == NWC)
                {
                NWC_DMA_PARAMS* pNWCDmaParams = (NWC_DMA_PARAMS*)pDmaParams;

                // check if number of bytes to free is really available
                if (stDMAStartStop.qwByteLen > pDmaParams->qwBytesTransfered)
                    {
                    DEBUGLOG (DBG_ERROR, "spcm4_lIoCtl %d - DMA %d Avail failed (too many bytes)\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel);
                    up (&pstCard->semDrvAccess);
                    return -EFAULT;
                    }

                SPCM4DRV_NWC_RefreshSGList (pstCard, pNWCDmaParams, stDMAStartStop.qwByteLen, bFlushSGList);
                }
            else
                {
                XDMA_DMA_PARAMS* pXDMADmaParams = (XDMA_DMA_PARAMS*)pDmaParams;

                // check if number of bytes to free is really available
                if (stDMAStartStop.qwByteLen > pDmaParams->qwBytesTransfered)
                    {
                    DEBUGLOG (DBG_ERROR, "spcm4_lIoCtl %d - DMA %d Avail failed (too many bytes: %llu > %llu)\n", (pstInode->i_rdev & 0xff), stDMAStartStop.dwDMAChannel, stDMAStartStop.qwByteLen, pDmaParams->qwBytesTransfered);
                    up (&pstCard->semDrvAccess);
                    return -EFAULT;
                    }

                SPCM4DRV_XDMA_RefreshSGList (pstCard, pXDMADmaParams, stDMAStartStop.qwByteLen, bFlushSGList);
                }
            up (&pstCard->semDrvAccess);
            break;
            }

        // ----- KernelEvent Wait -----
        case SPCM_IOC_EVENTWAIT:
            {
            SPCM_IOCTL_EVENTWAITSIGNAL stKernelEvent;
            if (copy_from_user (&stKernelEvent, (void*) dwArg, sizeof (SPCM_IOCTL_EVENTWAITSIGNAL)))
                return -EINVAL;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - KernelEventWait\n", (pstInode->i_rdev & 0xff));

            wait_event_interruptible (pstCard->wqKernelEvent, (pstCard->bWakeUp == 1));
            pstCard->bWakeUp = 0;
            break;
            }

        // ----- KernelEvent Signal -----
        case SPCM_IOC_EVENTSIGNAL:
            {
            SPCM_IOCTL_EVENTWAITSIGNAL stKernelEvent;
            if (copy_from_user (&stKernelEvent, (void*) dwArg, sizeof (SPCM_IOCTL_EVENTWAITSIGNAL)))
                return -EINVAL;

            DEBUGLOG (DBG_TRACE, "spcm4_lIoCtl %d - KernelEventSignal\n", (pstInode->i_rdev & 0xff));

            pstCard->bWakeUp = 1;
            wake_up_interruptible (&pstCard->wqKernelEvent);
            break;
            }

        // ----- continuous memory read out -----
        case SPCM_IOC_GETCONTMEM:
            {
            SPCM_IOCTL_DMACONTMEM stContMem;
            stContMem.pvContMemAdr = pstCard->pvContMemBuffer;
            stContMem.qwContMemLen = pstCard->qwContMemLen;

            DEBUGLOG (DBG_TRACE, "spcmIoCtl %d - KernelGetContMem Adr=%p Len=%x\n", (pstInode->i_rdev & 0xff), pstCard->pvContMemBuffer, (uint32) (pstCard->qwContMemLen & 0xffffffff));

            if (copy_to_user ((void*) dwArg, &stContMem, sizeof (SPCM_IOCTL_DMACONTMEM)))
                return -EINVAL;

            break;
            }

        // ----- unknown command -----
        default:
            DEBUGLOG (DBG_ERROR, "unknown ioctl command %x\n", dwCmd);
            return -EINVAL;
        }

    DEBUGLOG (DBG_TRACEALL, "spcm4_lIoCtl %d - End\n", (pstInode->i_rdev & 0xff));

    return 0;
    }



/*
**************************************************************************
lMMap: map the continuous buffer to the user buffer to gain access to it
**************************************************************************
*/

int spcm4_lMMap (struct file *pstFilp, struct vm_area_struct* pstVma)
    {
    SPCM_ST_CARDINFO* pstCard = (SPCM_ST_CARDINFO*) pstFilp->private_data;

    DEBUGLOG (DBG_TRACE, "spcm4_lMMap start=%x end=%x pg_off=%x\n", (uint32) pstVma->vm_start, (uint32) pstVma->vm_end, (uint32) pstVma->vm_pgoff);

    // no cont buffer available or too short
    if (!pstCard->pvContMemBuffer || ((pstVma->vm_end - pstVma->vm_start) > pstCard->qwContMemLen))
        return -ENOMEM;
        
#if (LINUX_VERSION_CODE >= 0x02060A)    // 2.6.10
    if (remap_pfn_range  (pstVma, pstVma->vm_start, virt_to_phys(pstCard->pvContMemBuffer) >> PAGE_SHIFT, pstVma->vm_end - pstVma->vm_start, pstVma->vm_page_prot) < 0)
#elif (LINUX_VERSION_CODE >= 0x020600)  // 2.6.0
    if (remap_page_range (pstVma, pstVma->vm_start, virt_to_phys(pstCard->pvContMemBuffer),               pstVma->vm_end - pstVma->vm_start, pstVma->vm_page_prot) < 0)
#elif defined(redhat90)                 // Red Hat 9.0 (using 2.5.x memory mapping sources)
    if (remap_page_range (pstVma, pstVma->vm_start, virt_to_phys(pstCard->pvContMemBuffer),               pstVma->vm_end - pstVma->vm_start, pstVma->vm_page_prot) < 0)
#else                                   // 2.4.x
    if (remap_page_range (        pstVma->vm_start, virt_to_phys(pstCard->pvContMemBuffer),               pstVma->vm_end - pstVma->vm_start, pstVma->vm_page_prot) < 0)
#endif
        {
        DEBUGLOG (DBG_ERROR, "... remapping of continuous kernel memory to user space failed\n");
        return -EAGAIN;
        }

    // store the user adresses to know whether we're in our continous memory if SGList is used
    pstCard->qwContMemUserStart = pstVma->vm_start;
    pstCard->qwContMemUserLen = pstVma->vm_end - pstVma->vm_start;
    DEBUGLOG (DBG_TRACE, "... successful\n");

    return 0;
    }



/*
**************************************************************************
lClose: close the driver
**************************************************************************
*/

int spcm4_lClose (struct inode *pstInode, struct file *pstFilp)
    {
    SPCM_ST_CARDINFO* pstCard = (SPCM_ST_CARDINFO*) pstFilp->private_data;
    int lRet;

    DEBUGLOG (DBG_TRACE, "spcm4_lClose %d - Start\n", (pstInode->i_rdev & 0xff));

    // ----- if card has been opened by M2i/M3i driver we don't need to clean up -----
    if ((pstFilp->f_mode & FMODE_WRITE) == 0)
        {
        DEBUGLOG (DBG_TRACE, "%s - Closed by M2i driver\n", __FUNCTION__);
        return 0;
        }

    if (!pstCard)
        return -EINVAL;

    // wait for the last access (no error checking as we clean up now, but the return value needs to be assigned to avoid warning)
    lRet = down_interruptible (&pstCard->semDrvAccess);

    // call the card release function that cleans up
    lRet = card_lClose (pstCard);

    // release the process id
    pstCard->dwOpenProcessId = 0;

    up (&pstCard->semDrvAccess);

    DEBUGLOG (DBG_TRACE, "spcm4_lClose %d - End\n", (pstInode->i_rdev & 0xff));

#if defined (LINUX_2_4)
    MOD_DEC_USE_COUNT;
#endif

    return lRet;
    }



