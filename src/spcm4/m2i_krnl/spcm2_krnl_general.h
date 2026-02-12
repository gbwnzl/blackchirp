#ifndef SPCM2_KRNL_GENERAL_H
#define SPCM2_KRNL_GENERAL_H

//######################################################################
//
//  Module:     spcm2_krnl_general.h
//
//  Descript.:  Common constants for M2i/M3i and M4i driver.
//
//######################################################################


//----------------------------------------------------------------------
// command definition for SPCM2_readWriteList function
//----------------------------------------------------------------------

typedef enum
    {
    ListEnd = 0,       // since M4i
    ReadPlx = 1,
    WritePlx = 2,
    ReadDMAReg = 1,    // for M4i: North West Core (DMA)
    WriteDMAReg = 2,
    ReadLocal = 3,
    WriteLocal = 4,
    ReadPciConfig = 5,
    WritePciConfig = 6,
    Delay_us = 7,       // in units of 1 us
    ReadNetbox = 8,     // read infos that are not stored in card
    WriteNetbox = 9     // set features that are not part of card
    } SPCM2_LISTCMD;


//----------------------------------------------------------------------
// constants to use at SPCM2_configReadTransfer as parameter busWidth
//----------------------------------------------------------------------

typedef enum
    {
    BUS_WIDTH_8 = 0,
    BUS_WIDTH_16_L = 1,
    BUS_WIDTH_16_H = 2,
    BUS_WIDTH_32 = 3
    } SPCM2_DMA_BUSWIDTH;



//----------------------------------------------------------------------
// EVENT_TYPE, used as parameter at SPCM2_getEvent
//----------------------------------------------------------------------

typedef enum
    {
    DMA_CH0 = 0,
    DMA_CH1 = 1,
    LOCAL_INT = 2
    } SPCM2_EVENT_TYPE;



//----------------------------------------------------------------------
// constants to use at DMA read/write function as parameter channel
//----------------------------------------------------------------------

typedef enum
    {
    DMA_CHANNEL0 = 0,
    DMA_CHANNEL1 = 1
    } SPCM2_DMA_CHANNEL;

// For M4i:
typedef enum
    {
    DMA_C2S_CHANNEL0 = 0,
    DMA_C2S_CHANNEL1 = 1,
    DMA_C2S_CHANNEL2 = 2,
    DMA_C2S_CHANNEL3 = 3,
    DMA_S2C_CHANNEL0 = 4,
    DMA_S2C_CHANNEL1 = 5,
    DMA_S2C_CHANNEL2 = 6,
    DMA_S2C_CHANNEL3 = 7,

    // card to gpu
    DMA_C2G_CHANNEL0 = 8,
    DMA_C2G_CHANNEL1 = 9,
    DMA_C2G_CHANNEL2 = 10,
    DMA_C2G_CHANNEL3 = 11,

    // gpu to card
    DMA_G2C_CHANNEL0 = 12,
    DMA_G2C_CHANNEL1 = 13,
    DMA_G2C_CHANNEL2 = 14,
    DMA_G2C_CHANNEL3 = 15
    } SPCM4_DMA_CHANNEL;

#endif

