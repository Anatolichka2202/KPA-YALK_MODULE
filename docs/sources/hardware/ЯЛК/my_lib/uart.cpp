#include "uart.h"

/*UART::UART(UARTName name,UARTOption *Option):Peripheral((ulong*)name){
	base_addr[LCR]=0x80;		  // DLAB = 1  	
	//Если 0 то дефолтные опции
	if(Option==NULL){
		base_addr[LCR] = 0;		// 8 bits, no Parity, 1 Stop bit 
    	base_addr[DLM] = 0;							
    	base_addr[DLL] = 0;			//??? как определять?
    	base_addr[FCR] = 0x87;		// Enable and reset TX and RX FIFO. 
	}
	else{
   		base_addr[LCR] |= Option->lcr;		/// 8 bits, no Parity, 1 Stop bit
    	base_addr[DLM] = Option->dlm;							
    	base_addr[DLL] = Option->dll;			//??? как определять?
    	base_addr[FCR] = Option->fcr;		// Enable and reset TX and RX FIFO. 
		//установка прерываний
	
		
	}
	base_addr[LCR]&= 0x7F;		// DLAB = 0 	
	if(Option->type_int!=0){
		base_addr[IER]=Option->type_int; 	
	}
} */ 
UART::UART(UARTName name):Peripheral((ulong*)name){
};

void UART::Init(const UARTOption* Option){
//void UART::Init(UARTOption* Option){
	base_addr[LCR] = 0x80;			   // DLAB = 1 
	base_addr[LCR] |= Option->lcr;		// 8 bits, no Parity, 1 Stop bit 
    base_addr[DLM] = Option->dlm;							
    base_addr[DLL] = Option->dll;			//??? как определять?
    base_addr[FCR] = Option->fcr;		// Enable and reset TX and RX FIFO.
	base_addr[LCR] &= 0x7F;		// DLAB = 0 
	//установка прерываний
	if(Option->type_int!=0){
		base_addr[IER]=Option->type_int; 	
	}
	
}
const uint UART::GetState(){
	return base_addr[LSR];
}
void UART::ClearInt(){}
bool UART::Read(byte* buf,const uint& size){
	if(base_addr[LSR]&1==0) return false; 
	for(int i=0;i<size;i++){
		buf[i]=base_addr[RBR];
	}
	return true;
}
bool UART::Send(const byte* buf,const uint& size){
	for(int i=0;i<size;i++){
		while(!(base_addr[LSR]&0x20));
		base_addr[THR]=buf[i];
	}
	return true;
}
UART* UART::Get(uint num){
	if(num>UART_SIZE) return (UART*)NULL;
	return DEV[num];
}
void UART::Register(uint num,UART* obj){
	if(num>UART_SIZE) return;
	DEV[num]=obj;
}

const uint UART::GetIntStatus(){
	return base_addr[IIR];
}


void UART::ClearRxFIFO(){
	base_addr[FCR]|=0x2;
}
void UART::ClearTxFIFO(){
	base_addr[FCR]|=0x4;
}
UART::~UART(){};

UART* UART::DEV[4]={};
