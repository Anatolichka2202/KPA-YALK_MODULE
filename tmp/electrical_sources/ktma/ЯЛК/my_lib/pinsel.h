#ifndef __PINSEL_H_
#define __PINSEL_H_
#include "peripheral.h"
enum FunCode{
	func_00=0,func_01=1,func_10=2,func_11=3
};
class PINSEL{
	private:
		static ulong* base_addr;
		PINSEL(){};
		~PINSEL(){};
	public:
		//Установить ногу по порту
		static void Set(uint port,uint pin,FunCode code);
		static void Set(uint num,uint val);
		static uint Get(uint port,uint pin);
		static uint Get(uint num); 
		
};


//
//
//PINSEL::Set(2,1,PINSEL::func_01);
////P1[20] SCK0
//PINSEL::Set(1,20,func_01); 
////P1[21] SSEL0
//PINSEL::Set(1,21,func_01);
////P1[23]=MOSI
//PINSEL::Set(1,23,func_01);
////P1[24] MISO
//PINSEL::Set(1,24,func_01);
//
//PINSEL::Set(3,0xffff);
#endif
