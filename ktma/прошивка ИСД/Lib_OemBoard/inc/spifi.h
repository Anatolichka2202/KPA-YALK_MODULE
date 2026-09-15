/*****************************************************************************
 *   spifi.h:  Header file for NXP LPC177x_8x Family Microprocessors
 *
 *   Copyright(C) 2009, NXP Semiconductor
 *   All rights reserved.
 *
 *   History
 *   2009.05.26  ver 1.00    Prelimnary version, first Release
 *
******************************************************************************/
#ifndef __SPIFI_H 
#define __SPIFI_H

/*
#define SST26VF016					0
#define W25Q16V						1
#if W25Q16V
#define FAST_READ_QUAD_OUTPUT_MODE	0
#define FAST_READ_QUAD_IO_MODE		1
#endif
*/


#define FRAMEFORM					21
#define PS                          19
#define INTLEN                      16
#define DOUT                        15
#define POLLRS                      14
#define DATALEN                     0



#define SPIFI_BASE                  0x28000000

extern void SPIFI_IRQHandler( void ); 

extern void spifi_init( void );
extern void spifi_enableQuadIO(void);
extern void spifi_disableQuadIO(void);
extern void spifi_readQuadJID( uint8_t *buf );
extern void spifi_programPage( uint32_t address, uint8_t *buf );
extern void spifi_memoryCommand(void);
extern void spifi_resetMemory(void);
extern void spifi_readUniqueId( uint8_t *buf );
extern void spifi_readPage( uint32_t address, uint8_t *buf );

extern void SPIFIReadStatus( uint8_t *buf );
extern uint8_t SPIFIReadStatusWriteEnabled( void );
extern uint8_t SPIFIReadStatusWriteDone( void );
extern void SPIFIReadBlockProtection( uint8_t *buf );
extern void SPIFIWriteBlockProtection( uint8_t *buf );
extern void SPIFIEraseSector( uint32_t address );
extern void SPIFIEraseBlock( uint32_t address );
extern void SPIFIEraseChip( void );


#endif /* end __SPIFI_H */
/*****************************************************************************
**                            End Of File
******************************************************************************/
