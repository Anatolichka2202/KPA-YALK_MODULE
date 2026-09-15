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
#ifndef __SDRAM_H
#define __SDRAM_H

/*
 * These timing parameters are based on the EMC clock
 * there is no way of ensuring what the EMC clock frequency is
 * without severely bloating the code
 * ENSURE THAT THE EMC clock is one of these values
 */
#define SDRAM_SPEED_48 0
#define SDRAM_SPEED_50 1
#define SDRAM_SPEED_60 2
#define SDRAM_SPEED_72 3

#define SDRAM_SPEED SDRAM_SPEED_60

#define SDRAM_CONFIG_32BIT
#define SDRAM_SIZE               0x2000000

#define SDRAM_BASE               0xA0000000 /*CS0*/

extern uint32_t sdram_init(void);

#endif /* end __SDRAM_H */
/****************************************************************************
**                            End Of File
*****************************************************************************/
