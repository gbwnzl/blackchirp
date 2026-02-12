/*
//######################################################################
//
//  Module:     nwdcore.c
//
//  Descript.:  implementation of the driver's northwest core functions
//
//######################################################################
*/

/*
//----------------------------------------------------------------------
// NWSCORE
//----------------------------------------------------------------------
*/

#ifdef WINVER
#   include "ntddk.h"
#   include "wdf.h"

#   include "m4i_krnl_wdm/prototypes.h"
#   include "m4i_krnl_wdm/spcm4drv.h"
#   include "nwdcore.h"

#   include ".\m4i_krnl_wdm\trace.h"
#   include "nwdcore.tmh"
#else
#   include "nwdcore.h"
#   include "dma-nwc.h"
#   include <asm/io.h>

#   include "../c_header/dlltyp.h"
#endif


// enable local interrupt
void NWD_EnableLocalInt (unsigned char* nwdBase)
{
    UINT32 dwReg;

#ifdef WINVER
    SPCM4DRV_DebugPrint (TRACE, 0, "NWD_EnableLocalInt\n");
#endif

    dwReg = NWD_ReadByOffset ((char*) nwdBase, NWD_COMMON_REGISTER_BLOCK);
    dwReg |= NWD_COMREG_GLOBAL_INTEN;
    dwReg |= NWD_COMREG_USER_INTEN;
    NWD_WriteByOffset ((char*) nwdBase, NWD_COMMON_REGISTER_BLOCK, dwReg);

}

// disable PCI interrupt
void NWD_DisableInt (unsigned char* nwdBase)
{
    UINT32 dwReg;

#ifdef WINVER
    SPCM4DRV_DebugPrint (TRACE, 0, "NWD_DisableInt\n");
#endif

    dwReg = NWD_ReadByOffset ((char*) nwdBase, NWD_COMMON_REGISTER_BLOCK);
    dwReg &= ~NWD_COMREG_GLOBAL_INTEN;
    dwReg &= ~NWD_COMREG_USER_INTEN;
    NWD_WriteByOffset ((char*) nwdBase, NWD_COMMON_REGISTER_BLOCK, dwReg);
}



// DMA direction differs in engine base address
void NWD_SetDMA_Direction (void* pDmaParams, int bWriteToDevice)
{
#ifdef WINVER
    SPCM4DRV_DebugPrint (TRACE, 0, "NWD_SetDMA_Direction: %s\n", bWriteToDevice? "System2Card" : "Card2System");
#endif

    if (bWriteToDevice)
        ((COMMON_DMA_PARAMS*)pDmaParams)->dwEngAddrOffs &= ~NWD_BASE_C2S_ENGINES;
    else
        ((COMMON_DMA_PARAMS*)pDmaParams)->dwEngAddrOffs |= NWD_BASE_C2S_ENGINES;
}

