/*
**************************************************************************

spcm_linux_isr.h                              (c) Spectrum GmbH,  09/2006

**************************************************************************

handles the interrupts

**************************************************************************
*/

// connection of ISR
int nConnectISR (SPCM_ST_CARDINFO* pstCard);
void vDisConnectISR (SPCM_ST_CARDINFO* pstCard);
