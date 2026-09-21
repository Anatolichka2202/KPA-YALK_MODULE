#include "vic.h"

VIC::VIC(){}
VIC::~VIC(){}

bool VIC::InstallIRQ(VIC::INT_SRC int_num,void* Handler,uint prio){
	base_addr[IntEnClr] = 1 << int_num;	// Disable Interrupt 
    if ( int_num >= VIC_SIZE ){
		return false;
    }
    else{
		vect_addr[int_num] = (ulong)Handler;	// set interrupt vector
		priority[int_num] = prio;
		base_addr[IntEnable] = 1 << int_num;	// Enable Interrupt
		return true;
    }	
}

void VIC::Init(){
	base_addr[IntEnClr] = 0xFFFFFFFF;
	base_addr[Addr] = 0;
	base_addr[IntSelect] = 0;

	for(uint i=0;i<VIC_SIZE;i++){
		vect_addr[i]=0;
		priority[i]=0xF;
	}
}

void VIC::IntClear(){
	base_addr[Addr]=0;
}
ulong* VIC::base_addr = (ulong*)0xFFFFF000;
ulong* VIC::vect_addr = (ulong*)0xFFFFF100;
ulong* VIC::priority  = (ulong*)0xFFFFF200;
const uint VIC::VIC_SIZE = 32;
