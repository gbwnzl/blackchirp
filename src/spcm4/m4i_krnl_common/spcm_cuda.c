#include "spcm_cuda.h"
#ifdef WINVER
    // not supported by nvidia
#else
#   include "../m4i_krnl_linux/spcm_linux_debug.h"
#   include "../m4i_krnl_linux/spcm_linux_card.h"
#endif

void vCudaCallback (void* pvData)
    {
    SPCM_ST_CARDINFO* pstCard = pvData;
SPCM4DRV_DebugPrint(DBG_TRACE, pstCard->dwBoardNumber, "%s\n", __FUNCTION__);
    //wait_for_pending_transfers (pstCard); // TODO
//    nvidia_p2p_free_page_table (pstCard->pstCudaPageTable);
    }

