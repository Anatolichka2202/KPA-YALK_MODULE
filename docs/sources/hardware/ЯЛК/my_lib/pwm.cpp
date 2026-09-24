#include "pwm.h"

PWM::PWM(PWMName name):Peripheral((ulong*)name){};
PWM::~PWM(){};

void PWM::Init(PWMOption* opt){
	base_addr[PC]=opt->prescaler;
	base_addr[MCR]=opt->mcr;
	base_addr[MR0]=opt->mr0;
	base_addr[MR1]=opt->mr1;
	base_addr[MR2]=opt->mr2;
	base_addr[MR3]=opt->mr3;
	base_addr[MR4]=opt->mr4;
	base_addr[MR5]=opt->mr5;
	base_addr[MR6]=opt->mr6;
	base_addr[PCR]=opt->pcr;
//	base_addr[LER]=opt->latch;	
}	

void PWM::Enable(){
	base_addr[TCR]=9;
}
void PWM::Disable(){
	base_addr[TCR]=0;
}
void PWM::Clear(){
	base_addr[TCR]=2;
}

void PWM::ClearInt(){
	base_addr[IR]|=base_addr[IR];
}

uint PWM::GetIntStatus() const{
	return base_addr[IR];
}


PWM* PWM::DEV[1]={};

void PWM::Register(const uint& num,PWM* obj){
	//if(!test_num(num)) return;   //а в test_num проверять корректность циферки
	if(num>0) return;
	DEV[num]=obj;
}
PWM* PWM::Get(const uint num){
	if(num>0) return (PWM*)0;
	return DEV[num];
}