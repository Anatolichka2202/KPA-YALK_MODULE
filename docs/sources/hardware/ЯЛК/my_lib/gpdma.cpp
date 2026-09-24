#include "gpdma.h"

GPDMA::GPDMA(DMAChannelName name):Peripheral((ulong*)DMA_BASE_ADDR),chan((ulong*)name){
	if (name==CHAN_0) chan_mask=1;
	else chan_mask=2;
};
GPDMA::~GPDMA(){}

void GPDMA::Init(const GPDMAOption* opt){
	//настройка контроллера
	base_addr[Configuration]=1|(opt->endian_mode)<<1;
	while ( !(base_addr[Configuration] & 0x1) );
	//GPDMA_CH0_DEST= DMA_DST; выставляется непосредственно перед GPDMA::Start(const uint size=0);
	//сброс ошибок и прерываний	хотя может стоит вынести в GPDMA::Clear(); и вызывать перед работой
	base_addr[IntClear] = chan_mask;
	base_addr[IntErrClr] = chan_mask; 
	//настройка канала

	chan[CH_Control]=
		((opt->SBSize)<<12)|
		((opt->DBSize)<<15)|
		((opt->SWidth)<<18)|
		((opt->DWidth)<<21)|
		((opt->SI)<<26)|
		((opt->DI)<<27)|
		((opt->Prot)<<28)|
		((opt->IntEnable)<<31);
	chan[CH_Configuration]=
		((opt->SrcP)<<1)|
		((opt->DestP)<<6)|
		((opt->FlowCntl)<<11)|
		((opt->IE)<<14)|
		((opt->ITC)<<15)|
		((opt->L)<<16)|
		((opt->H)<<18);
	//GPDMA_CH0_CTRL = (0x0 << 12) | (0x0<< 15)|(0<<18)|(0<<21)| (1 << 26)|(1<<31);
	//GPDMA_CH0_CFG =  (0x01 << 11)|(0x0 << 6)|(1<<15)|(1<<14);
};
void GPDMA::SetSrc(void* addr){
	chan[CH_SrcAddr]=(ulong)addr;
}
void GPDMA::SetDest(void* addr){
	chan[CH_DestAddr]=(ulong)addr;
}
void GPDMA::Start(const uint tsize){
	chan[CH_Control]|=tsize;	
	chan[CH_Configuration]|=1;
}
void GPDMA::Stop(){
	chan[CH_Configuration]&=(~1);
}

//там регистров прерыывания и ошибок много, какой когда нужен беспонятия
//коды ошибок еще надо доработать!!!!!
uint GPDMA::GetIntStatus(){
	return base_addr[IntStatus];
}
void GPDMA::ClearInt(){
	base_addr[IntClear]|=chan_mask;
}

//-------------------РЕЕСТР------------------------//
//!!! попробовать реализовать через шаблон		
void GPDMA::Register(GPDMA* const obj,const uint num){
	if(num>=DMA_CHAN_SIZE) return;
	DEV[num]=obj;
}
GPDMA* GPDMA::Get(uint num){
	if(num>=DMA_CHAN_SIZE) return (GPDMA*)0x0;
	else	return DEV[num];
}

GPDMA* GPDMA::DEV[2];
