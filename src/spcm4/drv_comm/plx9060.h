/* ************************************************************************ */
/* * PLX9060.H - definitions for the PCI9060 chip from PLX technology       */
/* ************************************************************************ */
/* (c) Spectrum GmbH 04/97                                                  */
/* ************************************************************************ */

#define PLX_VENDOR_ID           0x10b5
#define PLX_DEVICE_ID             0x9060
#define PLX_DEVICE_ID_ES        0x906E
#define PLX_DEVICE_ID_SD        0x906D
#define PLX_DEVICE_ID_9080      0x9080
#define PLX_DEVICE_ID_9656      0x9656

#define    SPC_VENDOR_ID           0x18F1


#define PLXREG_MAIL0            0x0040
#define PLXREG_MAIL1            0x0044
#define PLXREG_MAIL2            0x0048
#define PLXREG_MAIL3            0x004C
#define PLXREG_MAIL4            0x0050
#define PLXREG_MAIL5            0x0054
#define PLXREG_MAIL6            0x0058
#define PLXREG_MAIL7            0x005C
#define PLXREG_PCILOC_DOOR      0x0060
#define PLXREG_LOCPCI_DOOR      0x0064
#define PLXREG_STATUS           0x0068
#define PLXREG_EEPROM           0x006E

#define EE_MASK                 0x1F00
#define EE_CLK                  0x0100
#define EE_CS                   0x0200
#define EE_DI                   0x0400
#define EE_DO                   0x0800
#define EE2_DI                  0x0001
#define EE2_DO                  0x0002
#define EE_PRESENT              0x1000
#define EE_RELOAD               0x2000
#define PLX_SW_RESET            0x4000
#define PLX_LOCAL_INIT          0x8000

#define EE2MC_CLK                0x0001
#define EE2MC_CS                0x0002
#define EE2MC_DI                0x0004
#define EE2MC_DO                0x0008


/* 
**************************************************************************
additional defines for the PCI9656
**************************************************************************
*/

// ----- control registers -----
#define PLX9656_REG_MISCCONTROL     0x0000000c  // Little/BigEndian, MiscControl, EEProm WriteProt, MiscControl2
#define     PLX9656_EEPROT_MASK     0x007F0000

#define PLX9656_REG_EECNTRL         0x0000006c
#define     PLX9656_EEC_USEROUT     0x00010000
#define     PLX9656_EEC_USERIN      0x00020000
#define     PLX9656_EEC_EECLK       0x01000000
#define     PLX9656_EEC_EECS        0x02000000
#define     PLX9656_EEC_EEDI_OUT    0x04000000  // input of eeprom, PLX output
#define     PLX9656_EEC_EEDO_IN     0x08000000  // output of eeprom, PLX input
#define     PLX9656_EEC_EEPRESENT   0x10000000
#define     PLX9656_EEC_EERELOAD    0x20000000
#define     PLX9656_EEC_SWRESET     0x40000000
#define     PLX9656_EEC_EEINENABLE  0x80000000

#define PLX9656_REG_MAILBOX0        0x00000078
#define PLX9656_REG_MAILBOX1        0x0000007C


// ----- config space registers (vpd) -----
#define PLX9656_VPD_CONTROL         0x0000004c
#define     PLX9656_VPDC_FBIT       0x80000000
#define     PLX9656_VPDC_ADRMASK    0x7FFF0000

#define PLX9656_VPD_DATA            0x00000050

