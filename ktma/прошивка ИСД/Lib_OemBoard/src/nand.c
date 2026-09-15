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
#include "lpc177x_8x_timer.h"
#include "nand.h"


/******************************************************************************
 * Defines and typedefs
 *****************************************************************************/

#define K9F1G_CLE   ((volatile uint8_t *)0x90100000)
#define K9F1G_ALE   ((volatile uint8_t *)0x90080000)
#define K9F1G_DATA  ((volatile uint8_t *)0x90000000)

#define K9FXX_WAIT()

#define K9FXX_READ_1            0x00                
#define K9FXX_READ_2            0x30                

#define K9FXX_SET_ADDR_A        0x00                
#define K9FXX_SET_ADDR_B        0x01                
#define K9FXX_SET_ADDR_C        0x50                
#define K9FXX_READ_ID           0x90                
#define K9FXX_RESET             0xff                
#define K9FXX_BLOCK_PROGRAM_1   0x80                
#define K9FXX_BLOCK_PROGRAM_2   0x10                
#define K9FXX_BLOCK_ERASE_1     0x60                
#define K9FXX_BLOCK_ERASE_2     0xd0                
#define K9FXX_READ_STATUS       0x70                
#define K9FXX_BUSY              (1 << 6)            
#define K9FXX_OK                (1 << 0)   

#define ID_MARKER_CODE (0xEC)
#define ID_SAMSUNG     (0xF1)

#define ID_PAGE_SZ_1KB (0x00)
#define ID_PAGE_SZ_2KB (0x01)
#define ID_PAGE_SZ_4KB (0x02)
#define ID_PAGE_SZ_8KB (0x03)

#define ID_BLOCK_SZ_64KB  (0x00)
#define ID_BLOCK_SZ_128KB (0x01)
#define ID_BLOCK_SZ_256KB (0x02)
#define ID_BLOCK_SZ_512KB (0x03)

#define ID_PAGE_SZ_1KB (0x00)
#define ID_PAGE_SZ_2KB (0x01)
#define ID_PAGE_SZ_4KB (0x02)
#define ID_PAGE_SZ_8KB (0x03)

#define ID_REDUND_SZ_8  (0x00)
#define ID_REDUND_SZ_16 (0x01)



/* This macro could be changed to check the ready pin */
#define WAIT_READY() (TIM_Waitus(35))
         

/******************************************************************************
 * External global variables
 *****************************************************************************/

/******************************************************************************
 * Local variables
 *****************************************************************************/

static uint32_t pageSize   = 0;
static uint32_t blockSize  = 0;
static uint32_t reduntSize = 0;


/******************************************************************************
 * Local Functions
 *****************************************************************************/

static void pinConfig(void)
{
  LPC_IOCON->P3_0 |= 1; /* D0 @ P3.0 */
  LPC_IOCON->P3_1 |= 1; /* D1 @ P3.1 */
  LPC_IOCON->P3_2 |= 1; /* D2 @ P3.2 */
  LPC_IOCON->P3_3 |= 1; /* D3 @ P3.3 */

  LPC_IOCON->P3_4 |= 1; /* D4 @ P3.4 */
  LPC_IOCON->P3_5 |= 1; /* D5 @ P3.5 */
  LPC_IOCON->P3_6 |= 1; /* D6 @ P3.6 */
  LPC_IOCON->P3_7 |= 1; /* D7 @ P3.7 */

  LPC_IOCON->P3_8 |= 1; /* D8 @ P3.8 */
  LPC_IOCON->P3_9 |= 1; /* D9 @ P3.9 */
  LPC_IOCON->P3_10 |= 1; /* D10 @ P3.10 */
  LPC_IOCON->P3_11 |= 1; /* D11 @ P3.11 */

  LPC_IOCON->P3_12 |= 1; /* D12 @ P3.12 */
  LPC_IOCON->P3_13 |= 1; /* D13 @ P3.13 */
  LPC_IOCON->P3_14 |= 1; /* D14 @ P3.14 */
  LPC_IOCON->P3_15 |= 1; /* D15 @ P3.15 */

  LPC_IOCON->P3_16 |= 1; /* D16 @ P3.16 */
  LPC_IOCON->P3_17 |= 1; /* D17 @ P3.17 */
  LPC_IOCON->P3_18 |= 1; /* D18 @ P3.18 */
  LPC_IOCON->P3_19 |= 1; /* D19 @ P3.19 */

  LPC_IOCON->P3_20 |= 1; /* D20 @ P3.20 */
  LPC_IOCON->P3_21 |= 1; /* D21 @ P3.21 */
  LPC_IOCON->P3_22 |= 1; /* D22 @ P3.22 */
  LPC_IOCON->P3_23 |= 1; /* D23 @ P3.23 */

  LPC_IOCON->P3_24 |= 1; /* D24 @ P3.24 */
  LPC_IOCON->P3_25 |= 1; /* D25 @ P3.25 */
  LPC_IOCON->P3_26 |= 1; /* D26 @ P3.26 */
  LPC_IOCON->P3_27 |= 1; /* D27 @ P3.27 */

  LPC_IOCON->P3_28 |= 1; /* D28 @ P3.28 */
  LPC_IOCON->P3_29 |= 1; /* D29 @ P3.29 */
  LPC_IOCON->P3_30 |= 1; /* D30 @ P3.30 */
  LPC_IOCON->P3_31 |= 1; /* D31 @ P3.31 */

  LPC_IOCON->P4_0 |= 1; /* A0 @ P4.0 */
  LPC_IOCON->P4_1 |= 1; /* A1 @ P4.1 */
  LPC_IOCON->P4_2 |= 1; /* A2 @ P4.2 */
  LPC_IOCON->P4_3 |= 1; /* A3 @ P4.3 */

  LPC_IOCON->P4_4 |= 1; /* A4 @ P4.4 */
  LPC_IOCON->P4_5 |= 1; /* A5 @ P4.5 */
  LPC_IOCON->P4_6 |= 1; /* A6 @ P4.6 */
  LPC_IOCON->P4_7 |= 1; /* A7 @ P4.7 */

  LPC_IOCON->P4_8 |= 1; /* A8 @ P4.8 */
  LPC_IOCON->P4_9 |= 1; /* A9 @ P4.9 */
  LPC_IOCON->P4_10 |= 1; /* A10 @ P4.10 */
  LPC_IOCON->P4_11 |= 1; /* A11 @ P4.11 */

  LPC_IOCON->P4_12 |= 1; /* A12 @ P4.12 */
  LPC_IOCON->P4_13 |= 1; /* A13 @ P4.13 */
  LPC_IOCON->P4_14 |= 1; /* A14 @ P4.14 */
  LPC_IOCON->P4_15 |= 1; /* A15 @ P4.15 */

  LPC_IOCON->P4_16 |= 1; /* A16 @ P4.16 */
  LPC_IOCON->P4_17 |= 1; /* A17 @ P4.17 */
  LPC_IOCON->P4_18 |= 1; /* A18 @ P4.18 */
  LPC_IOCON->P4_19 |= 1; /* A19 @ P4.19 */

  LPC_IOCON->P4_20 |= 1; /* A20 @ P4.20 */
  LPC_IOCON->P4_21 |= 1; /* A21 @ P4.21 */
  LPC_IOCON->P4_22 |= 1; /* A22 @ P4.22 */
  LPC_IOCON->P4_23 |= 1; /* A23 @ P4.23 */

  LPC_IOCON->P4_24 |= 1; /* OEN @ P4.24 */
  LPC_IOCON->P4_25 |= 1; /* WEN @ P4.25 */
  LPC_IOCON->P4_26 |= 1; /* BLSN[0] @ P4.26 */
  LPC_IOCON->P4_27 |= 1; /* BLSN[1] @ P4.27 */

  LPC_IOCON->P4_28 |= 1; /* BLSN[2] @ P4.28 */
  LPC_IOCON->P4_29 |= 1; /* BLSN[3] @ P4.29 */
  LPC_IOCON->P4_30 |= 1; /* CSN[0] @ P4.30 */
  LPC_IOCON->P4_31 |= 1; /* CSN[1] @ P4.31 */

  LPC_IOCON->P2_14 |= 1; /* CSN[2] @ P2.14 */
  LPC_IOCON->P2_15 |= 1; /* CSN[3] @ P2.15 */
}


static uint32_t nandReadId(void)
{
  uint8_t a, b, c, d;
  volatile uint8_t *pCLE;
  volatile uint8_t *pALE;
  volatile uint8_t *pData;
  
  pCLE  = K9F1G_CLE;
  pALE  = K9F1G_ALE;
  pData = K9F1G_DATA;
    
  *pCLE = K9FXX_READ_ID;
  *pALE = 0;
    
  a = *pData;
  b = *pData;
  c = *pData;
  d = *pData;
  
    
  return (a << 24) | (b << 16) | (c << 8) | d;
}

static uint8_t nandStatus(void)
{
  uint8_t status = 0;
  volatile uint8_t *pCLE;
  volatile uint8_t *pALE;
  volatile uint8_t *pData;
  
  pCLE  = K9F1G_CLE;
  pALE  = K9F1G_ALE;
  pData = K9F1G_DATA;
    
  *pCLE = K9FXX_READ_STATUS;
  *pALE = 0;
    
  status = *pData;
      
  /* remove bits not used */
  return (status & 0xC1);  
}

static void nandWaitReady(void) 
{
  while( !(nandStatus() & (1<<6)) );
}

/******************************************************************************
 * Public Functions
 *****************************************************************************/



/******************************************************************************
 *
 * Description:
 *    Initialize the NAND Flash
 *
 * Returns:
 *    TRUE if initialization successful; otherwise FALSE
 *
 *****************************************************************************/
uint32_t nand_init (void)
{
  uint32_t nandId = 0;
  TIM_TIMERCFG_Type timerCfg;

  LPC_SC->PCONP     |= 0x00000800;
  LPC_EMC->Control   = 0x00000001;
  LPC_EMC->Config    = 0x00000000;

  pinConfig();

  TIM_ConfigStructInit(TIM_TIMER_MODE, &timerCfg);
  TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timerCfg);

  LPC_EMC->StaticConfig1   = 0x00000080;

  LPC_EMC->StaticWaitWen1  = 0x00000002; 
  LPC_EMC->StaticWaitOen1  = 0x00000002; 
  LPC_EMC->StaticWaitRd1   = 0x00000008; 
  LPC_EMC->StaticWaitPage1 = 0x0000001f; 
  LPC_EMC->StaticWaitWr1   = 0x00000008; 
  LPC_EMC->StaticWaitTurn1 = 0x0000000f;

  nandId = nandReadId();

  if ((nandId & 0xffff0000) != 
    (((uint32_t)(ID_MARKER_CODE) << 24) | ID_SAMSUNG << 16)) {
    /* unknown NAND chip */
    return FALSE;
  }

  pageSize   = 1024 * (1 << (nandId & 0x03));  
  blockSize  = 64*1024 * (1 << ((nandId>>4) & 0x03));
  reduntSize = 8 * (1 << ((nandId >> 1) & 0x1));

                         
  return TRUE;
}

/******************************************************************************
 *
 * Description:
 *    Get the page size of the NAND flash
 *
 * Returns:
 *    page size in bytes
 *
 *****************************************************************************/
uint32_t nand_getPageSize(void)
{
  return pageSize;
}

/******************************************************************************
 *
 * Description:
 *    Get the block size of the NAND flash
 *
 * Returns:
 *    block size in bytes
 *
 *****************************************************************************/
uint32_t nand_getBlockSize(void)
{
  return blockSize;
}

/******************************************************************************
 *
 * Description:
 *    Get the redundant (spare) size per page
 *
 * Returns:
 *    redundant/spare size in bytes
 *
 *****************************************************************************/
uint32_t nand_getRedundantSize(void)
{
  return reduntSize * (pageSize/512);
}

/******************************************************************************
 *
 * Description:
 *    Check if a block is valid
 *
 * Returns:
 *    TRUE if the block is valid; otherwise FALSE
 *
 *****************************************************************************/
uint32_t nand_isBlockValid(uint32_t block)
{
  uint32_t addr = 0;
  uint32_t page = 0;

  volatile uint8_t *pCLE;
  volatile uint8_t *pALE;
  volatile uint8_t *pData;

  
  pCLE  = K9F1G_CLE;
  pALE  = K9F1G_ALE;
  pData = K9F1G_DATA;

  if (block >= NAND_NUM_BLOCKS) {
    return FALSE;
  }

  addr = block * (blockSize/pageSize);

  /* 
   * Check page 0 and page 1 in each block. If the first byte
   * in the spare area (of either page 0 or page 1) is != 0xFF 
   * the block is invalid.
   */

  nandWaitReady();

  for (page = 0; page < 2; page++) {
    addr += page;

    *pCLE = K9FXX_READ_1;
    *pALE = (uint8_t) (pageSize & 0x00FF);
    *pALE = (uint8_t)((pageSize & 0xFF00) >> 8);
    *pALE = (uint8_t)((addr & 0x00FF));
    *pALE = (uint8_t)((addr & 0xFF00) >> 8);
    *pCLE = K9FXX_READ_2;

    WAIT_READY();

    if (*pData != 0xFF) {
      return FALSE;
    }
        
  }

  return TRUE;
}


/******************************************************************************
 *
 * Description:
 *    Read a page from the NAND memory
 *
 * Params:
 *    block - block number to read from
 *    page  - page withín block to read from
 *    pageBuf - data is copied to this buffer. The size must be at least 
 *              pageSize.
 *
 * Returns:
 *    TRUE if read successful; otherwise FALSE
 *
 *****************************************************************************/
uint32_t nand_readPage(uint32_t block, uint32_t page, uint8_t* pageBuf)
{
  uint32_t i = 0;
  uint32_t addr = 0;

  volatile uint8_t *pCLE;
  volatile uint8_t *pALE;
  volatile uint8_t *pData;

  
  pCLE  = K9F1G_CLE;
  pALE  = K9F1G_ALE;
  pData = K9F1G_DATA;

  if (block >= NAND_NUM_BLOCKS) {
    return FALSE;
  }

  if (page >= blockSize/pageSize) {
    return FALSE;
  }

  addr = block * (blockSize/pageSize) + page;

  /*
   * Always reading from start of a page address.
   * This means that the column address is always 0.
   */

  *pCLE = K9FXX_READ_1;
  *pALE = 0;
  *pALE = 0;
  *pALE = (uint8_t)((addr & 0x00FF));
  *pALE = (uint8_t)((addr & 0xFF00) >> 8);
  *pCLE = K9FXX_READ_2;

  WAIT_READY(); 
  

  for (i = 0; i < pageSize; i++) {
    *pageBuf++ = *pData;  
  }


  return TRUE;
}

/******************************************************************************
 *
 * Description:
 *    Write a page of data to the NAND memory
 *
 * Params:
 *    block - block number to write to
 *    page  - page within block to write to
 *    pageBuf - data is copied from this buffer. The size must be at least 
 *              pageSize.
 *
 * Returns:
 *    TRUE if write successful; otherwise FALSE
 *
 *****************************************************************************/
uint32_t nand_writePage(uint32_t block, uint32_t page, uint8_t* pageBuf)
{
  uint32_t i = 0;
  uint32_t addr = 0;

  volatile uint8_t *pCLE;
  volatile uint8_t *pALE;
  volatile uint8_t *pData;

  
  pCLE  = K9F1G_CLE;
  pALE  = K9F1G_ALE;
  pData = K9F1G_DATA;

  if (block >= NAND_NUM_BLOCKS) {
    return FALSE;
  }

  if (page >= blockSize/pageSize) {
    return FALSE;
  }

  addr = block * (blockSize/pageSize) + page;

  /*
   * Always writing to start of a page address.
   * This means that the column address is always 0.
   */ 

  *pCLE = K9FXX_BLOCK_PROGRAM_1;
  *pALE = 0; 
  *pALE = 0; 
  *pALE = (uint8_t)((addr & 0x00FF));
  *pALE = (uint8_t)((addr & 0xFF00) >> 8);


  for (i = 0; i < pageSize; i++) {
    *pData = *pageBuf++;
  }

  *pCLE = K9FXX_BLOCK_PROGRAM_2;

  TIM_Waitus(700);  
  nandWaitReady();
  
  return ((nandStatus() & 0x01) != 0x01); 
}

/******************************************************************************
 *
 * Description:
 *    Erase a block
 *
 * Params:
 *    block - block number to erase
 *
 * Returns:
 *    TRUE if eras successful; otherwise FALSE
 *
 *****************************************************************************/
uint32_t nand_eraseBlock(uint32_t block)
{
  uint32_t addr = 0;

  volatile uint8_t *pCLE;
  volatile uint8_t *pALE;

  pCLE  = K9F1G_CLE;
  pALE  = K9F1G_ALE;

  if (block >= NAND_NUM_BLOCKS) {
    return FALSE;
  }
    
  addr = block * (blockSize/pageSize);

  *pCLE = K9FXX_BLOCK_ERASE_1;
  *pALE = (uint8_t)(addr & 0x00FF);
  *pALE = (uint8_t)((addr & 0xFF00) >> 8);
  *pCLE = K9FXX_BLOCK_ERASE_2;

  TIM_Waitus(700);
  nandWaitReady();

  return ((nandStatus() & 0x01) != 0x01); 
}


