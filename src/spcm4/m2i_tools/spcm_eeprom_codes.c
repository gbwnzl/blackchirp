/*
**************************************************************************

spcm_eeprom_code.cpp                           (c) Spectrum GmbH , 09/2005

**************************************************************************

translation functions for eeprom codes

**************************************************************************
*/

// ----- driver dll includes -----
#include "../c_header/dlltyp.h"
#include "spcm_eeprom_codes.h"




/*
**************************************************************************
GetMemoryFromCode: recalculates memory code to real world value
**************************************************************************
*/

uint64 qwGetMemoryFromCode (uint8 byCode, uint8* pbyMods, uint8* pbyBanks)
    {
    switch (byCode)
        {
        case SPCM_EEMC_FIFO_4K:         (*pbyMods) = 1; (*pbyBanks) = 1; return (uint64)               4 * 1024;

        case SPCM_EEMC_MEM_64M_1x1:     (*pbyMods) = 1; (*pbyBanks) = 1; return (uint64)       64 * 1024 * 1024;
        case SPCM_EEMC_MEM_64M_1x2:     (*pbyMods) = 1; (*pbyBanks) = 2; return (uint64)       64 * 1024 * 1024;

        case SPCM_EEMC_MEM_128M_1x1:    (*pbyMods) = 1; (*pbyBanks) = 1; return (uint64)      128 * 1024 * 1024;
        case SPCM_EEMC_MEM_128M_1x2:    (*pbyMods) = 1; (*pbyBanks) = 2; return (uint64)      128 * 1024 * 1024;

        case SPCM_EEMC_MEM_256M_1x1:    (*pbyMods) = 1; (*pbyBanks) = 1; return (uint64)      256 * 1024 * 1024;
        case SPCM_EEMC_MEM_256M_1x2:    (*pbyMods) = 1; (*pbyBanks) = 2; return (uint64)      256 * 1024 * 1024;

        case SPCM_EEMC_MEM_512M_1x1:    (*pbyMods) = 1; (*pbyBanks) = 1; return (uint64)      512 * 1024 * 1024;
        case SPCM_EEMC_MEM_512M_1x2:    (*pbyMods) = 1; (*pbyBanks) = 2; return (uint64)      512 * 1024 * 1024;
        case SPCM_EEMC_MEM_512M_2x1:    (*pbyMods) = 2; (*pbyBanks) = 1; return (uint64)      512 * 1024 * 1024;
        case SPCM_EEMC_MEM_512M_2x2:    (*pbyMods) = 2; (*pbyBanks) = 2; return (uint64)      512 * 1024 * 1024;

        case SPCM_EEMC_MEM_1G_1x1:      (*pbyMods) = 1; (*pbyBanks) = 1; return (uint64)     1024 * 1024 * 1024;
        case SPCM_EEMC_MEM_1G_1x2:      (*pbyMods) = 1; (*pbyBanks) = 2; return (uint64)     1024 * 1024 * 1024;
        case SPCM_EEMC_MEM_1G_2x1:      (*pbyMods) = 2; (*pbyBanks) = 1; return (uint64)     1024 * 1024 * 1024;
        case SPCM_EEMC_MEM_1G_2x2:      (*pbyMods) = 2; (*pbyBanks) = 2; return (uint64)     1024 * 1024 * 1024;

        case SPCM_EEMC_MEM_2G_1x1:      (*pbyMods) = 1; (*pbyBanks) = 1; return (uint64) 2 * 1024 * 1024 * 1024;
        case SPCM_EEMC_MEM_2G_1x2:      (*pbyMods) = 1; (*pbyBanks) = 2; return (uint64) 2 * 1024 * 1024 * 1024;
        case SPCM_EEMC_MEM_2G_2x1:      (*pbyMods) = 2; (*pbyBanks) = 1; return (uint64) 2 * 1024 * 1024 * 1024;
        case SPCM_EEMC_MEM_2G_2x2:      (*pbyMods) = 2; (*pbyBanks) = 2; return (uint64) 2 * 1024 * 1024 * 1024;

        case SPCM_EEMC_MEM_4G_1x1:      (*pbyMods) = 1; (*pbyBanks) = 1; return (uint64) 4 * 1024 * 1024 * 1024;
        case SPCM_EEMC_MEM_4G_2x1:      (*pbyMods) = 2; (*pbyBanks) = 1; return (uint64) 4 * 1024 * 1024 * 1024;
        case SPCM_EEMC_MEM_4G_2x2:      (*pbyMods) = 2; (*pbyBanks) = 2; return (uint64) 4 * 1024 * 1024 * 1024;

        case SPCM_EEMC_MEM_8G_2x1:      (*pbyMods) = 2; (*pbyBanks) = 1; return (uint64) 8 * 1024 * 1024 * 1024;
        case SPCM_EEMC_MEM_16G_2x1:     (*pbyMods) = 2; (*pbyBanks) = 1; return (uint64)16 * 1024 * 1024 * 1024;

        default:
        case SPCM_EEMC_NOTDEFINED:      (*pbyMods) = 1; (*pbyBanks) = 1; return 0;
        }
    }



/*
**************************************************************************
GetQuartzFromCode: recalculates quartz code to real world value
**************************************************************************
*/

uint32 dwGetQuartzFromCode (uint8 byCode)
    {
    switch (byCode)
        {
        case SPCM_EEQC_NONE:  return         0; break;
        case SPCM_EEQC_100k:  return    100000; break;
        case SPCM_EEQC_200k:  return    200000; break;
        case SPCM_EEQC_500k:  return    500000; break;
        case SPCM_EEQC_800k:  return    800000; break;
        case SPCM_EEQC_1M:    return   1000000; break;
        case SPCM_EEQC_2M:    return   2000000; break;
        case SPCM_EEQC_3M:    return   3000000; break;
        case SPCM_EEQC_4M:    return   4000000; break;
        case SPCM_EEQC_5M:    return   5000000; break;
        case SPCM_EEQC_8M:    return   8000000; break;
        case SPCM_EEQC_10M:   return  10000000; break;
        case SPCM_EEQC_20M:   return  20000000; break;
        case SPCM_EEQC_24M:   return  24000000; break;
        case SPCM_EEQC_25M:   return  25000000; break;
        case SPCM_EEQC_25M6:  return  25600000; break;
        case SPCM_EEQC_30M:   return  30000000; break;
        case SPCM_EEQC_40M:   return  40000000; break;
        case SPCM_EEQC_50M:   return  50000000; break;
        case SPCM_EEQC_60M:   return  60000000; break;
        case SPCM_EEQC_62_5M: return  62500000; break;
        case SPCM_EEQC_80M:   return  80000000; break;
        case SPCM_EEQC_100M:  return 100000000; break;
        case SPCM_EEQC_102M4: return 102400000; break;
        case SPCM_EEQC_125M:  return 125000000; break;
        case SPCM_EEQC_250M:  return 250000000; break;
        case SPCM_EEQC_400M:  return 400000000; break;
        case SPCM_EEQC_500M:  return 500000000; break;

        default:              return 1; break;
        }
    }

