// ****************************************************************************
// Mapping of PLX9080/PLX9656 
// ****************************************************************************

typedef struct 
    {
    /* 0x00 */    uint32    dwSpace0Range;
    /* 0x04 */    uint32    dwSpace0Remap;
    /* 0x08 */    uint32    dwModeArbitration;
    /* 0x0C */    uint32    dwBigLittleEndianDescr;
    /* 0x10 */    uint32    dwExpansionROMRange;
    /* 0x14 */    uint32    dwExpansionROMRemap;
    /* 0x18 */    uint32    dwSpace0RegionDesr;
    /* 0x1C */    uint32    dwDirectMasterRange;
    /* 0x20 */    uint32    dwDirectMasterBaseMem;
    /* 0x24 */    uint32    dwDirectMasterBaseIO;
    /* 0x28 */    uint32    dwDirectMasterRemapMem;
    /* 0x2C */    uint32    dwDirectMasterRemapIO;
    /* 0x30 */    uint32    dwI2O_OutIRStatus;
    /* 0x34 */    uint32    dwI2O_OutIRMask;
    /* 0x38 */    uint32    dwNotUsed0;
    /* 0x3C */    uint32    dwnotUsed1;
    /* 0x40 */    uint32    dwMailbox0;            // bei I20 Inbound Queue
    /* 0x44 */    uint32    dwMailbox1;            // bei I2O Outbound Queue
    /* 0x48 */    uint32    dwMailbox2;
    /* 0x4C */    uint32    dwMailbox3;
    /* 0x50 */    uint32    dwMailbox4;
    /* 0x54 */    uint32    dwMailbox5;
    /* 0x58 */    uint32    dwMailbox6;
    /* 0x5C */    uint32    dwMailbox7;
    /* 0x60 */    uint32    dwPCIToLocalDoorbell;
    /* 0x64 */    uint32    dwLocalToPCIDoorbell;
    /* 0x68 */    uint32    dwIRControlStatus;
    /* 0x6C */    uint32    dwEEPromCmdInit;
    /* 0x70 */    uint16    wVendorID, wDeviceID;
    /* 0x74 */    uint16    wRevisionID, wUnused;
    /* 0x78 */    uint32    dwMailbox0Q;
    /* 0x7C */    uint32    dwMailbox1Q;
    /* 0x80 */    uint32    dwDMACh0Mode;
    /* 0x84 */    uint32    dwDMACh0PCIAdr;
    /* 0x88 */    uint32    dwDMACh0LocalAdr;
    /* 0x8C */    uint32    dwDMACh0TransferLen;
    /* 0x90 */    uint32    dwDMACh0DescrPtr;
    /* 0x94 */    uint32    dwDMACh1Mode;
    /* 0x98 */    uint32    dwDMACh1PCIAdr;
    /* 0x9C */    uint32    dwDMACh1LocalAdr;
    /* 0xA0 */    uint32    dwDMACh1TransferLen;
    /* 0xA4 */    uint32    dwDMACh1DescrPtr;
    /* 0xA8 */    uint32    dwDMACommandStatus;
    /* 0xAC */    uint32    dwDMAModeArbitration;
    /* 0xB0 */    uint32    dwDMAThreshold;
    /* 0xB4 */    uint32    dwNotUsed2;
    /* 0xB8 */    uint32    dwNotUsed3;
    /* 0xBC */    uint32    dwNotUsed4;
    /* 0xC0 */    uint32    dwI2O_MessageConfig;
    /* 0xC4 */    uint32    dwI2O_QueueBaseAdr;
    /* 0xC8 */    uint32    dwI2O_InFreeHead;
    /* 0xCC */    uint32    dwI2O_InFreeTail;
    /* 0xD0 */    uint32    dwI2O_InPostHead;
    /* 0xD4 */    uint32    dwI2O_InPostTail;
    /* 0xD8 */    uint32    dwI2O_OutFreeHead;
    /* 0xDC */    uint32    dwI2O_OutFreeTail;
    /* 0xE0 */    uint32    dwI2O_OutPostHead;
    /* 0xE4 */    uint32    dwI2O_OutPostTail;
    /* 0xE8 */    uint32    dwI2O_QueueStatus;
    /* 0xEC */    uint32    dwNotUsed5;
    /* 0xF0 */    uint32    dwSpace1Range;
    /* 0xF4 */    uint32    dwSpace1Remap;
    /* 0xF8 */    uint32    dwSpace1RegionDescr;
    /* 0xFC */    uint32    dwNotUsed6;
    } STPLX9080CFG, *PSTPLX9080CFG;


typedef struct 
    {
    /* 0x00 */  uint32  dwLocalAddressSpace0Range;
    /* 0x04 */  uint32  dwLocalAddressSpace0Remap;
    /* 0x08 */  uint32  dwModeArbitration;
    /* 0x0C */  uint8   byBigLittleEndianDescr;
    /* 0x0D */  uint8   byLocalMiscControl1;
    /* 0x0E */  uint8   byEEPromWriteProtect;
    /* 0x0F */  uint8   byLocalMiscControl0;
    /* 0x10 */  uint32  dwExpansionROMRange;
    /* 0x14 */  uint32  dwExpansionROMRemap;
    /* 0x18 */  uint32  dwLocalAddressSpace0RegionDesr;
    /* 0x1C */  uint32  dwDirectMasterRange;
    /* 0x20 */  uint32  dwDirectMasterBaseMem;
    /* 0x24 */  uint32  dwDirectMasterBaseIO;
    /* 0x28 */  uint32  dwDirectMasterRemapMem;
    /* 0x2C */  uint32  dwDirectMasterRemapIO;
    /* 0x30 */  uint32  dwI2O_OutIRStatus;
    /* 0x34 */  uint32  dwI2O_OutIRMask;
    /* 0x38 */  uint32  dwNotUsed0;
    /* 0x3C */  uint32  dwnotUsed1;
    /* 0x40 */  uint32  dwMailbox0_I20InQueue;
    /* 0x44 */  uint32  dwMailbox1_I20OutQueue;
    /* 0x48 */  uint32  dwMailbox2;
    /* 0x4C */  uint32  dwMailbox3;
    /* 0x50 */  uint32  dwMailbox4;
    /* 0x54 */  uint32  dwMailbox5;
    /* 0x58 */  uint32  dwMailbox6;
    /* 0x5C */  uint32  dwMailbox7;
    /* 0x60 */  uint32  dwPCIToLocalDoorbell;
    /* 0x64 */  uint32  dwLocalToPCIDoorbell;
    /* 0x68 */  uint32  dwIRControlStatus;
    /* 0x6C */  uint32  dwEEPromCmdInit;
    /* 0x70 */  uint16  wVendorID, wDeviceID;
    /* 0x74 */  uint16  wRevisionID, wUnused;
    /* 0x78 */  uint32  dwMailbox0;
    /* 0x7C */  uint32  dwMailbox1;
    /* 0x80 */  uint32  dwDMACh0Mode;
    /* 0x84 */  uint32  dwDMACh0PCIAdr;
    /* 0x88 */  uint32  dwDMACh0LocalAdr;
    /* 0x8C */  uint32  dwDMACh0TransferLen;
    /* 0x90 */  uint32  dwDMACh0DescrPtr;
    /* 0x94 */  uint32  dwDMACh1Mode;
    /* 0x98 */  uint32  dwDMACh1PCIAdr;
    /* 0x9C */  uint32  dwDMACh1LocalAdr;
    /* 0xA0 */  uint32  dwDMACh1TransferLen;
    /* 0xA4 */  uint32  dwDMACh1DescrPtr;
    /* 0xA8 */  uint32  dwDMACommandStatus;
    /* 0xAC */  uint32  dwDMAModeArbitration;
    /* 0xB0 */  uint32  dwDMAThreshold;
    /* 0xB4 */  uint32  dwDMACh0UpperAddress;
    /* 0xB8 */  uint32  dwDMACh1UpperAddress;
    /* 0xBC */  uint32  dwNotUsed4;
    /* 0xC0 */  uint32  dwI2O_MessageConfig;
    /* 0xC4 */  uint32  dwI2O_QueueBaseAdr;
    /* 0xC8 */  uint32  dwI2O_InFreeHead;
    /* 0xCC */  uint32  dwI2O_InFreeTail;
    /* 0xD0 */  uint32  dwI2O_InPostHead;
    /* 0xD4 */  uint32  dwI2O_InPostTail;
    /* 0xD8 */  uint32  dwI2O_OutFreeHead;
    /* 0xDC */  uint32  dwI2O_OutFreeTail;
    /* 0xE0 */  uint32  dwI2O_OutPostHead;
    /* 0xE4 */  uint32  dwI2O_OutPostTail;
    /* 0xE8 */  uint32  dwI2O_QueueStatus;
    /* 0xEC */  uint32  dwNotUsed5;
    /* 0xF0 */  uint32  dwLocalAddressSpace1Range;
    /* 0xF4 */  uint32  dwLocalAddressSpace1Remap;
    /* 0xF8 */  uint32  dwLocalAddressSpace1RegionDescr;
    /* 0xFC */  uint32  dwDirectMasterDualAddressUpper;
    /* 0x100*/  uint32  dwPCIArbiterControl;
    /* 0x104*/  uint32  dwPCIAbortAddress;
    } STPLX9956CFG, *PSTPLX9656CFG;



// ----- bit defines for the interrupt control/status -----
#define PLXIRQSC_PCIIRQ_ENABLE          0x00000100
#define PLXIRQSC_LOCIRQ_ENABLE          0x00000800
#define PLXIRQSC_LOCIRQ_ACTIVE          0x00008000
#define PLXIRQSC_LOCIRQ_OPEN            0x00010000
#define PLXIRQSC_DMA0_ENABLE            0x00040000
#define PLXIRQSC_DMA1_ENABLE            0x00080000
#define PLXIRQSC_DMA0_ACTIVE            0x00200000
#define PLXIRQSC_DMA1_ACTIVE            0x00400000

