#ifndef __PWM_H_
#define __PWM_H_

#include "peripheral.h"

enum PWMName{
	PWM_0=0xE0014000,
	PWM_1=0xE0018000
};

typedef struct PWMOption{
	//byte ct_mode;		// counter | timer mode
	//byte input_select;  // PCAP 1.0 |CAP1.1
	uint mcr;			//match control
	//uint ccr;			//capture control
	uint pcr;		//PWM control register
	//mr value
	uint mr0;
	uint mr1;
	uint mr2;
	uint mr3;
	uint mr4;
	uint mr5;
	uint mr6;
	//cr value
	//uint cr0;
	//uint cr1;
	//uint cr2;
	//uint cr3;
	uint prescaler;		//делитель
};

class PWM:public Peripheral{
	private:
		//Регистры
		enum{
			IR=0,TCR=1,TC=2,PR=3,PC=4,MCR=5,MR0=6,MR1=7,MR2=8,MR3=9,CCR=10,
			CR0=11,CR1=12,CR2=13,CR3=14,MR4=16,MR5=17,MR6=18,PCR=19,LER=20,CTCR=28	
		};
		//static ulong* MR_addr[];
		//static ulong* CR_addr[];
		static PWM* DEV[1]; 
	public:
		//прерывания
		enum{
			IMR0=1,IMR1=2,IMR2=4,IMR3=8,IMR4=256,IMR5=512,IMR6=1024
		};

		PWM(PWMName name);
		~PWM();

		void Init(PWMOption* opt);		

		void Enable();	  //Включать тут все и счетчик и PWM	
		void Disable();	  //А тут все выключать
		void Clear();	  //сброс
		void ClearInt();  //Сброс флагов прерывания
		uint GetIntStatus() const; //Статус прерывания
		//этот функционал пока не требуется 

		//void SetMR(uint num,uint val);
		//void SetCR(uint num,uint val);
		//void SetLatch(uint latch);
		//void CounterEnable();
		static void Register(const uint& num,PWM* obj);
		static PWM* Get(const uint num);
}; 


#define PREPARE_PWM PWM pwm1_dev(PWM_1); PWM::Register(0,&pwm1_dev); 
#endif
