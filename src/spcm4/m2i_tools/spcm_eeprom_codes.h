/*
**************************************************************************

spcm_eeprom_codes.h                            (c) Spectrum GmbH , 09/2005

**************************************************************************

internal eeprom codes and translation routines

**************************************************************************
*/




/*
**************************************************************************
several codings for the eeprom structure
**************************************************************************
*/

// memsize coding
#define SPCM_EEMC_NOTDEFINED    0x00
#define SPCM_EEMC_FIFO_4K       0x01
#define SPCM_EEMC_MEM_64M_1x1   0x10    // 1 x 64M  memory module / 1 bank
#define SPCM_EEMC_MEM_64M_1x2   0x11    // 1 x 64M  memory module / 2 banks
#define SPCM_EEMC_MEM_128M_1x1  0x14    // 1 x 128M memory module / 1 bank
#define SPCM_EEMC_MEM_128M_1x2  0x15    // 1 x 128M memory module / 2 banks
#define SPCM_EEMC_MEM_256M_1x1  0x18    // 1 x 256M memory module / 1 bank
#define SPCM_EEMC_MEM_256M_1x2  0x19    // 1 x 256M memory module / 2 banks
#define SPCM_EEMC_MEM_512M_1x1  0x1C    // 1 x 512M memory module / 1 bank
#define SPCM_EEMC_MEM_512M_1x2  0x1D    // 1 x 512M memory module / 2 banks
#define SPCM_EEMC_MEM_512M_2x1  0x1E    // 2 x 256M memory module / 1 bank
#define SPCM_EEMC_MEM_512M_2x2  0x1F    // 2 x 256M memory module / 2 banks
#define SPCM_EEMC_MEM_1G_1x1    0x20    // 1 x 1G   memory module / 1 bank
#define SPCM_EEMC_MEM_1G_1x2    0x21    // 1 x 1G   memory module / 2 banks
#define SPCM_EEMC_MEM_1G_2x1    0x22    // 2 x 512M memory module / 1 bank
#define SPCM_EEMC_MEM_1G_2x2    0x23    // 2 x 512M memory module / 2 banks
#define SPCM_EEMC_MEM_2G_1x1    0x24    // 1 x 2G   memory module / 1 bank
#define SPCM_EEMC_MEM_2G_1x2    0x25    // 1 x 2G   memory module / 2 banks
#define SPCM_EEMC_MEM_2G_2x1    0x26    // 2 x 1G   memory module / 1 bank
#define SPCM_EEMC_MEM_2G_2x2    0x27    // 2 x 1G   memory module / 2 banks
#define SPCM_EEMC_MEM_4G_1x1    0x28    // 1 x 4G   memory module / 1 bank
#define SPCM_EEMC_MEM_4G_2x1    0x2A    // 2 x 2G   memory module / 1 bank
#define SPCM_EEMC_MEM_4G_2x2    0x2B    // 2 x 2G   memory module / 2 banks
#define SPCM_EEMC_MEM_8G_2x1    0x2C    // 2 x 4G   memory module / 1 bank
#define SPCM_EEMC_MEM_16G_2x1   0x2D    // 2 x 8G   memory module / 1 bank


// quartz coding
#define SPCM_EEQC_NONE          0x00
#define SPCM_EEQC_100k          0x10
#define SPCM_EEQC_200k          0x11
#define SPCM_EEQC_500k          0x12
#define SPCM_EEQC_800k          0x13
#define SPCM_EEQC_1M            0x14
#define SPCM_EEQC_2M            0x15
#define SPCM_EEQC_3M            0x16
#define SPCM_EEQC_4M            0x17
#define SPCM_EEQC_5M            0x18
#define SPCM_EEQC_10M           0x19
#define SPCM_EEQC_20M           0x1A
#define SPCM_EEQC_25M           0x1B
#define SPCM_EEQC_30M           0x1C
#define SPCM_EEQC_40M           0x1D
#define SPCM_EEQC_50M           0x1E
#define SPCM_EEQC_60M           0x1F
#define SPCM_EEQC_62_5M         0x20
#define SPCM_EEQC_80M           0x21
#define SPCM_EEQC_100M          0x22
#define SPCM_EEQC_125M          0x23
#define SPCM_EEQC_8M            0x24
#define SPCM_EEQC_24M           0x25
#define SPCM_EEQC_102M4         0x26
#define SPCM_EEQC_25M6          0x27
#define SPCM_EEQC_250M          0x28
#define SPCM_EEQC_400M          0x29
#define SPCM_EEQC_500M          0x30
#define SPCM_EEQC_65M           0x31
#define SPCM_EEQC_105M          0x32
#define SPCM_EEQC_180M          0x33
#define SPCM_EEQC_130M          0x34

// extra eeprom coding
#define SPCM_EEXE_NONE          0x00
#define SPCM_EEXE_93CS46        0x01
#define SPCM_EEXE_93CS66        0x03

// custom design coding
#define SPCM_EECD_NONE          0x00
#define SPCM_EECD_SYSCKHALF     0x01


// --- input range modifier
// For high input range voltages that exceeds the 16 bit storage capacity the 
// bit MSB-1 (highest before sign) is a flag that the storaged value has to be
// multiplied with a factor of 10.
// For positiv range values the bit is set and for negative values cleared!
// The standard range are from 0 to +-16383 mV
// and the modified one from 0 to +-163830 mV
#define SPCM_EEIR_GAINFACTOR10  0x4000


// translation functions for the codes
uint32 dwGetQuartzFromCode (uint8 byCode);
uint64 qwGetMemoryFromCode (uint8 byCode, uint8* pbyModules, uint8* pbyBanksPerModule);
