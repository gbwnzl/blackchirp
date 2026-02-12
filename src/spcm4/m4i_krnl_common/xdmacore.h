//######################################################################
//
//  Module:     xdmacore.h
//
//  Descript.:  definition of Xilinx PCIe & DMA Core constants, structures and functions
//
//######################################################################

#ifndef XDMACORE_H
#define XDMACORE_H

//----------------------------------------------------------------------
// XDMACORE
//----------------------------------------------------------------------


//----------------------------------------------------------------------
// constants
//----------------------------------------------------------------------


// targets
#define XDMA_TARGET_MASK            (0xFUL << 12)
#define XDMA_TARGET_H2C_CHANNELS    (0x0UL << 12)
#define XDMA_TARGET_C2H_CHANNELS    (0x1UL << 12)
#define XDMA_TARGET_IRQBLOCK        (0x2UL << 12)
#define XDMA_TARGET_CONFIG          (0x3UL << 12)
#define XDMA_TARGET_H2C_SGDMA       (0x4UL << 12)
#define XDMA_TARGET_C2H_SGDMA       (0x5UL << 12)
#define XDMA_TARGET_SGDMA_COMMON    (0x6UL << 12)
#define XDMA_TARGET_MSIX            (0x8UL << 12)

// Host-to-card (H2C) and card-to-host (C2H) share same values
#define XDMA_REG_CHANNEL_IDENTIFIER                 0x00
#define     CHANNEL_IDENTIFIER_CORE                     0xFFF00000
#define     CHANNEL_IDENTIFIER_CH_TARGET                0x000F0000
#define     CHANNEL_IDENTIFIER_STREAM                   0x00008000
#define     CHANNEL_IDENTIFIER_RESERVED                 0x00007000
#define     CHANNEL_IDENTIFIER_CH_ID_TARGET             0x00000F00
#define     CHANNEL_IDENTIFIER_VERSION                  0x000000FF

#define XDMA_REG_CHANNEL_CONTROL                    0x04
#define XDMA_REG_CHANNEL_CONTROL_SET                0x08 // sets bits that are set to one
#define XDMA_REG_CHANNEL_CONTROL_CLR                0x0C // clears bits that are set to one
//#define     CHANNEL_CONTROL_RESERVED                    0xF0000000
#define     CHANNEL_CONTROL_WB                          0x08000000
#define     CHANNEL_CONTROL_POLLMODE_WB_ENABLE          0x04000000
#define     CHANNEL_CONTROL_NON_INC_MODE                0x02000000
#define     CHANNEL_CONTROL_IE_DESC_ERROR               0x00F80000
#define     CHANNEL_CONTROL_IE_WRITE_ERROR              0x0007C000
#define     CHANNEL_CONTROL_IE_READ_ERROR               0x00003E00
//#define     CHANNEL_CONTROL_RESERVED                    0x00000180
#define     CHANNEL_CONTROL_IE_IDLE_STOPPED             0x00000040
#define     CHANNEL_CONTROL_IE_INVALID_LEN              0x00000020
#define     CHANNEL_CONTROL_IE_MAGIC_STOPPED            0x00000010
#define     CHANNEL_CONTROL_IE_ALIGN_MISMATCH           0x00000008
#define     CHANNEL_CONTROL_IE_DESC_COMPL               0x00000004
#define     CHANNEL_CONTROL_IE_DESC_STOPPED             0x00000002
#define     CHANNEL_CONTROL_RUN                         0x00000001

#define XDMA_REG_CHANNEL_STATUS                     0x40
#define XDMA_REG_CHANNEL_STATUS_CLEARONREAD         0x44
#define     CHANNEL_STATUS_DESC_ERR                     0x00F80000
#define     CHANNEL_STATUS_WRITE_ERR                    0x0007C000
#define     CHANNEL_STATUS_READ_ERR                     0x00003E00
#define     CHANNEL_STATUS_IDLE_STOPPED                 0x00000040
#define     CHANNEL_STATUS_INVALID_LEN                  0x00000020
#define     CHANNEL_STATUS_MAGIC_STOPPED                0x00000010
#define     CHANNEL_STATUS_ALIGN_MISMATCH               0x00000008
#define     CHANNEL_STATUS_DESC_COMPL                   0x00000004
#define     CHANNEL_STATUS_DESC_STOPPED                 0x00000002
#define     CHANNEL_STATUS_BUSY                         0x00000001

#define XDMA_REG_CHANNEL_COMPL_DESC_CNT             0x48 // completed descriptor count
#define XDMA_REG_CHANNEL_ALIGNMENTS                 0x4C
#define     CHANNEL_ALIGNMENT_ADDR_ALIGN                0x00FF0000
#define     CHANNEL_ALIGNMENT_LEN_GRANULARITY           0x0000FF00
#define     CHANNEL_ALIGNMENT_ADDR_BITS                 0x000000FF
#define XDMA_REG_CHANNEL_POLLMODE_LOW_WB_ADDR       0x88
#define XDMA_REG_CHANNEL_POLLMODE_HIGH_WB_ADDR      0x8C
#define XDMA_REG_CHANNEL_IRQ_ENABLE_MASK            0x90
#define XDMA_REG_CHANNEL_IRQ_ENABLE_MASK_SET        0x94 // sets bits that are set to one
#define XDMA_REG_CHANNEL_IRQ_ENABLE_MASK_CLR        0x98 // clears bits that are set to one
#define     CHANNEL_IRQ_ENABLE_DESC_ERR                 0x00F80000
#define     CHANNEL_IRQ_ENABLE_WRITE_ERR                0x0007C000
#define     CHANNEL_IRQ_ENABLE_READ_ERR                 0x00003E00
#define     CHANNEL_IRQ_ENABLE_IM_IDLE_STOPPED          0x00000040
#define     CHANNEL_IRQ_ENABLE_IM_INVALID_LEN           0x00000020 // H2C only
#define     CHANNEL_IRQ_ENABLE_IM_MAGIC_STOPPED         0x00000010
#define     CHANNEL_IRQ_ENABLE_IM_ALIGN_MISMATCH        0x00000008 // H2C only
#define     CHANNEL_IRQ_ENABLE_IM_DESC_COMPL            0x00000004
#define     CHANNEL_IRQ_ENABLE_IM_DESC_STOPPED          0x00000002
#define XDMA_REG_CHANNEL_PERF_MONITOR_CONTROL       0xC0
#define     CHANNEL_PERF_MONITOR_CONTROL_RUN            0x00000004 // set to 1 to arm performace counters
#define     CHANNEL_PERF_MONITOR_CONTROL_CLR            0x00000002 // write 1 to clear performance counters
#define     CHANNEL_PERF_MONITOR_CONTROL_AUTO           0x00000001 // 
#define XDMA_REG_CHANNEL_PERF_CYCLE_COUNT_LOW       0xC4
#define XDMA_REG_CHANNEL_PERF_CYCLE_COUNT_HIGH      0xC8
#define     CHANNEL_PERF_CYCLE_COUNT_MAXED              0x00010000
#define     CHANNEL_PERF_CYCLE_COUNT                    0x000003FF
#define XDMA_REG_CHANNEL_PERF_DATA_COUNT_LOW        0xCC
#define XDMA_REG_CHANNEL_PERF_DATA_COUNT_HIGH       0xD0
#define     CHANNEL_PERF_DATA_COUNT_MAXED               0x00010000
#define     CHANNEL_PERF_DATA_COUNT                     0x000003FF


// IRQBLOCK
#define XDMA_REG_IRQBLOCK_IDENTIFIER                0x00
#define     IRQBLOCK_ID_CORE                            0xFFF00000
#define     IRQBLOCK_ID_IRQ                             0x000F0000
#define     IRQBLOCK_ID_RESERVED                        0x0000FF00
#define     IRQBLOCK_ID_VERSION                         0x000000FF
#define XDMA_REG_IRQBLOCK_USR_IRQ_EN_MASK           0x04
#define XDMA_REG_IRQBLOCK_USR_IRQ_EN_MASK_SET       0x08
#define XDMA_REG_IRQBLOCK_USR_IRQ_EN_MASK_CLR       0x0C
#define XDMA_REG_IRQBLOCK_CH_IRQ_EN_MASK            0x10
#define XDMA_REG_IRQBLOCK_CH_IRQ_EN_MASK_SET        0x14
#define XDMA_REG_IRQBLOCK_CH_IRQ_EN_MASK_CLR        0x18
#define XDMA_REG_IRQBLOCK_USR_IRQ_REQUEST           0x40
#define XDMA_REG_IRQBLOCK_CH_IRQ_REQUEST            0x44
#define XDMA_REG_IRQBLOCK_USR_IRQ_PENDING           0x48
#define XDMA_REG_IRQBLOCK_CH_IRQ_PENDING            0x4C
#define XDMA_REG_IRQBLOCK_USR_VECTOR_NUMBER_30      0x80
#define XDMA_REG_IRQBLOCK_USR_VECTOR_NUMBER_74      0x84
#define XDMA_REG_IRQBLOCK_USR_VECTOR_NUMBER_118     0x88
#define XDMA_REG_IRQBLOCK_USR_VECTOR_NUMBER_1512    0x8C
#define     IRQBLOCK_USR_VECTOR_3                       0x0F000000 // TODO: constant values are the same for all four USR_VECTOR_NUMBER registers. trotzdem defines für alle?
#define     IRQBLOCK_USR_VECTOR_2                       0x000F0000
#define     IRQBLOCK_USR_VECTOR_1                       0x00000F00
#define     IRQBLOCK_USR_VECTOR_0                       0x0000000F
#define XDMA_REG_IRQBLOCK_CH_VECTOR_NUMBER_30       0xA0
#define XDMA_REG_IRQBLOCK_CH_VECTOR_NUMBER_74       0xA4
#define     IRQBLOCK_CH_VECTOR_3                        0x0F000000 // TODO: constant values are the same for both CH_VECTOR_NUMBER registers. trotzdem defines für alle?
#define     IRQBLOCK_CH_VECTOR_2                        0x000F0000
#define     IRQBLOCK_CH_VECTOR_1                        0x00000F00
#define     IRQBLOCK_CH_VECTOR_0                        0x0000000F


// CONFIG
#define XDMA_REG_CONFIG_IDENTIFIER                  0x00
#define     CONFIG_ID_CORE                              0xFFF00000
#define     CONFIG_ID_IRQ                               0x000F0000
#define     CONFIG_ID_RESERVED                          0x0000FF00
#define     CONFIG_ID_VERSION                           0x000000FF

#define XDMA_REG_CONFIG_BUSDEV                      0x04
#define     CONFIG_BUSDEV                               0x0000FFFF
#define XDMA_REG_CONFIG_PCIE_MAX_PAYLOAD            0x08
#define     CONFIG_PCIE_MAX_PAYLOAD_128                 0x0
#define     CONFIG_PCIE_MAX_PAYLOAD_256                 0x1
#define     CONFIG_PCIE_MAX_PAYLOAD_512                 0x2
#define     CONFIG_PCIE_MAX_PAYLOAD_1024                0x3
#define     CONFIG_PCIE_MAX_PAYLOAD_2048                0x4
#define     CONFIG_PCIE_MAX_PAYLOAD_4096                0x5
#define XDMA_REG_CONFIG_PCIE_MAX_READ_REQUEST       0x0C
#define     CONFIG_PCIE_READ_REQUEST_128                0x0
#define     CONFIG_PCIE_READ_REQUEST_256                0x1
#define     CONFIG_PCIE_READ_REQUEST_512                0x2
#define     CONFIG_PCIE_READ_REQUEST_1024               0x3
#define     CONFIG_PCIE_READ_REQUEST_2048               0x4
#define     CONFIG_PCIE_READ_REQUEST_4096               0x5
#define XDMA_REG_CONFIG_BLOCK_SYSTEM_ID             0x10
#define XDMA_REG_CONFIG_BLOCK_MSI_EN                0x14
#define XDMA_REG_CONFIG_PCIE_DATA_WIDTH             0x18
#define XDMA_REG_CONFIG_PCIE_CONTROL                0x1C
#define XDMA_REG_CONFIG_AXI_USER_MAX_PAYLOAD        0x40
#define XDMA_REG_CONFIG_AXI_USER_MAX_READ_REQUEST   0x44
#define XDMA_REG_CONFIG_WRITE_FLUSH_TIMEOUT         0x60


// SGDMA (H2C and C2H)
#define XDMA_REG_CHANNEL_SGDMA_IDENTIFIER           0x00
#define     CHANNEL_SGDMA_ID_CORE                       0xFFF00000
#define     CHANNEL_SGDMA_ID_DMA_TARGET                 0x000F0000
#define     CHANNEL_SGDMA_ID_STREAM                     0x00008000
#define     CHANNEL_SGDMA_ID_RESERVED                   0x00007000
#define     CHANNEL_SGDMA_ID_CH_ID_TARGET               0x00000F00
#define     CHANNEL_SGDMA_ID_VERSION                    0x000000FF

#define XDMA_REG_CHANNEL_SGDMA_LOW_ADDR             0x80 // lower 32 bits of start descriptor address
#define XDMA_REG_CHANNEL_SGDMA_HIGH_ADDR            0x84 // upper 32 bits of start descriptor address
#define XDMA_REG_CHANNEL_SGDMA_ADJACENT             0x88 // number of adjacent descriptors (6 bits) after the start descriptor address
#define XDMA_REG_CHANNEL_SGDMA_CREDITS              0x8C


// SGDMA common
#define XDMA_REG_COMMON_SGDMA_IDENTIFIER                    0x00
#define XDMA_REG_COMMON_SGDMA_DESC_CONTROL                  0x10
#define XDMA_REG_COMMON_SGDMA_DESC_CONTROL_SET              0x14
#define XDMA_REG_COMMON_SGDMA_DESC_CONTROL_CLR              0x18
#define     SGDMA_DESC_CONTROL_HALT_C2H_3                       0x8000
#define     SGDMA_DESC_CONTROL_HALT_C2H_2                       0x4000
#define     SGDMA_DESC_CONTROL_HALT_C2H_1                       0x2000
#define     SGDMA_DESC_CONTROL_HALT_C2H_0                       0x1000
#define     SGDMA_DESC_CONTROL_HALT_H2C_3                       0x0008
#define     SGDMA_DESC_CONTROL_HALT_H2C_2                       0x0004
#define     SGDMA_DESC_CONTROL_HALT_H2C_1                       0x0002
#define     SGDMA_DESC_CONTROL_HALT_H2C_0                       0x0001
#define XDMA_REG_COMMON_SGDMA_DESC_CREDIT_MODE_ENABLE       0x20
#define     SGDMA_DESC_CREDIT_ENABLE_C2H_3                      0x8000
#define     SGDMA_DESC_CREDIT_ENABLE_C2H_2                      0x4000
#define     SGDMA_DESC_CREDIT_ENABLE_C2H_1                      0x2000
#define     SGDMA_DESC_CREDIT_ENABLE_C2H_0                      0x1000
#define     SGDMA_DESC_CREDIT_ENABLE_H2C_3                      0x0008
#define     SGDMA_DESC_CREDIT_ENABLE_H2C_2                      0x0004
#define     SGDMA_DESC_CREDIT_ENABLE_H2C_1                      0x0002
#define     SGDMA_DESC_CREDIT_ENABLE_H2C_0                      0x0001
#define XDMA_REG_COMMON_SGDMA_DESC_CREDIT_MODE_ENABLE_SET   0x24
#define XDMA_REG_COMMON_SGDMA_DESC_CREDIT_MODE_ENABLE_CLR   0x28


// MSI-X vector table and PBA
#define XDMA_REG_MSIX_VECTOR0_ADDR_LOW                      0x000
#define XDMA_REG_MSIX_VECTOR0_ADDR_HIGH                     0x004
#define XDMA_REG_MSIX_VECTOR0_DATA                          0x008
#define XDMA_REG_MSIX_VECTOR0_CONTROL                       0x00C
// repeat addr_low, addr_high, data, control for all 32 vectors
#define XDMA_REG_MSIX_PENDING_BIT_ARRAY                     0xFE0

// misc masks to simplify programming
#define SPCM4DRV_XDMA_DMA_NUMBER_OF_H2C_CHANNELS            1 // data
#define SPCM4DRV_XDMA_DMA_NUMBER_OF_C2H_CHANNELS            2 // data and timestamps
#define XDMA_IRQREQ_H2C_INT_ENG_MASK                        0x01
#define XDMA_IRQREQ_H2C_INT_ENG0                            0x01
#define XDMA_IRQREQ_C2H_INT_ENG_MASK                        0x06 // next bits after H2C mask
#define XDMA_IRQREQ_C2H_INT_ENG0                            0x02
#define XDMA_IRQREQ_C2H_INT_ENG1                            0x04
#define SPCM4DRV_XDMA_NUMBER_OF_USR_IRQ                     3 // pre, trigger, ready
#define XDMA_IRQREQ_USR_INT_ENG_MASK                        0x07


#ifndef WINVER
// do not include dlltyp.h because it generates errors due to double-defined/typedef'd "bool"
typedef unsigned ULONG;
#endif

//----------------------------------------------------------------------
// structures
//----------------------------------------------------------------------

#define SGLIST_ENTRY_CONTROL_MAGIC              0xAD4B0000 // magic number to "verify that the driver generated descriptor is valid"
#define SGLIST_ENTRY_CONTROL_EOP                0x00000010 // end of packet
#define SGLIST_ENTRY_CONTROL_IRQONCOMPL         0x00000002 // interrupt after this descriptor
#define SGLIST_ENTRY_CONTROL_STOP_FETCH_DESC    0x00000001 // stop fetching descriptors for this descriptor list
typedef struct _XDMA_SGLIST_ENTRY
    {
    ULONG dwControl;            // see SGLIST_ENTRY_CONTROL_xyz defines above
    ULONG dwLength;             // length in bytes
    ULONG dwSrcAddrLow;         // source address (H2C) or metadata writeback address (C2H)
    ULONG dwSrcAddrHigh;
    ULONG dwDestAddrLow;        // destination address (C2H), unused for H2C
    ULONG dwDestAddrHigh;
    ULONG dwNextDescAddrLow;    // address of next descriptor in list
    ULONG dwNextDescAddrHigh;
    } XDMA_SGLIST_ENTRY;

#define C2H_WRITEBACK_STATUS_MASK 0x1
#define C2H_WRITEBACK_STATUS_EOP    0x1
#define C2H_WRITEBACK_MAGIC 0x52B40000
typedef struct _XDMA_C2H_WRITEBACK
    {
    ULONG dwStatus;
    ULONG dwLength;
    } XDMA_C2H_WRITEBACK;

//----------------------------------------------------------------------
// functions
//----------------------------------------------------------------------
#ifdef WINVER
    #define XDMA_WriteByOffset( plxBase, offset, data ) \
        ( WRITE_REGISTER_ULONG( (PULONG)(plxBase + offset), data ))
    #define XDMA_ReadByOffset( plxBase, offset ) \
        ( READ_REGISTER_ULONG( (PULONG)(plxBase + offset) ))

    void XDMA_EnableLocalInt (unsigned char*);

#else

    #define XDMA_WriteByOffset( plxBase, offset, data) \
        ( iowrite32(data, ((char*)plxBase) + offset ))
    #define XDMA_ReadByOffset( plxBase, offset ) \
        ( ioread32(((char*)plxBase) + offset ))

    void XDMA_EnableLocalInt (unsigned char*);
    void XDMA_DisableInt (unsigned char*);
#endif



#endif // XDMACORE_H

