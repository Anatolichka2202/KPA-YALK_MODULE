#include "port.h"


Port::Port(PortName name,uint _mask,uint mode):Peripheral((ulong*)name),mask(_mask){
	SCS|=1;		//Шустрые порты
	//вывод
	if(mode==OUTPUT){
		base_addr[DIR]=~mask&0xFFFFFFFF;	
	}
	base_addr[MASK]=mask;
	
}
Port::~Port(){}
void Port::Init(PortOption* opt){
	if(opt->mode==OUTPUT){
		base_addr[DIR]=~(opt->mask)&0xFFFFFFFF;	
	}
	base_addr[MASK]=opt->mask;
	mask=opt->mask;	
}
Port* Port::operator=(uint val){
	base_addr[MASK]=mask;
	base_addr[PIN]=val;
	return this;
}

void Port::SetValue(uint val){
	base_addr[MASK]=mask;
	base_addr[PIN]=val;
}
uint Port::GetValue() const{
	base_addr[MASK]=mask;
	return base_addr[PIN];
}