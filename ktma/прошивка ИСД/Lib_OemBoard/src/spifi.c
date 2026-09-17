/*****************************************************************************
 *
 *   Copyright(C) 2011, Embedded Artists AB
 *   All rights reserved.
 *
 ******************************************************************************
 * Software that is described herein is for illustrative purposes only
 * which provides customers with programming information regarding the
 * products. This software is supplied "AS IS" without any warranties.
 * Embedded Artists AB assumes no responsibility or liability for the
 * use of the software, conveys no license or title under any patent,
 * copyright, or mask work right to the product. Embedded Artists AB
 * reserves the right to make changes in the software without
 * notification. Embedded Artists AB also make no representation or
 * warranty that such application will be suitable for the specified
 * use without further testing or modification.
 *****************************************************************************/
 
/******************************************************************************
 * Includes
 *****************************************************************************/


#include "lpc_types.h"
#include "spifi.h"

/******************************************************************************
 * Defines and typedefs
 *****************************************************************************/

/* SPIFI control register offset (bits) */
#define CTRL_FSCKI  27
#define CTRL_CSHI   24
#define CTRL_MODE3  23
#define CTRL_INTEN  22
#define CTRL_CLRID  21
#define CTRL_TO      5
#define CTRL_AMSB    0

/* commands */
#define CMD_READ_ID                   0x9F000000
#define CMD_FAST_READ_QUAD_OUTPUT		  0x6B000000
#define CMD_FAST_READ_QUAD_IO			    0xEB000000
#define CMD_CONTINUOUS_READ_MODE_RST	0xFF000000
#define CMD_WRSR						          0x01000000
#define CMD_RDSR1						          0x05000000
#define CMD_RDSR2						          0x35000000
#define CMD_JID                       0x9F000000
#define CMD_SECTOR_ERASE				      0x20000000
#define CMD_BLOCK_ERASE					      0xD8000000
#define CMD_CHIP_ERASE					      0xC7000000
#define CMD_WREN						          0x06000000
#define CMD_WRDI						          0x04000000
#define CMD_QUAD_PAGE_PROGRAM			    0x32000000
#define CMD_UNIQUE_ID			            0x4B000000

/******************************************************************************
 * External global variables
 *****************************************************************************/

/******************************************************************************
 * Local variables
 *****************************************************************************/

/******************************************************************************
 * Local Functions
 *****************************************************************************/

static void pinConfig(void) 
{
  LPC_IOCON->P2_7 |= 0x05; /* SPIFI_CSN @ P2.7 */
  LPC_IOCON->P0_22 |= 0x05; /* SPIFI_CLK @ P0.22 */
  LPC_IOCON->P0_15 |= 0x5; /* SPIFI_IO2 @ P0.15 */
  LPC_IOCON->P0_16 |= 0x5; /* SPIFI_IO3 @ P0.16 */
  LPC_IOCON->P0_17 |= 0x5; /* SPIFI_IO1 @ P0.17 */
  LPC_IOCON->P0_18 |= 0x5; /* SPIFI_IO0 @ P0.18 */
}


static void spifi_readStatus( uint8_t *buf )
{

  LPC_SPIFI->Command = CMD_RDSR1
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x1 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  *buf++ = LPC_SPIFI->Data;

  LPC_SPIFI->Command = CMD_RDSR2
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x1 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  *buf++ = LPC_SPIFI->Data;

  return;
}

/******************************************************************************
 * Public Functions
 *****************************************************************************/

void SPIFI_IRQHandler (void) 
{  
  uint32_t data;

  data = LPC_SPIFI->Status; /* clear interrupt flag */

  return;
}


void spifi_init(void)
{
  uint32_t deviceId = 0;
  uint32_t status = 0;
  
  
  LPC_SC->PCONP |= (1 << 16);

  pinConfig();

  LPC_SPIFI->Control = (0x0 << CTRL_FSCKI)
						  | (0x0 << CTRL_CSHI)
						  | (0x0 << CTRL_MODE3)
						  | (0x1 << CTRL_INTEN)
						  | (0x1 << CTRL_CLRID)
						  | (0xFFFF << CTRL_TO)
              /* 32 MBit */
						  | (0x15 << CTRL_AMSB);

  NVIC_EnableIRQ(SPIFI_IRQn);

//  SPIFIEnableQuadIO();

#if 1
  LPC_SPIFI->Command = CMD_READ_ID
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x3 << DATALEN);

  do {
    status = LPC_SPIFI->Status;
  } while ((status & 0x2) == 0x2);
//  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  deviceId = LPC_SPIFI->Data;

  if (deviceId & 0xfe) {
    deviceId = deviceId;
  }
#endif


  return;
}

/*****************************************************************************
** Function name:		SPIFIEnableQuadIO
**
** Descriptions:		Enable Quad I/O
**
** parameters:			None			 
** 						
** Returned value:		None
** 
*****************************************************************************/
void spifi_enableQuadIO( void )
{
  uint8_t data[2];

  /* 
   * First read status registers, set the enable quad bit,
   * write enable and finally write to the status register
   */

  LPC_SPIFI->Command = CMD_RDSR1
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x1 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = LPC_SPIFI->Data;

  LPC_SPIFI->Command = CMD_RDSR2
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x1 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[1] = LPC_SPIFI->Data;
  data[1] |= 0x2;

  LPC_SPIFI->Command = CMD_WREN
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  LPC_SPIFI->Command = CMD_WRSR
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x1 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x2 << DATALEN);

  LPC_SPIFI->Data = (data[1] << 8) | data[0];
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);


  return;
}

/*****************************************************************************
** Function name:		SPIFIDisableQuadIO
**
** Descriptions:		Disable Quad I/O
**
** parameters:			None			 
** 						
** Returned value:		None
** 
*****************************************************************************/
void spifi_disableQuadIO(void)
{
  uint8_t data[2];

  /* 
   * First read status registers, disable the enable quad bit,
   * write enable and finally write to the status register
   */

  LPC_SPIFI->Command = CMD_RDSR1
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x1 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = LPC_SPIFI->Data;

  LPC_SPIFI->Command = CMD_RDSR2
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x1 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[1] = LPC_SPIFI->Data;
  data[1] &= ~0x02;

  LPC_SPIFI->Command = CMD_WREN
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  LPC_SPIFI->Command = CMD_WRSR
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x1 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x2 << DATALEN);

  LPC_SPIFI->Data = (data[1] << 8) | data[0];
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  return;
}

/*****************************************************************************
** Function name:		SPIFIReadQuadJID
**
** Descriptions:		Read Quad JID
**
** parameters:			buf			 
** 						
** Returned value:		None
** 
*****************************************************************************/
void spifi_readQuadJID( uint8_t *buf )
{

  LPC_SPIFI->Command = CMD_JID
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x3 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  *(uint32_t *)buf = LPC_SPIFI->Data;
  
}

/*****************************************************************************
** Function name:		SPIFIProgramPage
**
** Descriptions:		Program one page of 256 bytes
**
** parameters:			address and buf			 
** 						
** Returned value:		None
** 
*****************************************************************************/
void spifi_programPage( uint32_t address, uint8_t *buf ) 
{
  uint32_t i;


  LPC_SPIFI->Command = CMD_WREN
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);


  LPC_SPIFI->Address = address;

  LPC_SPIFI->Command = CMD_QUAD_PAGE_PROGRAM
						  | (0x4 << FRAMEFORM)
						  | (0x1 << PS)
						  | (0x0 << INTLEN)
						  | (0x1 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x100 << DATALEN);

  for (i = 0; i < 0x40; i++)
  {
    LPC_SPIFI->Data = *(uint32_t *)(buf + (i * 4));
  }

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

}


void spifi_readPage( uint32_t address, uint8_t *buf )
{
  int i = 0;

  LPC_SPIFI->Address = address;

  LPC_SPIFI->Command = CMD_FAST_READ_QUAD_IO
						  | (0x4 << FRAMEFORM)
						  | (0x1 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x100 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  for (i = 0; i < 0x40; i++)
  {
    //LPC_SPIFI->Data = *(uint32_t *)(buf + (i * 4));
     *(uint32_t *)(buf + (i * 4)) = LPC_SPIFI->Data;
  }

  *(uint32_t *)buf = LPC_SPIFI->Data;
  
}

/*****************************************************************************
** Function name:		SPIFIMemoryCommand
**
** Descriptions:		Memory Command
**
** parameters:			None			 
** 						
** Returned value:		None
** 
*****************************************************************************/
void spifi_memoryCommand( void )
{

  LPC_SPIFI->IntermediateData = 0x0;

  LPC_SPIFI->MemoryCommand = /*CMD_FAST_READ_QUAD_IO
								|*/ (0x4 << FRAMEFORM)
//								| (0x2 << PS)
//								| (0x3 << INTLEN)
								| (0x0 << DOUT)
								| (0x0 << POLLRS)
								| (0x3FFF << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);


  return;
}

/*****************************************************************************
** Function name:		SPIFIResetMemory
**
** Descriptions:		Reset Memory
**
** parameters:			None			 
** 						
** Returned value:		None
** 
*****************************************************************************/
void spifi_resetMemory(void)
{

  LPC_SPIFI->Command = CMD_CONTINUOUS_READ_MODE_RST
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

}

void spifi_readUniqueId( uint8_t *buf )
{

  LPC_SPIFI->Command = CMD_UNIQUE_ID
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0C << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  
  *(uint32_t *)buf = LPC_SPIFI->Data;
  *(uint32_t *)buf = LPC_SPIFI->Data;
  *(uint32_t *)(&buf[4]) = LPC_SPIFI->Data;
  
}

#if 0



/*****************************************************************************
** Function name:		SPIFIEraseSector
**
** Descriptions:		Sector Erase
**
** parameters:			address			 
** 						
** Returned value:		None
** 
*****************************************************************************/
void SPIFIEraseSector( uint32_t address ) 
{
  uint8_t data[2];

#if SST26VF016
  LPC_SPIFI->Command = WREN
						  | (0x1 << FRAMEFORM)
						  | (0x3 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteEnabled();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x02) == 0x00)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }

  LPC_SPIFI->Address = address;

  LPC_SPIFI->Command = SECTOR_ERASE
						  | (0x4 << FRAMEFORM)
						  | (0x3 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteDone();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x80) == 0x80)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }
#endif

#if W25Q16V
  LPC_SPIFI->Command = WREN
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteEnabled();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x02) == 0x00)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }

  LPC_SPIFI->Address = address;

  LPC_SPIFI->Command = SECTOR_ERASE
						  | (0x4 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteDone();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x80) == 0x80)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }
#endif

  return;
}

/*****************************************************************************
** Function name:		SPIFIEraseBlock
**
** Descriptions:		Block Erase
**
** parameters:			address			 
** 						
** Returned value:		None
** 
*****************************************************************************/
void SPIFIEraseBlock( uint32_t address ) 
{
  uint8_t data[2];

#if SST26VF016
  LPC_SPIFI->Command = WREN
						  | (0x1 << FRAMEFORM)
						  | (0x3 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteEnabled();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x02) == 0x00)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }

  LPC_SPIFI->Address = address;

  LPC_SPIFI->Command = BLOCK_ERASE
						  | (0x4 << FRAMEFORM)
						  | (0x3 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteDone();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x80) == 0x80)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }
#endif

#if W25Q16V
  LPC_SPIFI->Command = WREN
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteEnabled();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x02) == 0x00)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }

  LPC_SPIFI->Address = address;

  LPC_SPIFI->Command = BLOCK_ERASE
						  | (0x4 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteDone();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x80) == 0x80)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }
#endif

  return;
}

/*****************************************************************************
** Function name:		SPIFIEraseChip
**
** Descriptions:		Chip Erase
**
** parameters:			None
** 						
** Returned value:		None
** 
*****************************************************************************/
void SPIFIEraseChip( void ) 
{
  uint8_t data[2];

#if SST26VF016
  LPC_SPIFI->Command = WREN
						  | (0x1 << FRAMEFORM)
						  | (0x3 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteEnabled();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x02) == 0x00)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }

  LPC_SPIFI->Command = CHIP_ERASE
						  | (0x1 << FRAMEFORM)
						  | (0x3 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteDone();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x80) == 0x80)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }
#endif

#if W25Q16V
  LPC_SPIFI->Command = WREN
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);
  
  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteEnabled();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x02) == 0x00)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }

  LPC_SPIFI->Command = CHIP_ERASE
						  | (0x1 << FRAMEFORM)
						  | (0x0 << PS)
						  | (0x0 << INTLEN)
						  | (0x0 << DOUT)
						  | (0x0 << POLLRS)
						  | (0x0 << DATALEN);

  while ((LPC_SPIFI->Status & 0x2) == 0x2);

  data[0] = SPIFIReadStatusWriteDone();
//  SPIFIReadStatus((uint8_t *)&data);
//  while ((data[0] & 0x80) == 0x80)
//  {
//    SPIFIReadStatus((uint8_t *)&data);
//  }
#endif

  return;
}



#endif

/******************************************************************************
**                            End Of File
******************************************************************************/
