#ifdef WINVER
#   include "ntddk.h"
#   include "wdf.h"

#   include "m4i_krnl_wdm/prototypes.h"
#   include "m4i_krnl_wdm/spcm4drv.h"
#   include "xdmacore.h"

#   include ".\m4i_krnl_wdm\trace.h"
#   include "xdmacore.tmh"
#else
#   include "xdmacore.h"
//#   include "dma-xdma.h"
#   include <asm/io.h>

#   include "../c_header/dlltyp.h"
#endif



void XDMA_EnableLocalInt (unsigned char* pbyBaseAddr)
    {
    XDMA_WriteByOffset (pbyBaseAddr, XDMA_TARGET_IRQBLOCK + XDMA_REG_IRQBLOCK_USR_IRQ_EN_MASK_SET, XDMA_IRQREQ_USR_INT_ENG_MASK);
    }

void XDMA_DisableInt (unsigned char* pbyBaseAddr)
    {
    XDMA_WriteByOffset (pbyBaseAddr, XDMA_TARGET_IRQBLOCK + XDMA_REG_IRQBLOCK_USR_IRQ_EN_MASK_CLR, XDMA_IRQREQ_USR_INT_ENG_MASK);
    }

