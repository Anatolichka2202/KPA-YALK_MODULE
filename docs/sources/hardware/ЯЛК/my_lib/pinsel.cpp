#include "pinsel.h"



void PINSEL::Set(uint port,uint pin,FunCode code){
	uint offset=port<<1;
	uint shift=pin<<1;
	if(pin>15){
		offset++;
		shift-=32;
	}
	base_addr[offset]&=~(3<<shift);
	base_addr[offset]|=code<<shift;
}
		
void PINSEL::Set(uint num,uint val){
	base_addr[num]=val;
}
uint PINSEL::Get(uint port,uint pin){
	uint offset=port<<1;
	uint shift=pin<<1;
	if(pin>15){
		offset++;
		pin-=32;
	}
	return base_addr[offset]&(3<<shift);
}
uint PINSEL::Get(uint num){
	return base_addr[num];
} 

ulong* PINSEL::base_addr=(ulong*)0xE002C000;
