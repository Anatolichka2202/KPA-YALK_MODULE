/******************************************************************
 *****                                                        *****
 *****  Name: easyweb.c                                       *****
 *****  Ver.: 1.0                                             *****
 *****  Date: 07/05/2001                                      *****
 *****  Auth: Andreas Dannenberg                              *****
 *****        HTWK Leipzig                                    *****
 *****        university of applied sciences                  *****
 *****        Germany                                         *****
 *****        adannenb@et.htwk-leipzig.de                     *****
 *****  Func: implements a dynamic HTTP-server by using       *****
 *****        the easyWEB-API                                 *****
 *****  Rem.: In IAR-C, use  linker option                    *****
 *****        "-e_medium_write=_formatted_write"              *****
 *****                                                        *****
 ******************************************************************/

// Modifications by Code Red Technologies for NXP LPC1768

// CodeRed - removed header for MSP430 microcontroller
//#include "msp430x14x.h"

#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "iap.h"
#include "Serial.h"
#include "LPC177x_8x.h" 
#include "flash.h"

#define extern 
#define Timeout 1000000 
#define ADDR_DM1 0								 //Àíàëîãîâûå ïàðàìåòðû (êàíàëû 1-32)
#define ADDR_DM2 1								 //Àíàëîãîâûå ïàðàìåòðû (êàíàëû 33-64)
#define ADDR_DM3 2								 //Àíàëîãîâûå ïàðàìåòðû (êàíàëû 65-96)
#define ADDR_KM1 3								 //Êîììóòàòîð îáùåé øèíû (êàíàëû 1-32)
#define ADDR_KM2 4								 //Êîììóòàòîð îáùåé øèíû (êàíàëû 33-64)
#define ADDR_KM3 5								 //Êîììóòàòîð îáùåé øèíû (êàíàëû 65-96)
#define ADDR_KM4 6								 //Êîíòàêòíûå ïàðàìåòðû (êàíàëû 1-32)
#define ADDR_KM5 7				

// CodeRed - added #define extern on next line (else variables
// not defined). This has been done due to include the .h files 
// rather than the .c files as in the original version of easyweb.


#include "easyweb.h"

// CodeRed - removed header for original ethernet controller
//#include "cs8900.c"                              // ethernet packet driver

//CodeRed - added for LPC ethernet controller
#include "ethmac.h"

// CodeRed - include .h rather than .c file
// #include "tcpip.c"                               // easyWEB TCP/IP stack
#include "tcpip.h"                               // easyWEB TCP/IP stack




// CodeRed - include renamed .h rather than .c file
// #include "webside.c"                             // webside for our HTTP server (HTML)
#include "webside.h"                             // webside for our HTTP server (HTML)

unsigned char DMTable[96][2]=					//Òàáëèöà ñîîòâåòñòâèÿ íîìåðàì êàíàëîâ íà âûõîäå ÈÑÄ îòïðàâëÿåìûì àäðåñó DM è íîìåðó êàíàëà DM (1 èíäåêñ-íîìåð àíàëîãîâîãî êàíàëà ÈÑÄ; 2 èíäåêñ: 0-àäðåñ DM,1-íîìåð êàíàëà íà DM)
{
	ADDR_DM1,24,								//1 êàíàë ÈÑÄ				   
	ADDR_DM3,32,								//2 êàíàë ÈÑÄ				   
	ADDR_DM1,23,								//3 êàíàë ÈÑÄ				   
	ADDR_DM3,31,								//4 êàíàë ÈÑÄ				   
	ADDR_DM1,22,								//5 êàíàë ÈÑÄ				   
	ADDR_DM3,30,								//6 êàíàë ÈÑÄ				   
	ADDR_DM1,21,								//7 êàíàë ÈÑÄ				   
	ADDR_DM3,29,								//8 êàíàë ÈÑÄ				   
	ADDR_DM1,20,								//9 êàíàë ÈÑÄ				   
	ADDR_DM3,28,								//10 êàíàë ÈÑÄ				   
	ADDR_DM3,27,								//11 êàíàë ÈÑÄ				   
	ADDR_DM1,19,								//12 êàíàë ÈÑÄ				   
	ADDR_DM1,18,								//13 êàíàë ÈÑÄ				   
	ADDR_DM3,26,								//14 êàíàë ÈÑÄ				   
	ADDR_DM1,17,								//15 êàíàë ÈÑÄ				   
	ADDR_DM1,16,								//16 êàíàë ÈÑÄ				   
	ADDR_DM3,24,								//17 êàíàë ÈÑÄ				   
	ADDR_DM3,25,								//18 êàíàë ÈÑÄ				   
	ADDR_DM1,15,								//19 êàíàë ÈÑÄ				   
	ADDR_DM3,23,								//20 êàíàë ÈÑÄ				   
	ADDR_DM3,22,								//21 êàíàë ÈÑÄ					
	ADDR_DM1,13,								//22 êàíàë ÈÑÄ				   
	ADDR_DM3,21,								//23 êàíàë ÈÑÄ				   
	ADDR_DM1,14,								//24 êàíàë ÈÑÄ				   										
	ADDR_DM2,4,									//25 êàíàë ÈÑÄ				   
	ADDR_DM1,12,								//26 êàíàë ÈÑÄ				   
	ADDR_DM3,20,								//27 êàíàë ÈÑÄ				   
	ADDR_DM1,11,								//28 êàíàë ÈÑÄ				   
	ADDR_DM3,19,								//29 êàíàë ÈÑÄ				   
	ADDR_DM1,10,								//30 êàíàë ÈÑÄ				   
	ADDR_DM3,18,								//31 êàíàë ÈÑÄ				   
	ADDR_DM1,9,									//32 êàíàë ÈÑÄ				   
	ADDR_DM3,17,								//33 êàíàë ÈÑÄ				   
	ADDR_DM3,16,								//34 êàíàë ÈÑÄ				   
	ADDR_DM3,15,								//35 êàíàë ÈÑÄ				   
	ADDR_DM3,13,								//36 êàíàë ÈÑÄ				   
	ADDR_DM3,14,								//37 êàíàë ÈÑÄ				   
	ADDR_DM3,12,								//38 êàíàë ÈÑÄ				   
	ADDR_DM3,11,								//39 êàíàë ÈÑÄ				   
	ADDR_DM3,10,								//40 êàíàë ÈÑÄ				   
	ADDR_DM3,8,									//41 êàíàë ÈÑÄ				   
	ADDR_DM3,7,									//42 êàíàë ÈÑÄ				   
	ADDR_DM3,9,									//43 êàíàë ÈÑÄ				   
	ADDR_DM3,6,									//44 êàíàë ÈÑÄ				   
	ADDR_DM3,5,									//45 êàíàë ÈÑÄ				   
	ADDR_DM3,3,									//46 êàíàë ÈÑÄ				   
	ADDR_DM3,2,									//47 êàíàë ÈÑÄ				   
	ADDR_DM3,1,									//48 êàíàë ÈÑÄ				   
	ADDR_DM3,4,									//49 êàíàë ÈÑÄ				   
	ADDR_DM2,1,									//50 êàíàë ÈÑÄ				   
	ADDR_DM1,32,								//51 êàíàë ÈÑÄ				   
	ADDR_DM2,32,								//52 êàíàë ÈÑÄ				   
	ADDR_DM1,31,								//53 êàíàë ÈÑÄ				   
	ADDR_DM2,31,								//54 êàíàë ÈÑÄ				   
	ADDR_DM1,30,								//55 êàíàë ÈÑÄ				   
	ADDR_DM2,30,								//56 êàíàë ÈÑÄ				   
	ADDR_DM1,29,								//57 êàíàë ÈÑÄ				   
	ADDR_DM2,29,								//58 êàíàë ÈÑÄ				   
	ADDR_DM1,28,								//59 êàíàë ÈÑÄ				   
	ADDR_DM2,28,								//60 êàíàë ÈÑÄ				   
	ADDR_DM2,27,								//61 êàíàë ÈÑÄ				   
	ADDR_DM1,27,								//62 êàíàë ÈÑÄ				   
	ADDR_DM1,26,								//63 êàíàë ÈÑÄ				   
	ADDR_DM2,26,								//64 êàíàë ÈÑÄ				   
	ADDR_DM1,25,								//65 êàíàë ÈÑÄ				   
	ADDR_DM1,8,									//66 êàíàë ÈÑÄ				   
	ADDR_DM2,24,								//67 êàíàë ÈÑÄ				   
	ADDR_DM2,25,								//68 êàíàë ÈÑÄ				   
	ADDR_DM1,7,									//69 êàíàë ÈÑÄ					
	ADDR_DM2,23,								//70 êàíàë ÈÑÄ					
	ADDR_DM2,22,								//71 êàíàë ÈÑÄ					
	ADDR_DM1,6,									//72 êàíàë ÈÑÄ					
	ADDR_DM2,21,								//73 êàíàë ÈÑÄ					
	ADDR_DM1,5,									//74 êàíàë ÈÑÄ					
	ADDR_DM2,2,									//75 êàíàë ÈÑÄ					
	ADDR_DM1,4,									//76 êàíàë ÈÑÄ					
	ADDR_DM2,20,								//77 êàíàë ÈÑÄ					
	ADDR_DM1,3,									//78 êàíàë ÈÑÄ					
	ADDR_DM2,19,								//79 êàíàë ÈÑÄ					
	ADDR_DM1,2,									//80 êàíàë ÈÑÄ					
	ADDR_DM2,18,								//81 êàíàë ÈÑÄ					
	ADDR_DM1,1,								    //82 êàíàë ÈÑÄ					
	ADDR_DM2,17,								//83 êàíàë ÈÑÄ					
	ADDR_DM2,16,								//84 êàíàë ÈÑÄ					
	ADDR_DM2,15,								//85 êàíàë ÈÑÄ					
	ADDR_DM2,13,								//86 êàíàë ÈÑÄ					
	ADDR_DM2,14,								//87 êàíàë ÈÑÄ					
	ADDR_DM2,12,								//88 êàíàë ÈÑÄ					
	ADDR_DM2,11,								//89 êàíàë ÈÑÄ					
	ADDR_DM2,10,								//90 êàíàë ÈÑÄ					
	ADDR_DM2,8,									//91 êàíàë ÈÑÄ					
	ADDR_DM2,9,									//92 êàíàë ÈÑÄ					
	ADDR_DM2,7,									//93 êàíàë ÈÑÄ					
	ADDR_DM2,6,									//94 êàíàë ÈÑÄ					
	ADDR_DM2,5,									//95 êàíàë ÈÑÄ					
	ADDR_DM2,3,									//96 êàíàë ÈÑÄ					
};

unsigned char KMTable[64][2]=					//Òàáëèöà ñîîòâåòñòâèÿ íîìåðàì êàíàëîâ íà âûõîäå ÈÑÄ îòïðàâëÿåìûì àäðåñó ÊÌ è íîìåðó êàíàëà ÊM (1 èíäåêñ-íîìåð êîíòàêòíîãî êàíàëà ÈÑÄ; 2 èíäåêñ: 0-àäðåñ ÊÌ,1-íîìåð êàíàëà íà ÊÌ)
{
	ADDR_KM5,32,								//1 êàíàë ÈÑÄ
	ADDR_KM5,31,								//2 êàíàë ÈÑÄ
	ADDR_KM5,30,								//3 êàíàë ÈÑÄ
	ADDR_KM5,29,								//4 êàíàë ÈÑÄ
	ADDR_KM5,28,								//5 êàíàë ÈÑÄ
	ADDR_KM5,27,								//6 êàíàë ÈÑÄ
	ADDR_KM5,26,								//7 êàíàë ÈÑÄ
	ADDR_KM5,25,								//8 êàíàë ÈÑÄ
	ADDR_KM5,24,								//9 êàíàë ÈÑÄ
	ADDR_KM5,23,								//10 êàíàë ÈÑÄ
	ADDR_KM5,22,								//11 êàíàë ÈÑÄ
	ADDR_KM5,21,								//12 êàíàë ÈÑÄ
	ADDR_KM5,20,								//13 êàíàë ÈÑÄ
	ADDR_KM5,19,								//14 êàíàë ÈÑÄ
	ADDR_KM5,18,								//15 êàíàë ÈÑÄ
	ADDR_KM5,17,								//16 êàíàë ÈÑÄ
	ADDR_KM5,16,								//17 êàíàë ÈÑÄ
	ADDR_KM5,15,								//18 êàíàë ÈÑÄ
	ADDR_KM5,14,								//19 êàíàë ÈÑÄ
	ADDR_KM5,13,								//20 êàíàë ÈÑÄ
	ADDR_KM5,12,								//21 êàíàë ÈÑÄ
	ADDR_KM5,11,								//22 êàíàë ÈÑÄ
	ADDR_KM5,10,								//23 êàíàë ÈÑÄ
	ADDR_KM5,9,									//24 êàíàë ÈÑÄ
	ADDR_KM5,8,									//25 êàíàë ÈÑÄ
	ADDR_KM5,7,									//26 êàíàë ÈÑÄ
	ADDR_KM5,6,									//27 êàíàë ÈÑÄ
	ADDR_KM5,5,									//28 êàíàë ÈÑÄ
	ADDR_KM5,4,									//29 êàíàë ÈÑÄ
	ADDR_KM5,3,									//30 êàíàë ÈÑÄ
	ADDR_KM5,2,									//31 êàíàë ÈÑÄ
	ADDR_KM5,1,									//32 êàíàë ÈÑÄ
	ADDR_KM4,32,								//33 êàíàë ÈÑÄ
	ADDR_KM4,31,								//34 êàíàë ÈÑÄ
	ADDR_KM4,30,								//35 êàíàë ÈÑÄ
	ADDR_KM4,29,								//36 êàíàë ÈÑÄ
	ADDR_KM4,28,								//37 êàíàë ÈÑÄ
	ADDR_KM4,27,								//38 êàíàë ÈÑÄ
	ADDR_KM4,26,								//39 êàíàë ÈÑÄ
	ADDR_KM4,25,								//40 êàíàë ÈÑÄ
	ADDR_KM4,24,								//41 êàíàë ÈÑÄ
	ADDR_KM4,23,								//42 êàíàë ÈÑÄ
	ADDR_KM4,22,								//43 êàíàë ÈÑÄ
	ADDR_KM4,21,								//44 êàíàë ÈÑÄ
	ADDR_KM4,20,								//45 êàíàë ÈÑÄ
	ADDR_KM4,19,								//46 êàíàë ÈÑÄ
	ADDR_KM4,18,								//47 êàíàë ÈÑÄ
	ADDR_KM4,17,								//48 êàíàë ÈÑÄ
	ADDR_KM4,16,								//49 êàíàë ÈÑÄ
	ADDR_KM4,15,								//50 êàíàë ÈÑÄ
	ADDR_KM4,14,								//51 êàíàë ÈÑÄ
	ADDR_KM4,13,								//52 êàíàë ÈÑÄ
	ADDR_KM4,12,								//53 êàíàë ÈÑÄ
	ADDR_KM4,11,								//54 êàíàë ÈÑÄ
	ADDR_KM4,10,								//55 êàíàë ÈÑÄ
	ADDR_KM4,9,									//56 êàíàë ÈÑÄ
	ADDR_KM4,8,									//57 êàíàë ÈÑÄ
	ADDR_KM4,7,									//58 êàíàë ÈÑÄ
	ADDR_KM4,6,									//59 êàíàë ÈÑÄ
	ADDR_KM4,5,									//60 êàíàë ÈÑÄ
	ADDR_KM4,4,									//61 êàíàë ÈÑÄ
	ADDR_KM4,3,									//62 êàíàë ÈÑÄ
	ADDR_KM4,2,									//63 êàíàë ÈÑÄ
	ADDR_KM4,1									//64 êàíàë ÈÑÄ
};

unsigned char KMGlobalBusTable[3][32][2]=		//Òàáëèöà ñîîòâåòñòâèÿ àíàëîãîâîìó êàíàëó êàíàëà êîììóòàòîðà îáùåé øèíû
{
	ADDR_KM2,9,									//1 êàíàë DM1							
	ADDR_KM2,10,								//2 êàíàë DM1							
	ADDR_KM2,11,								//3 êàíàë DM1							
	ADDR_KM2,12,								//4 êàíàë DM1							
	ADDR_KM2,13,								//5 êàíàë DM1							
	ADDR_KM2,14,								//6 êàíàë DM1							
	ADDR_KM2,15,								//7 êàíàë DM1							
	ADDR_KM2,16,								//8 êàíàë DM1							
	ADDR_KM3,17,								//9 êàíàë DM1							
	ADDR_KM3,18,								//10 êàíàë DM1							
	ADDR_KM3,19,								//11 êàíàë DM1							
	ADDR_KM3,20,								//12 êàíàë DM1							
	ADDR_KM3,21,								//13 êàíàë DM1							
	ADDR_KM3,22,								//14 êàíàë DM1							
	ADDR_KM3,23,								//15 êàíàë DM1							
	ADDR_KM3,24,								//16 êàíàë DM1							
	ADDR_KM3,25,								//17 êàíàë DM1							
	ADDR_KM3,26,								//18 êàíàë DM1							
	ADDR_KM3,27,								//19 êàíàë DM1							
	ADDR_KM3,28,								//20 êàíàë DM1							
	ADDR_KM3,29,								//21 êàíàë DM1							
	ADDR_KM3,30,								//22 êàíàë DM1							
	ADDR_KM3,31,								//23 êàíàë DM1							
	ADDR_KM3,32,								//24 êàíàë DM1							
	ADDR_KM1,18,								//25 êàíàë DM1							
	ADDR_KM1,20,								//26 êàíàë DM1							
	ADDR_KM1,22,								//27 êàíàë DM1							
	ADDR_KM1,24,								//28 êàíàë DM1							
	ADDR_KM1,26,								//29 êàíàë DM1							
	ADDR_KM1,28,								//30 êàíàë DM1							
	ADDR_KM1,30,								//31 êàíàë DM1							
	ADDR_KM1,32,								//32 êàíàë DM1							
																						
	ADDR_KM1,1,									//1 êàíàë DM2							
	ADDR_KM2,1,									//2 êàíàë DM2							
	ADDR_KM1,2,									//3 êàíàë DM2							
	ADDR_KM2,2,									//4 êàíàë DM2							
	ADDR_KM1,3,									//5 êàíàë DM2							
	ADDR_KM2,3,									//6 êàíàë DM2							
	ADDR_KM1,4,									//7 êàíàë DM2							
	ADDR_KM2,4,									//8 êàíàë DM2							
	ADDR_KM1,5,									//9 êàíàë DM2							
	ADDR_KM2,5,									//10 êàíàë DM2							
	ADDR_KM1,6,									//11 êàíàë DM2							
	ADDR_KM2,6,									//12 êàíàë DM2							
	ADDR_KM1,7,									//13 êàíàë DM2							
	ADDR_KM2,7,									//14 êàíàë DM2							
	ADDR_KM1,8,									//15 êàíàë DM2							
	ADDR_KM2,8,									//16 êàíàë DM2							
	ADDR_KM1,9,									//17 êàíàë DM2							
	ADDR_KM1,10,								//18 êàíàë DM2							
	ADDR_KM1,11,								//19 êàíàë DM2							
	ADDR_KM1,12,								//20 êàíàë DM2							
	ADDR_KM1,13,								//21 êàíàë DM2							
	ADDR_KM1,14,								//22 êàíàë DM2							
	ADDR_KM1,15,								//23 êàíàë DM2							
	ADDR_KM1,16,								//24 êàíàë DM2							
	ADDR_KM1,17,								//25 êàíàë DM2							
	ADDR_KM1,19,								//26 êàíàë DM2							
	ADDR_KM1,21,								//27 êàíàë DM2							
	ADDR_KM1,23,								//28 êàíàë DM2							
	ADDR_KM1,25,								//29 êàíàë DM2							
	ADDR_KM1,27,								//30 êàíàë DM2							
	ADDR_KM1,29,								//31 êàíàë DM2							
	ADDR_KM1,31,								//32 êàíàë DM2							
																						
	ADDR_KM3,1,									//1 êàíàë DM3							
	ADDR_KM3,2,									//2 êàíàë DM3							
	ADDR_KM3,3,									//3 êàíàë DM3							
	ADDR_KM3,4,									//4 êàíàë DM3							
	ADDR_KM3,5,									//5 êàíàë DM3							
	ADDR_KM3,6,									//6 êàíàë DM3							
	ADDR_KM3,7,									//7 êàíàë DM3							
	ADDR_KM3,8,									//8 êàíàë DM3							
	ADDR_KM3,9,									//9 êàíàë DM3							
	ADDR_KM3,10,								//10 êàíàë DM3							
	ADDR_KM3,11,								//11 êàíàë DM3							
	ADDR_KM3,12,								//12 êàíàë DM3							
	ADDR_KM3,13,								//13 êàíàë DM3							
	ADDR_KM3,14,								//14 êàíàë DM3							
	ADDR_KM3,15,								//15 êàíàë DM3							
	ADDR_KM3,16,								//16 êàíàë DM3							
	ADDR_KM2,17,								//17 êàíàë DM3							
	ADDR_KM2,18,								//18 êàíàë DM3							
	ADDR_KM2,19,								//19 êàíàë DM3							
	ADDR_KM2,20,								//20 êàíàë DM3							
	ADDR_KM2,21,								//21 êàíàë DM3							
	ADDR_KM2,22,								//22 êàíàë DM3							
	ADDR_KM2,23,								//23 êàíàë DM3							
	ADDR_KM2,24,								//24 êàíàë DM3							
	ADDR_KM2,25,								//25 êàíàë DM3							
	ADDR_KM2,26,								//26 êàíàë DM3							
	ADDR_KM2,27,								//27 êàíàë DM3							
	ADDR_KM2,28,								//28 êàíàë DM3							
	ADDR_KM2,29,								//29 êàíàë DM3							
	ADDR_KM2,30,								//30 êàíàë DM3							
	ADDR_KM2,31,								//31 êàíàë DM3							
	ADDR_KM2,32									//32 êàíàë DM3							
};

unsigned char AnalogResetQuery[96][8]=
{
0,64,0,0,0,0,1,212,
0,65,0,0,0,0,60,20,
0,66,0,0,0,0,120,20,
0,67,0,0,0,0,69,212,
0,68,0,0,0,0,240,20,
0,69,0,0,0,0,205,212,
0,70,0,0,0,0,137,212,
0,71,0,0,0,0,180,20,
0,72,0,0,0,0,224,21,
0,73,0,0,0,0,221,213,
0,74,0,0,0,0,153,213,
0,75,0,0,0,0,164,21,
0,76,0,0,0,0,17,213,
0,77,0,0,0,0,44,21,
0,78,0,0,0,0,104,21,
0,79,0,0,0,0,85,213,
64,64,0,0,0,0,15,20,
64,65,0,0,0,0,50,212,
64,66,0,0,0,0,118,212,
64,67,0,0,0,0,75,20,
64,68,0,0,0,0,254,212,
64,69,0,0,0,0,195,20,
64,70,0,0,0,0,135,20,
64,71,0,0,0,0,186,212,
64,72,0,0,0,0,238,213,
64,73,0,0,0,0,211,21,
64,74,0,0,0,0,151,21,
64,75,0,0,0,0,170,213,
64,76,0,0,0,0,31,21,
64,77,0,0,0,0,34,213,
64,78,0,0,0,0,102,213,
64,79,0,0,0,0,91,21,

1,64,0,0,0,0,0,5,
1,65,0,0,0,0,61,197,
1,66,0,0,0,0,121,197,
1,67,0,0,0,0,68,5,
1,68,0,0,0,0,241,197,
1,69,0,0,0,0,204,5,
1,70,0,0,0,0,136,5,
1,71,0,0,0,0,181,197,
1,72,0,0,0,0,225,196,
1,73,0,0,0,0,220,4,
1,74,0,0,0,0,152,4,
1,75,0,0,0,0,165,196,
1,76,0,0,0,0,16,4,
1,77,0,0,0,0,45,196,
1,78,0,0,0,0,105,196,
1,79,0,0,0,0,84,4,
65,64,0,0,0,0,14,197,
65,65,0,0,0,0,51,5,
65,66,0,0,0,0,119,5,
65,67,0,0,0,0,74,197,
65,68,0,0,0,0,255,5,
65,69,0,0,0,0,194,197,
65,70,0,0,0,0,134,197,
65,71,0,0,0,0,187,5,
65,72,0,0,0,0,239,4,
65,73,0,0,0,0,210,196,
65,74,0,0,0,0,150,196,
65,75,0,0,0,0,171,4,
65,76,0,0,0,0,30,196,
65,77,0,0,0,0,35,4,
65,78,0,0,0,0,103,4,
65,79,0,0,0,0,90,196,

2,64,0,0,0,0,0,54,
2,65,0,0,0,0,61,246,
2,66,0,0,0,0,121,246,
2,67,0,0,0,0,68,54,
2,68,0,0,0,0,241,246,
2,69,0,0,0,0,204,54,
2,70,0,0,0,0,136,54,
2,71,0,0,0,0,181,246,
2,72,0,0,0,0,225,247,
2,73,0,0,0,0,220,55,
2,74,0,0,0,0,152,55,
2,75,0,0,0,0,165,247,
2,76,0,0,0,0,16,55,
2,77,0,0,0,0,45,247,
2,78,0,0,0,0,105,247,
2,79,0,0,0,0,84,55,
66,64,0,0,0,0,14,246,
66,65,0,0,0,0,51,54,
66,66,0,0,0,0,119,54,
66,67,0,0,0,0,74,246,
66,68,0,0,0,0,255,54,
66,69,0,0,0,0,194,246,
66,70,0,0,0,0,134,246,
66,71,0,0,0,0,187,54,
66,72,0,0,0,0,239,55,
66,73,0,0,0,0,210,247,
66,74,0,0,0,0,150,247,
66,75,0,0,0,0,171,55,
66,76,0,0,0,0,30,247,
66,77,0,0,0,0,35,55,
66,78,0,0,0,0,103,55,
66,79,0,0,0,0,90,247
};

unsigned char KMResetQuery[64][8]=
{
6,0,0,0,0,0,0,125,
6,1,0,0,0,0,61,189,
6,2,0,0,0,0,121,189,
6,3,0,0,0,0,68,125,
6,4,0,0,0,0,241,189,
6,5,0,0,0,0,204,125,
6,6,0,0,0,0,136,125,
6,7,0,0,0,0,181,189,
6,8,0,0,0,0,225,188,
6,9,0,0,0,0,220,124,
6,10,0,0,0,0,152,124,
6,11,0,0,0,0,165,188,
6,12,0,0,0,0,16,124,
6,13,0,0,0,0,45,188,
6,14,0,0,0,0,105,188,
6,15,0,0,0,0,84,124,
70,0,0,0,0,0,14,189,
70,1,0,0,0,0,51,125,
70,2,0,0,0,0,119,125,
70,3,0,0,0,0,74,189,
70,4,0,0,0,0,255,125,
70,5,0,0,0,0,194,189,
70,6,0,0,0,0,134,189,
70,7,0,0,0,0,187,125,
70,8,0,0,0,0,239,124,
70,9,0,0,0,0,210,188,
70,10,0,0,0,0,150,188,
70,11,0,0,0,0,171,124,
70,12,0,0,0,0,30,188,
70,13,0,0,0,0,35,124,
70,14,0,0,0,0,103,124,
70,15,0,0,0,0,90,188,
7,0,0,0,0,0,1,172,
7,1,0,0,0,0,60,108,
7,2,0,0,0,0,120,108,
7,3,0,0,0,0,69,172,
7,4,0,0,0,0,240,108,
7,5,0,0,0,0,205,172,
7,6,0,0,0,0,137,172,
7,7,0,0,0,0,180,108,
7,8,0,0,0,0,224,109,
7,9,0,0,0,0,221,173,
7,10,0,0,0,0,153,173,
7,11,0,0,0,0,164,109,
7,12,0,0,0,0,17,173,
7,13,0,0,0,0,44,109,
7,14,0,0,0,0,104,109,
7,15,0,0,0,0,85,173,
71,0,0,0,0,0,15,108,
71,1,0,0,0,0,50,172,
71,2,0,0,0,0,118,172,
71,3,0,0,0,0,75,108,
71,4,0,0,0,0,254,172,
71,5,0,0,0,0,195,108,
71,6,0,0,0,0,135,108,
71,7,0,0,0,0,186,172,
71,8,0,0,0,0,238,173,
71,9,0,0,0,0,211,109,
71,10,0,0,0,0,151,109,
71,11,0,0,0,0,170,173,
71,12,0,0,0,0,31,109,
71,13,0,0,0,0,34,173,
71,14,0,0,0,0,102,173,
71,15,0,0,0,0,91,109
};

unsigned char KMGlobalBusResetQuery[96][8]=
{
3,0,0,0,0,0,0,40,
3,1,0,0,0,0,61,232,
3,2,0,0,0,0,121,232,
3,3,0,0,0,0,68,40,
3,4,0,0,0,0,241,232,
3,5,0,0,0,0,204,40,
3,6,0,0,0,0,136,40,
3,7,0,0,0,0,181,232,
3,8,0,0,0,0,225,233,
3,9,0,0,0,0,220,41,
3,10,0,0,0,0,152,41,
3,11,0,0,0,0,165,233,
3,12,0,0,0,0,16,41,
3,13,0,0,0,0,45,233,
3,14,0,0,0,0,105,233,
3,15,0,0,0,0,84,41,
67,0,0,0,0,0,14,232,
67,1,0,0,0,0,51,40,
67,2,0,0,0,0,119,40,
67,3,0,0,0,0,74,232,
67,4,0,0,0,0,255,40,
67,5,0,0,0,0,194,232,
67,6,0,0,0,0,134,232,
67,7,0,0,0,0,187,40,
67,8,0,0,0,0,239,41,
67,9,0,0,0,0,210,233,
67,10,0,0,0,0,150,233,
67,11,0,0,0,0,171,41,
67,12,0,0,0,0,30,233,
67,13,0,0,0,0,35,41,
67,14,0,0,0,0,103,41,
67,15,0,0,0,0,90,233,
4,0,0,0,0,0,1,159,
4,1,0,0,0,0,60,95,
4,2,0,0,0,0,120,95,
4,3,0,0,0,0,69,159,
4,4,0,0,0,0,240,95,
4,5,0,0,0,0,205,159,
4,6,0,0,0,0,137,159,
4,7,0,0,0,0,180,95,
4,8,0,0,0,0,224,94,
4,9,0,0,0,0,221,158,
4,10,0,0,0,0,153,158,
4,11,0,0,0,0,164,94,
4,12,0,0,0,0,17,158,
4,13,0,0,0,0,44,94,
4,14,0,0,0,0,104,94,
4,15,0,0,0,0,85,158,
68,0,0,0,0,0,15,95,
68,1,0,0,0,0,50,159,
68,2,0,0,0,0,118,159,
68,3,0,0,0,0,75,95,
68,4,0,0,0,0,254,159,
68,5,0,0,0,0,195,95,
68,6,0,0,0,0,135,95,
68,7,0,0,0,0,186,159,
68,8,0,0,0,0,238,158,
68,9,0,0,0,0,211,94,
68,10,0,0,0,0,151,94,
68,11,0,0,0,0,170,158,
68,12,0,0,0,0,31,94,
68,13,0,0,0,0,34,158,
68,14,0,0,0,0,102,158,
68,15,0,0,0,0,91,94,
5,0,0,0,0,0,0,78,
5,1,0,0,0,0,61,142,
5,2,0,0,0,0,121,142,
5,3,0,0,0,0,68,78,
5,4,0,0,0,0,241,142,
5,5,0,0,0,0,204,78,
5,6,0,0,0,0,136,78,
5,7,0,0,0,0,181,142,
5,8,0,0,0,0,225,143,
5,9,0,0,0,0,220,79,
5,10,0,0,0,0,152,79,
5,11,0,0,0,0,165,143,
5,12,0,0,0,0,16,79,
5,13,0,0,0,0,45,143,
5,14,0,0,0,0,105,143,
5,15,0,0,0,0,84,79,
69,0,0,0,0,0,14,142,
69,1,0,0,0,0,51,78,
69,2,0,0,0,0,119,78,
69,3,0,0,0,0,74,142,
69,4,0,0,0,0,255,78,
69,5,0,0,0,0,194,142,
69,6,0,0,0,0,134,142,
69,7,0,0,0,0,187,78,
69,8,0,0,0,0,239,79,
69,9,0,0,0,0,210,143,
69,10,0,0,0,0,150,143,
69,11,0,0,0,0,171,79,
69,12,0,0,0,0,30,143,
69,13,0,0,0,0,35,79,
69,14,0,0,0,0,103,79,
69,15,0,0,0,0,90,143
};



// CodeRed - added for use in dynamic side of web page
unsigned int aaPagecounter=0;
unsigned int adcValue = 0;

unsigned char SendCommandFlag=0,QUERY_COPY[3];
unsigned char buff_[8],UART1_count=0;
unsigned char Str[5000];	
unsigned short CalibrCRC;


int ReadParameter(unsigned char *str)
{
	unsigned char *str2;
	if (str2=strstr(TCP_RX_BUF,str)) 
	{
		str2+=strlen(str);
		return atoi(str2);	 			
	}
	else return -1;
}

struct AdapterSettings AdapterS;		 
struct Struct_State State;
struct Struct_Calibr Calibr;

int calcCRC(unsigned char* data,unsigned char length)
{
	unsigned int CRC1 = 0x0FFFF,i = 0, j = 0;
	char fl = 0;
	while (i<length)
	{
	CRC1 = CRC1 ^ data[i]; 
	j = 0;
	fl=0;
	while (j<8)
		{
		if ((CRC1&1)==1) fl = 1; else fl = 0; 
		CRC1 = CRC1 /2 ;
		if (fl == 1) CRC1 = CRC1 ^ 0x0A001;
		j++;
		}
	i++;
	}
	return CRC1;
} 

void UART1_Init()
{
/*	U1_LCR  |= 0x80;			//Ðàçðåøàåì äîñòóï ê ðåãèñòðó äåëèòåëÿ
	U1DLM = 0;
	U1DLL = 39;			    //Óñòàíàâëèâàåì áèòðåéò 115200
	U1LCR  = 0x03;		    //8 áèò
	U1FCR = 0x7;		
	U1IER = 0;			*/
}

void UART1_Send(const unsigned char* buffer,int bufSize)
{		
	int i=0;
//	U1FCR|=3;											  //Î÷èùàåì ïðèåìíûé áóôôåð ïåðåä îòïðàâêîé äëÿ ïðèåìà ïîñëåäóþùåãî îòâåòà
	while (i<bufSize)
	{
	SER_PutChar(buffer[i]);
		i++;
	}
}

void UART1_Read(unsigned char* buffer,int bufsize)
{
	int i;
	for (i=0;i<bufsize;i++) buffer[i]=SER_GetChar();
}



float ReadFloatParameter(unsigned char *str)
{
	unsigned char *str2;
	if (str2=strstr(TCP_RX_BUF,str)) 
	{
		str2+=strlen(str);
		return atof(str2);	 			
	}
	else return -65535.0;
}

int TryReceiveData()
{
/*
	char X;
	X = SER_GetChar();
	
	if (X!=-1)
	{	
		buff_[UART1_count]=X;
		UART1_count++;	
    SER_PutChar(X);		
	}
	if(UART1_count==8) 
	{
		UART1_count=0; 
		return 1;
	}
	else return 0;
	*/

	if (LPC_UART2->LSR & 0x01)
	{	
		buff_[UART1_count]=LPC_UART2->RBR;
		UART1_count++;	   
	}
	if(UART1_count==8) 
	{
		UART1_count=0; 
		return 1;
	}
	else return 0;
}

unsigned char AnalogReset()
{
	long j;
	int i,k;

	for (k=0;k<96;k++)
	{
		LPC_UART2->FCR|=3;
		LPC_UART2->THR=AnalogResetQuery[k][0];
		LPC_UART2->THR=AnalogResetQuery[k][1];
		LPC_UART2->THR=AnalogResetQuery[k][2];
		LPC_UART2->THR=AnalogResetQuery[k][3];
		LPC_UART2->THR=AnalogResetQuery[k][4];
		LPC_UART2->THR=AnalogResetQuery[k][5];
		LPC_UART2->THR=AnalogResetQuery[k][6];
		LPC_UART2->THR=AnalogResetQuery[k][7];

		for (j=0;j<Timeout;j++) if (TryReceiveData()==1) break;
		if (j!=Timeout) 
		{
			for (i=0;i<8;i++) if (AnalogResetQuery[k][i]!=buff_[i]) break;
			if (i!=8) return 0;    
		}
			else return 0;
		for (j=0;j<1000;j++);								//Çàäåðæêà
	}
	return 1;
}

unsigned char KMReset()
{
	long j;
	int i,k;

	for (k=0;k<64;k++)
	{
		LPC_UART2->FCR|=3;
		LPC_UART2->THR=KMResetQuery[k][0];
		LPC_UART2->THR=KMResetQuery[k][1];
		LPC_UART2->THR=KMResetQuery[k][2];
		LPC_UART2->THR=KMResetQuery[k][3];
		LPC_UART2->THR=KMResetQuery[k][4];
		LPC_UART2->THR=KMResetQuery[k][5];
		LPC_UART2->THR=KMResetQuery[k][6];
		LPC_UART2->THR=KMResetQuery[k][7];

		for (j=0;j<Timeout;j++) if (TryReceiveData()==1) break;
		if (j!=Timeout) 
		{
			for (i=0;i<8;i++) if (KMResetQuery[k][i]!=buff_[i]) break;
			if (i!=8) return 0;    
		}
			else return 0;
		for (j=0;j<1000;j++);								//Çàäåðæêà
	}
	return 1;
}

unsigned char KMGlobalBusReset()
{
	long j;
	int i,k;

	for (k=0;k<96;k++)
	{
	LPC_UART2->FCR|=3;
		LPC_UART2->THR=KMGlobalBusResetQuery[k][0];
		LPC_UART2->THR=KMGlobalBusResetQuery[k][1];
		LPC_UART2->THR=KMGlobalBusResetQuery[k][2];
		LPC_UART2->THR=KMGlobalBusResetQuery[k][3];
		LPC_UART2->THR=KMGlobalBusResetQuery[k][4];
		LPC_UART2->THR=KMGlobalBusResetQuery[k][5];
		LPC_UART2->THR=KMGlobalBusResetQuery[k][6];
		LPC_UART2->THR=KMGlobalBusResetQuery[k][7];

		for (j=0;j<Timeout;j++) if (TryReceiveData()==1) break;
		if (j!=Timeout) 
		{
			for (i=0;i<8;i++) if (KMGlobalBusResetQuery[k][i]!=buff_[i]) break;
			if (i!=8) return 0;    
		}
			else return 0;
		for (j=0;j<1000;j++);								//Çàäåðæêà
	}
	return 1;
}


int main (void)
{


	SER_Init();

	TCPLowLevelInit();


  HTTPStatus = 0;                                // clear HTTP-server's flag register

  TCPLocalPort = TCP_PORT_HTTP;                  // set port we want to listen to
 

  
  while (1)                                      // repeat forever
  {
    if (!(SocketStatus & SOCK_ACTIVE)) TCPPassiveOpen();   // listen for incoming TCP-connection
    DoNetworkStuff();                                      // handle network and easyWEB-stack
                                                           // events
		 if (!(SocketStatus & SOCK_ACTIVE)) {TCPPassiveOpen();SendCommandFlag=0;}
    HTTPServer();
  }
}

// This function implements a very simple dynamic HTTP-server.
// It waits until connected, then sends a HTTP-header and the
// HTML-code stored in memory. Before sending, it replaces
// some special strings with dynamic values.
// NOTE: For strings crossing page boundaries, replacing will
// not work. In this case, simply add some extra lines
// (e.g. CR and LFs) to the HTML-code.


void HTTPServer(void)
{ 
unsigned short len;
  long j;
  unsigned char st_tmp[100];
  int val=0,type=0,num=0,i,work=0,bus=0,crc,k,p1code=0,p2code=0;
  float val_volt,p1val,p2val,p1calcval;
  unsigned char QUERY[8],StateFlag=0;

  if (SocketStatus & SOCK_CONNECTED)             // check if somebody has connected to our TCP
  {
    if (SocketStatus & SOCK_DATA_AVAILABLE)      // check if remote TCP sent data
	{	
		if (SendCommandFlag==0) 
		{
			type=ReadParameter("type=");
			num=ReadParameter("num=");
			if (type==5) 
			{
				val_volt=ReadFloatParameter("val=");
				StateFlag=1;
			}
				else 
				{
					val=ReadParameter("val=");
					StateFlag=0;
				}
			work=ReadParameter("work=");
			bus=ReadParameter("bus=");
			if (type==6)
			{
				p1code=ReadParameter("p1code=");
				p2code=ReadParameter("p2code=");
				p1val=ReadFloatParameter("p1val=");
				p2val=ReadFloatParameter("p2val=");
			}

			if ((type!=-1)&&(num!=-1))
			{
				switch (type)  
				{
					case 1:												//àíàëîãîâûå ïàðàìåòðû
						{
		
							if ((val==-1)&&(work==-1)) {PWebSide="Komanda ne vypolnena. Dolzen byt6 ukazan hot9by odin iz parametrov val или work!\0"; break;}
							if ((num<1)||(num>96)) {PWebSide="Komanda ne vypolnena! Znachenie nomera kanala 'num' dolzno byt6 ot 1 do 96!\0"; break;}
							if (((val<0)||(val>4095))&&(val!=-1)) {PWebSide="Команда не выполнена! Значение параметра 'val' должно быть в диапазоне от 0 до 4095!\0"; break;}
							if ((work<-1)||(work>1)) {PWebSide="Komanda ne vypolnena! 'work'  0 / 1!\0"; break;}
							for (i=0;i<96;i++) if ((State.Analog[i].connectToBus==1)&&(State.Analog[i].work==1)&&(State.Analog[num-1].connectToBus==1)&&(i!=(num-1))) {PWebSide="Команда не выполнена! Данный канал необходимо отключить от общей шины!\0"; break;}

							if (i!=96) break;

							QUERY[0]=DMTable[num-1][0];				//Àäðåñ ìèêðîñõåìû DM
							QUERY[1]=DMTable[num-1][1]-1; 			//Íîìåð êàíàëà
							if (QUERY[1]<16) QUERY[0]|=64;			//Â àäðåñå óñòàíàâëèâàåì ñòàðøèé 7-îé áèò, îçíà÷àþùèé ÷òî ïàêåò ïðåäíàçíà÷åí äëÿ ïåðâîãî ïðîöåññîðà 
								else QUERY[1]-=16; 

							if (work==1) QUERY[1]|=128;					//Ïðèçíàê òîãî, ÷òî íåîáõîäèìî âêëþ÷èòü êàíàë
								else if (work==0) QUERY[1]|=64;			//Ïðèçíàê òîãî, ÷òî íåîáõîäèìî îòêëþ÷èòü êàíàë
 
							if (val==-1) val=65535;
							
							QUERY[2]=val;
							QUERY[3]=val>>8;
							crc=calcCRC(QUERY,6);
							QUERY[6]=crc;
							QUERY[7]=crc>>8;
							UART1_Send(QUERY,8);

							for (j=0;j<Timeout;j++) if (TryReceiveData()==1) break;
							if (j!=Timeout) 
							{
								for (i=0;i<8;i++) if (QUERY[i]!=buff_[i]) break;
								if (i==8) 
								{
									PWebSide="OK\0";
									if (val!=65535) 
									{
										State.Analog[num-1].val=val;
										State.Analog[num-1].val_volt=(State.Analog[num-1].val-Calibr.b[num-1])/Calibr.k[num-1]-2.0;
									}
									if (work!=-1) State.Analog[num-1].work=work;
								}
									else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 1\0"; 
							}
								else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 2\0";
					
							if (bus!=-1)
							{
								if (bus==1) 
								{
									for (i=0;i<96;i++) if ((State.Analog[i].connectToBus==1)&&(State.Analog[i].work==1)&&(State.Analog[num-1].work==1)&&(i!=(num-1))) {PWebSide="Kommutaciya na obshuyu shinu - sboi!\0"; break;}
									if (i!=96) break;
								}
								if (DMTable[num-1][0]==ADDR_DM1) 
								{
									QUERY[0]=KMGlobalBusTable[0][DMTable[num-1][1]-1][0];	//Àäðåñ ìèêðîñõåìû ÊÌ
									QUERY[1]=KMGlobalBusTable[0][DMTable[num-1][1]-1][1]-1; //Íîìåð êàíàëà
								}
								else if (DMTable[num-1][0]==ADDR_DM2) 
								{
									QUERY[0]=KMGlobalBusTable[1][DMTable[num-1][1]-1][0];	//Àäðåñ ìèêðîñõåìû ÊÌ
									QUERY[1]=KMGlobalBusTable[1][DMTable[num-1][1]-1][1]-1; //Íîìåð êàíàëà
								}
								else if (DMTable[num-1][0]==ADDR_DM3) 
								{
									QUERY[0]=KMGlobalBusTable[2][DMTable[num-1][1]-1][0];	//Àäðåñ ìèêðîñõåìû ÊÌ
									QUERY[1]=KMGlobalBusTable[2][DMTable[num-1][1]-1][1]-1; //Íîìåð êàíàëà
								}
								if (QUERY[1]<16) QUERY[0]|=64;			//Â àäðåñå óñòàíàâëèâàåì ñòàðøèé 7-îé áèò, îçíà÷àþùèé ÷òî ïàêåò ïðåäíàçíà÷åí äëÿ ïåðâîãî ïðîöåññîðà
									else QUERY[1]-=16;
										
								QUERY[2]=bus;
								crc=calcCRC(QUERY,6);
								QUERY[6]=crc;
								QUERY[7]=crc>>8;
											
								for (j=0;j<1000;j++);								//Çàäåðæêà
								UART1_Send(QUERY,8);
										
								for (j=0;j<Timeout;j++) if (TryReceiveData()==1) break;
								if (j!=Timeout) 
								{
									for (i=0;i<8;i++) if (QUERY[i]!=buff_[i]) break;
									if (i==8) 
									{
										PWebSide="OK\0"; 
										State.Analog[num-1].connectToBus=bus;
									}
										else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 3\0";
								}
									else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 4\0";
							}

							break;									  
						}
					case 2:											//êîíòàêòíûå ïàðàìåòðû
						{ 
							if ((num<1)||(num>64)) {PWebSide="Komanda ne vypolnena, 'num' 1  64!\0"; break;}
							if ((val<0)||(val>1)) {PWebSide="Komanda ne vypolnena 'val' 0  1!\0"; break;}
								
							QUERY[0]=KMTable[num-1][0];		//Àäðåñ ìèêðîñõåìû ÊÌ
							QUERY[1]=KMTable[num-1][1]-1; 	//Íîìåð êàíàëà
							if (QUERY[1]<16) QUERY[0]|=64;	//Â àäðåñå óñòàíàâëèâàåì ñòàðøèé 7-îé áèò, îçíà÷àþùèé ÷òî ïàêåò ïðåäíàçíà÷åí äëÿ ïåðâîãî ïðîöåññîðà
								else QUERY[1]-=16;

							QUERY[2]=val;
							crc=calcCRC(QUERY,6);
							QUERY[6]=crc;
							QUERY[7]=crc>>8;
							UART1_Send(QUERY,8);
							
							for (j=0;j<Timeout;j++) if (TryReceiveData()==1) break;
							if (j!=Timeout) 
							{
								for (i=0;i<8;i++) if (QUERY[i]!=buff_[i]) break;
								if (i==8) 
								{
									PWebSide="OK\0"; 
									State.Digital[num-1]=val;
								}
									else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 5!\0";
							}
								else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 6!\0";

							break;									  
						}
					case 3: 									  	//êîììóòàòîð îáùåé øèíû
						{
							if ((num<1)||(num>96)) {PWebSide="Komanda ne vypolnena! 'num' 1  96!\0"; break;}
							if ((val<0)||(val>1)) {PWebSide="Komanda ne vypolnena! 'val' 0  1!\0"; break;}
							if (val==1)
							{
								for (i=0;i<96;i++) if ((State.Analog[i].connectToBus==1)&&(State.Analog[i].work==1)&&(State.Analog[num-1].work==1)&&(i!=(num-1))) {PWebSide="Kommutaciya na obshuyu shinu - sboi 2!\0"; break;}
								if (i!=96) break;
							}

							if (DMTable[num-1][0]==ADDR_DM1) 
							{
								QUERY[0]=KMGlobalBusTable[0][DMTable[num-1][1]-1][0];	//Àäðåñ ìèêðîñõåìû ÊÌ
								QUERY[1]=KMGlobalBusTable[0][DMTable[num-1][1]-1][1]-1; //Íîìåð êàíàëà
							}
							else if (DMTable[num-1][0]==ADDR_DM2) 
							{
								QUERY[0]=KMGlobalBusTable[1][DMTable[num-1][1]-1][0];	//Àäðåñ ìèêðîñõåìû ÊÌ
								QUERY[1]=KMGlobalBusTable[1][DMTable[num-1][1]-1][1]-1; //Íîìåð êàíàëà
							}
							else if (DMTable[num-1][0]==ADDR_DM3) 
							{
								QUERY[0]=KMGlobalBusTable[2][DMTable[num-1][1]-1][0];	//Àäðåñ ìèêðîñõåìû ÊÌ
								QUERY[1]=KMGlobalBusTable[2][DMTable[num-1][1]-1][1]-1; //Íîìåð êàíàëà
							}
							if (QUERY[1]<16) QUERY[0]|=64;			//Â àäðåñå óñòàíàâëèâàåì ñòàðøèé 7-îé áèò, îçíà÷àþùèé ÷òî ïàêåò ïðåäíàçíà÷åí äëÿ ïåðâîãî ïðîöåññîðà
								else QUERY[1]-=16;
									
							QUERY[2]=val;
							crc=calcCRC(QUERY,6);
							QUERY[6]=crc;
							QUERY[7]=crc>>8;
										
							for (j=0;j<1000;j++);								//Çàäåðæêà
							UART1_Send(QUERY,8);
									
							for (j=0;j<Timeout;j++) if (TryReceiveData()==1) break;
							if (j!=Timeout) 
							{
								for (i=0;i<8;i++) if (QUERY[i]!=buff_[i]) break;
								if (i==8) 
								{
									PWebSide="OK\0"; 
									State.Analog[num-1].connectToBus=val;
								}
									else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 7!\0";
							}
								else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 8!\0";

							break;
						}
					case 4:
						{
							if ((num<1)||(num>4)) {PWebSide="Komanda ne vypolnena! 'num' 1  4!\0"; break;}
							switch (num)
							{
								case 1:													//Îòêëþ÷åíèå âñåõ êàíàëîâ
									{
										if (AnalogReset()==0) 
										{
											PWebSide="Komanda ne vypolnena! Modul ne otvechaet 9!\0";
											break;
										}
										if (KMReset()==0) 
										{
											PWebSide="Komanda ne vypolnena! Modul ne otvechaet 8!\0";
											break;
										}
										if (KMGlobalBusReset()==0) 
										{
											PWebSide="Komanda ne vypolnena! Modul ne otvechaet 9!\0";
											break;
										}
										PWebSide="OK\0";
										for (i=0;i<96;i++) 
										{
											State.Analog[i].val=0;
											State.Analog[i].work=0;
											State.Analog[i].connectToBus=0;
											State.Analog[i].val_volt=-2.0;
										}
										for (i=0;i<64;i++) State.Digital[i]=0;
										break;
									}
								case 2:                                                	//Àíàëîãîâûå
									{
										if (AnalogReset()==1) 
										{
											PWebSide="OK\0";
											for (i=0;i<96;i++) 
											{
												State.Analog[i].val=0;
												State.Analog[i].work=0;
												State.Analog[i].val_volt=-2.0;
											}
										}
											else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 10!\0";
										break;
									}
								case 3:													//Öèôðîâûå
									{
										if (KMReset()==1) 
										{
											PWebSide="OK\0";
											for (i=0;i<64;i++) State.Digital[i]=0;
										}
											else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 11\0";
										break;
									}
								case 4:                                                 //Îáùàÿ øèíà
									{
										if (KMGlobalBusReset()==1) 
										{
											PWebSide="OK\0";
											for (i=0;i<96;i++) State.Analog[i].connectToBus=0;
										}
											else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 12\0";
										break;
									}
							}
							break;
						}
					case 5:
						{
							if ((val_volt==-65535.0)&&(work==-1)) {PWebSide="Komanda ne vypolnena! val  work!\0"; break;}
							if ((num<1)||(num>96)) {PWebSide="Komanda ne vypolnena! 'num' 1  96!\0"; break;}
							if (((val_volt<-2.0)||(val_volt>8.0))&&(val_volt!=-65535.0)) {PWebSide="Komanda ne vypolnena! 'val'  -2  8!\0"; break;}
							if ((work<-1)||(work>1)) {PWebSide="Komanda ne vypolnena!! 'work' 0  1!\0"; break;}
							for (i=0;i<96;i++) if ((State.Analog[i].connectToBus==1)&&(State.Analog[i].work==1)&&(State.Analog[num-1].connectToBus==1)&&(i!=(num-1))) {PWebSide="Komanda ne vypolnena! Sboi obshey shiny!\0"; break;}
							if (i!=96) break;

							//val=(val_volt+2.0)*Calibr.k[num-1]+Calibr.b[num-1];
							val=(val_volt+2.0)*400;
							if (val<0) val=0;
							if (val>4095) val=4095;

							QUERY[0]=DMTable[num-1][0];				//Àäðåñ ìèêðîñõåìû DM
							QUERY[1]=DMTable[num-1][1]-1; 			//Íîìåð êàíàëà
							if (QUERY[1]<16) QUERY[0]|=64;			//Â àäðåñå óñòàíàâëèâàåì ñòàðøèé 7-îé áèò, îçíà÷àþùèé ÷òî ïàêåò ïðåäíàçíà÷åí äëÿ ïåðâîãî ïðîöåññîðà 
								else QUERY[1]-=16; 

							if (work==1) QUERY[1]|=128;					//Ïðèçíàê òîãî, ÷òî íåîáõîäèìî âêëþ÷èòü êàíàë
								else if (work==0) QUERY[1]|=64;			//Ïðèçíàê òîãî, ÷òî íåîáõîäèìî îòêëþ÷èòü êàíàë
							
                            if (val_volt==-65535.0) val=65535;

							QUERY[2]=val;
							QUERY[3]=val>>8;
							crc=calcCRC(QUERY,6);
							QUERY[6]=crc;
							QUERY[7]=crc>>8;
							UART1_Send(QUERY,8);

							for (j=0;j<Timeout;j++) if (TryReceiveData()==1) break;
							if (j!=Timeout) 
							{
								for (i=0;i<8;i++) if (QUERY[i]!=buff_[i]) break;
								if (i==8) 
								{
									PWebSide="OK\0";
									if (val!=65535) State.Analog[num-1].val=val;
									if (work!=-1) State.Analog[num-1].work=work;
									if (val_volt!=-65535.0) State.Analog[num-1].val_volt=val_volt;
								}
									else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 13\0";
							}
								else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 14\0";

							if (bus!=-1)
							{
								if (bus==1) 
								{
									for (i=0;i<96;i++) if ((State.Analog[i].connectToBus==1)&&(State.Analog[i].work==1)&&(State.Analog[num-1].work==1)&&(i!=(num-1))) {PWebSide="Komanda ne vypolnena! Sboi obshey shiny!\0"; break;}
									if (i!=96) break;
								}
								if (DMTable[num-1][0]==ADDR_DM1) 
								{
									QUERY[0]=KMGlobalBusTable[0][DMTable[num-1][1]-1][0];	//Àäðåñ ìèêðîñõåìû ÊÌ
									QUERY[1]=KMGlobalBusTable[0][DMTable[num-1][1]-1][1]-1; //Íîìåð êàíàëà
								}
								else if (DMTable[num-1][0]==ADDR_DM2) 
								{
									QUERY[0]=KMGlobalBusTable[1][DMTable[num-1][1]-1][0];	//Àäðåñ ìèêðîñõåìû ÊÌ
									QUERY[1]=KMGlobalBusTable[1][DMTable[num-1][1]-1][1]-1; //Íîìåð êàíàëà
								}
								else if (DMTable[num-1][0]==ADDR_DM3) 
								{
									QUERY[0]=KMGlobalBusTable[2][DMTable[num-1][1]-1][0];	//Àäðåñ ìèêðîñõåìû ÊÌ
									QUERY[1]=KMGlobalBusTable[2][DMTable[num-1][1]-1][1]-1; //Íîìåð êàíàëà
								}
								if (QUERY[1]<16) QUERY[0]|=64;			//Â àäðåñå óñòàíàâëèâàåì ñòàðøèé 7-îé áèò, îçíà÷àþùèé ÷òî ïàêåò ïðåäíàçíà÷åí äëÿ ïåðâîãî ïðîöåññîðà
									else QUERY[1]-=16;
										
								QUERY[2]=bus;
								crc=calcCRC(QUERY,6);
								QUERY[6]=crc;
								QUERY[7]=crc>>8;
											
								for (j=0;j<1000;j++);								//Çàäåðæêà
								UART1_Send(QUERY,8);
										
								for (j=0;j<Timeout;j++) if (TryReceiveData()==1) break;
								if (j!=Timeout) 
								{
									for (i=0;i<8;i++) if (QUERY[i]!=buff_[i]) break;
									if (i==8) 
									{
										PWebSide="OK\0"; 
										State.Analog[num-1].connectToBus=bus;
									}
										else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 15\0";
								}
									else PWebSide="Komanda ne vypolnena! Modul ne otvechaet 16\0";
							}

							break;
						}
					case 6:																	//êàëèáðîâêà
						{
							if ((p1code==-1)||(p2code==-1)||(p1val==-65535.0)||(p2val==-65535.0)) {PWebSide="Komanda ne vypolnena!  p1code, p2code, p1val, p2val!\0"; break;}
							if ((num<1)||(num>96)) {PWebSide="Komanda ne vypolnena! 'num' 1  96!\0"; break;}
							Calibr.k[num-1]=(p2code-p1code)/(p2val-p1val);	
							p1calcval=(p1code/409.5)-2.0;
							Calibr.b[num-1]=(p1calcval-p1val)*Calibr.k[num-1]+0.5;

						//	if (WriteCalibrToFlash(&Calibr)==OK) PWebSide="OK\0";
						//			else PWebSide="Komanda ne vypolnena! Sboi flash!\0";

							break;
						}
					case 7:
						{
						/*	if (num!=1) {PWebSide="Komanda ne vypolnena! 'num'  1!\0"; break;}

							switch (num)
							{
								case 1:
									{
										Str[0]=0;
										strcat(Str,"<pre>State Analog:<br>");
										strcat(Str,"#&#9 On#9 O&#9 Code#9 Sh<br>");
										for (i=0;i<96;i++)
										{
											sprintf(st_tmp,"%ld&#9 %ld&#9 %.3f&#9 %ld&#9 %ld<br>",i+1,State.Analog[i].work,State.Analog[i].val_volt,State.Analog[i].val,State.Analog[i].connectToBus);
											strcat(Str,st_tmp);
										}
					
										strcat(Str,"<br>Ñîñòîÿíèå öèôðîâûõ êàíàëîâ:<br>");
										strcat(Str,"#&#9 Ñîñòîÿíèå<br>");
										for (i=0;i<64;i++)
										{
											sprintf(st_tmp,"%ld&#9 %ld&#9<br>",i+1,State.Digital[i]);
											strcat(Str,st_tmp);
										}
										strcat(Str,"=====================================</pre>");
										break;
									}
							}     

							PWebSide=Str;*/
							break;
						}
					default:

					{
							PWebSide="Êîìàíäà íå âûïîëíåíà! Çíà÷åíèå 'type' äîëæíî áûòü â äèàïàçîíå îò 1 äî 7!\0"; 
							break;
						}
				}

			}
			else PWebSide="komanda ne vypolnena. Nepravilny format zaprosa: http://192.168.0.99/type=1num=2val=3work=1\0";
		}		 
		SendCommandFlag=1;
      	TCPReleaseRxBuffer();                      // and throw it away
	}
	if ((SocketStatus & SOCK_TX_BUF_RELEASED)&&(SendCommandFlag==1))     // check if buffer is free for TX
    {
		if (!(HTTPStatus & HTTP_SEND_PAGE)) HTTPBytesToSend = strlen(PWebSide);   // get HTML length, ignore trailing zero
		if (HTTPBytesToSend > MAX_TCP_TX_DATA_SIZE)     // transmit a segment of MAX_SIZE
		{
			if (!(HTTPStatus & HTTP_SEND_PAGE))
			{
			  memcpy(TCP_TX_BUF, GetResponse, sizeof(GetResponse) - 1);
			  memcpy(TCP_TX_BUF + sizeof(GetResponse) - 1, PWebSide, MAX_TCP_TX_DATA_SIZE - sizeof(GetResponse) + 1);
			  HTTPBytesToSend -= MAX_TCP_TX_DATA_SIZE - sizeof(GetResponse) + 1;
			  PWebSide += MAX_TCP_TX_DATA_SIZE - sizeof(GetResponse) + 1;
			}
			else
			{
			  memcpy(TCP_TX_BUF, PWebSide, MAX_TCP_TX_DATA_SIZE);
			  HTTPBytesToSend -= MAX_TCP_TX_DATA_SIZE;
			  PWebSide += MAX_TCP_TX_DATA_SIZE;
			}

			TCPTxDataCount = MAX_TCP_TX_DATA_SIZE;   // bytes to xfer
			TCPTransmitTxBuffer();                   // xfer buffer
        }
        else if (HTTPBytesToSend)                  // transmit leftover bytes
		{
			if (!(HTTPStatus & HTTP_SEND_PAGE))
			{
			  memcpy(TCP_TX_BUF, GetResponse, sizeof(GetResponse) - 1);
			  memcpy(TCP_TX_BUF + sizeof(GetResponse) - 1, PWebSide, HTTPBytesToSend );
			  TCPTxDataCount = sizeof(GetResponse)+HTTPBytesToSend-1;   // bytes to xfer
			}
			else 
			{
				memcpy(TCP_TX_BUF, PWebSide, HTTPBytesToSend); 
				TCPTxDataCount = HTTPBytesToSend;        // bytes to xfer
			}
			
			TCPTransmitTxBuffer();                   // send last segment
			HTTPBytesToSend = 0;                     // all data sent
			
		}

		if (HTTPBytesToSend==0) TCPClose();         // and close connection
		
		HTTPStatus |= HTTP_SEND_PAGE;              // ok, 1st loop executed
    }
  }
  else
    HTTPStatus &= ~HTTP_SEND_PAGE;               // reset help-flag if not connected
}



unsigned int GetAD7Val(void)
{
//  aaScrollbar = (aaScrollbar +16) % 1024;
//  adcValue = (aaScrollbar / 10) * 1000/1024;
//  return aaScrollbar;
}

// Code Red - Original MSP430 version of GetAD7Val() removed
/*
// samples and returns the AD-converter value of channel 7
// (associated with Port P6.7)

unsigned int GetAD7Val(void)
{
  ADC12CTL0 = ADC12ON | SHT0_15 | REF2_5V | REFON;   // ADC on, int. ref. on (2,5 V),
                                                     // single channel single conversion
  ADC12CTL1 = ADC12SSEL_2 | ADC12DIV_7 | CSTARTADD_0 | SHP;// MCLK / 8 = 1 MHz

  ADC12MCTL0 = SREF_1 | INCH_7;                  // int. ref., channel 7
  
  ADC12CTL0 |= ENC;                              // enable conversion
  ADC12CTL0 |= ADC12SC;                          // sample & convert
  
  while (ADC12CTL0 & ADC12SC);                   // wait until conversion is complete
  
  ADC12CTL0 &= ~ENC;                             // disable conversion

  return ADC12MEM0 / 41;                         // scale 12 bit value to 0..100%
}

// End of Original MSP430 version of GetAD7Val()
*/


// Code Red - Original GetTempVal() removed
// Function no longer used
/*
// samples and returns AD-converter value of channel 10
// (MSP430's internal temperature reference diode)
// NOTE: to get a more exact value, 8-times oversampling is used

unsigned int GetTempVal(void)
{
  unsigned long ReturnValue;

  ADC12CTL0 = ADC12ON | SHT0_15 | MSH | REFON;   // ADC on, int. ref. on (1,5 V),
                                                 // multiple sample & conversion
  ADC12CTL1 = ADC12SSEL_2 | ADC12DIV_7 | CSTARTADD_0 | CONSEQ_1 | SHP;   // MCLK / 8 = 1 MHz

  ADC12MCTL0 = SREF_1 | INCH_10;                 // int. ref., channel 10
  ADC12MCTL1 = SREF_1 | INCH_10;                 // int. ref., channel 10
  ADC12MCTL2 = SREF_1 | INCH_10;                 // int. ref., channel 10
  ADC12MCTL3 = SREF_1 | INCH_10;                 // int. ref., channel 10
  ADC12MCTL4 = SREF_1 | INCH_10;                 // int. ref., channel 10
  ADC12MCTL5 = SREF_1 | INCH_10;                 // int. ref., channel 10
  ADC12MCTL6 = SREF_1 | INCH_10;                 // int. ref., channel 10
  ADC12MCTL7 = EOS | SREF_1 | INCH_10;           // int. ref., channel 10, last seg.
  
  ADC12CTL0 |= ENC;                              // enable conversion
  ADC12CTL0 |= ADC12SC;                          // sample & convert
  
  while (ADC12CTL0 & ADC12SC);                   // wait until conversion is complete
  
  ADC12CTL0 &= ~ENC;                             // disable conversion

  ReturnValue = ADC12MEM0;                       // sum up values...
  ReturnValue += ADC12MEM1;
  ReturnValue += ADC12MEM2;
  ReturnValue += ADC12MEM3;
  ReturnValue += ADC12MEM4;
  ReturnValue += ADC12MEM5;
  ReturnValue += ADC12MEM6;
  ReturnValue += ADC12MEM7;

  ReturnValue >>= 3;                             // ... and divide by 8

  if (ReturnValue < 2886) ReturnValue = 2886;    // lower bound (0% = 20�C)
  ReturnValue = (ReturnValue - 2886) / 2.43;     // convert AD-value to a temperature from
                                                 // 20�C...45�C represented by a value
                                                 // of 0...100%
  if (ReturnValue > 100) ReturnValue = 100;      // upper bound (100% = 45�C)

  return ReturnValue;
}
// End of Original MSP430 version of GetTempVal()
*/


// searches the TX-buffer for special strings and replaces them
// with dynamic values (AD-converter results)

// Code Red - new version of InsertDynamicValues()
void InsertDynamicValues(void)
{
  unsigned char *Key;
           char NewKey[6];
  unsigned int i;
  
  if (TCPTxDataCount < 4) return;                     // there can't be any special string
  
  Key = TCP_TX_BUF;
  
  for (i = 0; i < (TCPTxDataCount - 3); i++)
  {
    if (*Key == 'A')
     if (*(Key + 1) == 'D')
       if (*(Key + 3) == '%')
         switch (*(Key + 2))
         {
           case '8' :                                 // "AD8%"?
           {
             sprintf(NewKey, "%04d", GetAD7Val());     // insert pseudo-ADconverter value
             memcpy(Key, NewKey, 4);                  
             break;
           }
           case '7' :                                 // "AD7%"?
           {
             sprintf(NewKey, "%3u", adcValue);     // copy saved value from previous read
             memcpy(Key, NewKey, 3);                 
             break;
           }
		   case '1' :                                 // "AD1%"?
           {
 			 sprintf(NewKey, "%4u", ++aaPagecounter);    // increment and insert page counter
             memcpy(Key, NewKey, 4);  
//			 *(Key + 3) = ' ';  
             break;
           }
         }
    Key++;
  }
}


// Code Red - commented out original InsertDynamicValues()
/*
void InsertDynamicValues(void)
{
  unsigned char *Key;
  unsigned char NewKey[5];
  unsigned int i;
  
  if (TCPTxDataCount < 4) return;                     // there can't be any special string
  
  Key = TCP_TX_BUF;
  
  for (i = 0; i < (TCPTxDataCount - 3); i++)
  {
    if (*Key == 'A')
     if (*(Key + 1) == 'D')
       if (*(Key + 3) == '%')
         switch (*(Key + 2))
         {
           case '7' :                                 // "AD7%"?
           {
             sprintf(NewKey, "%3u", GetAD7Val());     // insert AD converter value
             memcpy(Key, NewKey, 3);                  // channel 7 (P6.7)
             break;
           }
           case 'A' :                                 // "ADA%"?
           {
             sprintf(NewKey, "%3u", GetTempVal());    // insert AD converter value
             memcpy(Key, NewKey, 3);                  // channel 10 (temp.-diode)
             break;
           }
         }
    Key++;
  }
}

// Code Red - End of original InsertDynamicValues ()
*/

// Code Red - Deleted InitOsc() and InitPorts() as not required
// by LPC 1776

/*
// enables the 8MHz crystal on XT1 and use
// it as MCLK

void InitOsc(void)
{
  WDTCTL = WDTPW | WDTHOLD;                      // stop watchdog timer

  BCSCTL1 |= XTS;                                // XT1 as high-frequency
  _BIC_SR(OSCOFF);                               // turn on XT1 oscillator
                          
  do                                             // wait in loop until crystal is stable 
    IFG1 &= ~OFIFG;
  while (IFG1 & OFIFG);

  BCSCTL1 |= DIVA0;                              // ACLK = XT1 / 2
  BCSCTL1 &= ~DIVA1;
  
  IE1 &= ~WDTIE;                                 // disable WDT int.
  IFG1 &= ~WDTIFG;                               // clear WDT int. flag
  
  WDTCTL = WDTPW | WDTTMSEL | WDTCNTCL | WDTSSEL | WDTIS1; // use WDT as timer, flag each
                                                           // 512 pulses from ACLK
                                                           
  while (!(IFG1 & WDTIFG));                      // count 1024 pulses from XT1 (until XT1's
                                                 // amplitude is OK)

  IFG1 &= ~OFIFG;                                // clear osc. fault int. flag
  BCSCTL2 = SELM0 | SELM1;                       // set XT1 as MCLK
}  

void InitPorts(void)
{
  P1SEL = 0;                                     // switch all unused ports to output
  P1OUT = 0;                                     // (rem.: ports 3 & 5 are set in "cs8900.c")
  P1DIR = 0xFF;

  P2SEL = 0;
  P2OUT = 0;
  P2DIR = 0xFF;

  P4SEL = 0;
  P4OUT = 0;
  P4DIR = 0xFF;

  P6SEL = 0x80;                                  // use P6.7 for the ADC module
  P6OUT = 0;
  P6DIR = 0x7F;                                  // all output except P6.7
}

*/
