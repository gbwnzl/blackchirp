/*
**************************************************************************

spcm_linux_wrapper.h                           (c) Spectrum GmbH,  10/2006

**************************************************************************

wrapper functions for older kernel versions

**************************************************************************
*/


// ----- mapping of ioread/iowrite commands -----
#ifndef ioread32
#   define ioread8(pvAddr) readb(pvAddr)
#   define ioread16(pvAddr) readw(pvAddr)
#   define ioread32(pvAddr) readl(pvAddr)
#   define iowrite8(byValue, pvAdr) writeb(byValue, pvAdr)
#   define iowrite16(wValue, pvAdr) writew(wValue, pvAdr)
#   define iowrite32(dwValue, pvAdr) writel(dwValue, pvAdr)
#endif


// ----- PCI_DEVICE macro definition -----
#ifndef PCI_DEVICE
#   define PCI_DEVICE(Vendor, Device) Vendor, Device, PCI_ANY_ID, PCI_ANY_ID
#endif


// ----- define DMA direction if it's not defined in the system -----
#if (LINUX_VERSION_CODE < 0x020600)
    enum dma_data_direction {DMA_FROM_DEVICE, DMA_TO_DEVICE};
#endif

// ----- DMA mapping -----
#if (LINUX_VERSION_CODE < 0x020600)
    static inline dma_addr_t dma_map_page(void* pvDev, struct page* pstPage, unsigned long dwOffset, size_t dwSize, enum dma_data_direction eDir)
        {return page_to_phys(pstPage) + dwOffset;}

    static inline void dma_unmap_page(void* pvDev, dma_addr_t dwAddr, size_t dwSize, enum dma_data_direction eDir)
        {}
#endif


