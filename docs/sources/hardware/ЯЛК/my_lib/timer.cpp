#include "timer.h"



Timer::Timer(TimerName name):Peripheral((ulong*)name){};
Timer::~Timer(){}

void Timer::Init(const TimerMatchOption* opt){
	base_addr[PR]=opt->presc;
	base_addr[TCR]=2;
	base_addr[MCR]=opt->mode;
	base_addr[MR0]=opt->mr0;
	base_addr[MR1]=opt->mr1;
	base_addr[MR2]=opt->mr2;
	base_addr[MR3]=opt->mr3;
}
//—брос
void Timer::Clear(){
   base_addr[TCR]=3;
}

void Timer::Start(){
   	base_addr[TCR]=1;
}
void Timer::Stop(){
	base_addr[TCR]&=~1;	
}
uint Timer::GetCount(){
	return base_addr[TC];
}

void Timer::ClearInt(){
	base_addr[IR]|=base_addr[IR];
}

const uint Timer::GetIntStatus() const{
	return base_addr[IR];
}

//реестр!

void Timer::Register(uint num,Timer* obj){
	if(num>TIMER_SIZE) return;
	DEV[num]=obj;
}
Timer* Timer::Get(uint num){
	if(num>TIMER_SIZE) return (Timer*)NULL;
	return DEV[num];
}

Timer* Timer::DEV[4];
