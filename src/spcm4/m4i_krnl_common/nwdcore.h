//######################################################################
//
//  Module:     nwdcore.h
//
//  Descript.:  definition of Northwest DMA Core constants, structures and functions
//
//######################################################################

#ifndef NWDCORE_H
#define NWDCORE_H

//----------------------------------------------------------------------
// NWDCORE
//----------------------------------------------------------------------

//----------------------------------------------------------------------
// contants
//----------------------------------------------------------------------

#define NWD_COMMON_REGISTER_BLOCK       0x4000      // DMA Common Register Block
#define NWD_COMREG_GLOBAL_INTEN             0x00000001  // Global DMA Interrupt Enable
#define NWD_COMREG_DMA_INT                  0x00000002  // DMA Interrupt Active
#define NWD_COMREG_USER_INTEN               0x00000010  // Read/Write: User_Interrupt_Enable
#define NWD_COMREG_USER_INT                 0x00000020  // User_Interrupt_Active
#define NWD_COMREG_S2C_INT_ENG0             0x00010000  // S2C_Interrupt_Status from Engine ...
#define NWD_COMREG_S2C_INT_ENG1             0x00020000  // S2C_Interrupt_Status from Engine ...
#define NWD_COMREG_S2C_INT_ENG2             0x00040000  // S2C_Interrupt_Status from Engine ...
#define NWD_COMREG_S2C_INT_ENG3             0x00080000  // S2C_Interrupt_Status from Engine ...
#define NWD_COMREG_S2C_INT_ENG4             0x00100000  // S2C_Interrupt_Status from Engine ...
#define NWD_COMREG_S2C_INT_ENG5             0x00200000  // S2C_Interrupt_Status from Engine ...
#define NWD_COMREG_S2C_INT_ENG6             0x00400000  // S2C_Interrupt_Status from Engine ...
#define NWD_COMREG_S2C_INT_ENG7             0x00800000  // S2C_Interrupt_Status from Engine ...
#define NWD_COMREG_S2C_INT_ENG_MASK         0x00ff0000  // mask for previous flags
#define NWD_COMREG_C2S_INT_ENG0             0x01000000  // C2S_Interrupt_Status from Engine ...
#define NWD_COMREG_C2S_INT_ENG1             0x02000000  // C2S_Interrupt_Status from Engine ...
#define NWD_COMREG_C2S_INT_ENG2             0x04000000  // C2S_Interrupt_Status from Engine ...
#define NWD_COMREG_C2S_INT_ENG3             0x08000000  // C2S_Interrupt_Status from Engine ...
#define NWD_COMREG_C2S_INT_ENG4             0x10000000  // C2S_Interrupt_Status from Engine ...
#define NWD_COMREG_C2S_INT_ENG5             0x20000000  // C2S_Interrupt_Status from Engine ...
#define NWD_COMREG_C2S_INT_ENG6             0x40000000  // C2S_Interrupt_Status from Engine ...
#define NWD_COMREG_C2S_INT_ENG7             0x80000000  // C2S_Interrupt_Status from Engine ...
#define NWD_COMREG_C2S_INT_ENG_MASK         0xff000000  // mask for previous flags


#define NWD_BASE_S2C_ENGINES            0x0000      // Base address of System to Card Descriptor Engine
#define NWD_BASE_C2S_ENGINES            0x2000      // Base address of Card to System Descriptor Engine
#define NWD_DESCR_ENG_OFFSETS           0x0100      // Distance between each descriptor engine.


#define NWD_ENGCAP                      0x00        // DMA_Engine_Capabilities
#define NWD_ENGCAP_PRESENT                  0x00000001  // Present
#define NWD_ENGCAP_DIRECTIOM_C2S            0x00000002  // Engine Direction


#define NWD_ENGCNTRL                    0x04        // DMA Engine Control
#define NWD_ENGCNTRL_INTEN                  0x00000001  // Interrupt_Enable
#define NWD_ENGCNTRL_INT_ACT_CLR            0x00000002  // Interrupt_Active (write a 1 to clear)
#define NWD_ENGCNTRL_DSCR_CMPL_CLR          0x00000004  // Descriptor_Complete (write a 1 to clear)

#define NWD_ENGCNTRL_DSCR_CHNEND_CLR        0x00000008  // Descriptor_Chain_End (write a 1 to clear)
#define NWD_ENGCNTRL_DMA_EN                 0x00000100  // DMA_Enable
#define NWD_ENGCNTRL_DMA_RUNNING            0x00000400  // DMA_Running
#define NWD_ENGCNTRL_DMA_WAITING            0x00000800  // DMA_Waiting
#define NWD_ENGCNTRL_DMA_RESET_REQUEST      0x00004000  // DMA_Reset_Request
#define NWD_ENGCNTRL_DMA_RESET              0x00008000  // DMA_Reset


#define NWD_NEXT_DESC_PTR               0x08        // Reg_Next_Desc_Ptr

#define NWD_SW_DESC_PTR                 0x0c        // Reg_SW_Desc_Ptr

#define NWD_COMPLETED_DESC_PTR          0x10        // Reg_Completed_Desc_Ptr

#define NWD_ACTIVE_TIME                 0x14        // DMA_Active_Time

#define NWD_WAIT_TIME                   0x18        // DMA_Wait_Time

#define NWD_COMPLETED_BYTE_COUNT        0x1c        // DMA_Completed_Byte_Count

// Bitmasken für Descriptoren
#define NWD_S2CDESC_BYTECOUNT_MASK          0x000fffff  
#define NWD_S2CDESC_CNTRL_FLAG_SOP          0x80000000  
#define NWD_S2CDESC_CNTRL_FLAG_EOP          0x40000000  
#define NWD_S2CDESC_CNTRL_FLAG_IRQONERROR   0x02000000  
#define NWD_S2CDESC_CNTRL_FLAG_IRQONCOMPL   0x01000000  

#define NWD_S2CDESC_STATUS_FLAG_COMPLETE    0x01000000  

//----------------------------------------------------------------------
// structures
//----------------------------------------------------------------------


//----------------------------------------------------------------------
// functions
//----------------------------------------------------------------------

#ifdef WINVER
    #define NWD_WriteByOffset( plxBase, offset, data ) \
        ( WRITE_REGISTER_ULONG( (PULONG)(plxBase + offset), data ))
    #define NWD_ReadByOffset( plxBase, offset ) \
        ( READ_REGISTER_ULONG( (PULONG)(plxBase + offset) ))
#else

    #define NWD_WriteByOffset( plxBase, offset, data) \
        ( iowrite32(data, ((char*)plxBase) + offset ))
    #define NWD_ReadByOffset( plxBase, offset ) \
        ( ioread32(((char*)plxBase) + offset ))

    void NWD_EnableLocalInt (unsigned char*);
    void NWD_DisableInt (unsigned char*);
    void NWD_SetDMA_Direction (void*, int);
#endif



#endif // NWDCORE_H

