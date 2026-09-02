#include "gd32f30x.h"

unsigned char command[4],state=0,Flag_100Hz=0;
unsigned char Query[4],QueryCounter=0,SendBuf[2][14],SendBufWrite=0,SendBufRead=1;
unsigned short U_BI=0, U_NI=0, U_PWR=0;

const char unsigned Crc8Table[256] = {
    0x00, 0x31, 0x62, 0x53, 0xC4, 0xF5, 0xA6, 0x97,
    0xB9, 0x88, 0xDB, 0xEA, 0x7D, 0x4C, 0x1F, 0x2E,
    0x43, 0x72, 0x21, 0x10, 0x87, 0xB6, 0xE5, 0xD4,
    0xFA, 0xCB, 0x98, 0xA9, 0x3E, 0x0F, 0x5C, 0x6D,
    0x86, 0xB7, 0xE4, 0xD5, 0x42, 0x73, 0x20, 0x11,
    0x3F, 0x0E, 0x5D, 0x6C, 0xFB, 0xCA, 0x99, 0xA8,
    0xC5, 0xF4, 0xA7, 0x96, 0x01, 0x30, 0x63, 0x52,
    0x7C, 0x4D, 0x1E, 0x2F, 0xB8, 0x89, 0xDA, 0xEB,
    0x3D, 0x0C, 0x5F, 0x6E, 0xF9, 0xC8, 0x9B, 0xAA,
    0x84, 0xB5, 0xE6, 0xD7, 0x40, 0x71, 0x22, 0x13,
    0x7E, 0x4F, 0x1C, 0x2D, 0xBA, 0x8B, 0xD8, 0xE9,
    0xC7, 0xF6, 0xA5, 0x94, 0x03, 0x32, 0x61, 0x50,
    0xBB, 0x8A, 0xD9, 0xE8, 0x7F, 0x4E, 0x1D, 0x2C,
    0x02, 0x33, 0x60, 0x51, 0xC6, 0xF7, 0xA4, 0x95,
    0xF8, 0xC9, 0x9A, 0xAB, 0x3C, 0x0D, 0x5E, 0x6F,
    0x41, 0x70, 0x23, 0x12, 0x85, 0xB4, 0xE7, 0xD6,
    0x7A, 0x4B, 0x18, 0x29, 0xBE, 0x8F, 0xDC, 0xED,
    0xC3, 0xF2, 0xA1, 0x90, 0x07, 0x36, 0x65, 0x54,
    0x39, 0x08, 0x5B, 0x6A, 0xFD, 0xCC, 0x9F, 0xAE,
    0x80, 0xB1, 0xE2, 0xD3, 0x44, 0x75, 0x26, 0x17,
    0xFC, 0xCD, 0x9E, 0xAF, 0x38, 0x09, 0x5A, 0x6B,
    0x45, 0x74, 0x27, 0x16, 0x81, 0xB0, 0xE3, 0xD2,
    0xBF, 0x8E, 0xDD, 0xEC, 0x7B, 0x4A, 0x19, 0x28,
    0x06, 0x37, 0x64, 0x55, 0xC2, 0xF3, 0xA0, 0x91,
    0x47, 0x76, 0x25, 0x14, 0x83, 0xB2, 0xE1, 0xD0,
    0xFE, 0xCF, 0x9C, 0xAD, 0x3A, 0x0B, 0x58, 0x69,
    0x04, 0x35, 0x66, 0x57, 0xC0, 0xF1, 0xA2, 0x93,
    0xBD, 0x8C, 0xDF, 0xEE, 0x79, 0x48, 0x1B, 0x2A,
    0xC1, 0xF0, 0xA3, 0x92, 0x05, 0x34, 0x67, 0x56,
    0x78, 0x49, 0x1A, 0x2B, 0xBC, 0x8D, 0xDE, 0xEF,
    0x82, 0xB3, 0xE0, 0xD1, 0x46, 0x77, 0x24, 0x15,
    0x3B, 0x0A, 0x59, 0x68, 0xFF, 0xCE, 0x9D, 0xAC
};

unsigned char Crc8(unsigned char *pcBlock, unsigned char len)
{
    unsigned char crc = 0xFF;
 
    while (len--)
        crc = Crc8Table[crc ^ *pcBlock++];
 
    return crc;
}

void ChangeState(state)
{
	if ((state&1)!=0)												//БИ
	{
		GPIO_BOP(GPIOC)=1<<7;                                      	//BAT_SEL-1
		GPIO_BC(GPIOC)=1<<6;                                   		//GP_SEL-0

		GPIO_BOP(GPIOA)=1<<10;                                      //BIOUT-1
		GPIO_BC(GPIOA)=1<<9;                                   		//NIOUT-0
	}
		else														//НИ
		{
			GPIO_BC(GPIOC)=1<<7;                                    //BAT_SEL-0
			GPIO_BOP(GPIOC)=1<<6;                                   //GP_SEL-1

			GPIO_BC(GPIOA)=1<<10;                                  	//BIOUT-0
			GPIO_BOP(GPIOA)=1<<9;                             		//NIOUT-1
		}
	if ((state&2)!=0) GPIO_BC(GPIOD)=1<<14;							//ВКЛ РПУ					
		else GPIO_BOP(GPIOD)=1<<14;                                 //ВЫКЛ РПУ
}

int main()
{
    int i,j;
	unsigned short val;

	//Настройка тактирования
	RCU_CTL |= 1<<16;										//Включаем внешний осциллятор											
	while ((RCU_CTL & (1<<17)) == 0);			            //Ожидаем включения
	RCU_CFG0 |= (1<<10)|(3<<18)|(1<<16);  					//APB2 = AHB/1=80МГц, APB1 = AHB/2=40МГц, PLLM=5 (CPUCLK=80МГц), ADCCLK=40МГц, HXTAL на вход PLL										
	RCU_CTL |= 1<<24;										//Включаем PLL
	while ((RCU_CTL & (1<<25)) == 0);			            //Ожидаем включения
    RCU_CFG0 &= ~3;
    RCU_CFG0 |= 1<<1;										//Тактирование от PLL
	while((RCU_CFG0 & 0xC)!=8); 

    //Настраиваем все выводы 
	RCU_APB2EN|=1|(1<<2)|(1<<3)|(1<<4)|(1<<5);				//Тактирование портов A, B, C, D и AFIO
	GPIO_CTL0(GPIOA)=0x44444000;                            //PA0-PA2-входы АЦП, PA7-команда перехода на НИ, PA4-команда перехода на БИ, PA6-ВКЛ РПУ, PA3-ВЫКЛ РПУ 
	GPIO_CTL1(GPIOA)=0x44444334;							//PA9-индикация MCU_NIOUT, PA10- индикация MCU_BIOUT
	GPIO_CTL0(GPIOB)=0x4B444444;							//PB6-UART0_TX, PB7-UART0_RX
	GPIO_CTL1(GPIOB)=0x33334444;                            //PB12-PB15-временные команды
	GPIO_CTL0(GPIOC)=0x33444444;                           	//PC6-GP_SEL, PC7-BAT_SEL
	GPIO_CTL1(GPIOC)=0x44444434;                            //PC9-DIR2
	GPIO_CTL1(GPIOD)=0x43444444;                            //PD14-RF_EN
	AFIO_PCF0|=1<<2;                                        //Remap UART0

	ChangeState(0);

	//Настраиваем DMA для отправки данных по УАРТ
	RCU_AHBEN|=1;											//Тактирование DMA0

	//Настраиваем UART для обмена данными с ЯФК                            
	RCU_APB2EN|=1<<14;							            //Тактирование UART0
	USART_CTL0(USART0)=(1<<2)|(1<<3)|(1<<5)|(1<<6); 		//Включаем приемник, передатчик, прерывания по приему и прерывания по отправке
    USART_BAUD(USART0)=(1<<4)|1;                          	//Битрейт 4800000
	USART_CTL0(USART0)|=1<<13;								//UART Enable    
	USART_CTL2(USART0)|=1<<7;								//DMA Enable
	NVIC_SetPriority(USART0_IRQn, 1);
	NVIC_EnableIRQ(USART0_IRQn);

	//Настраиваем АЦП
	RCU_APB2EN|=(1<<9);
	//Настройка АЦП0
	ADC_CTL1(ADC0)=(7<<17)|(1<<20);                         //программный запуск
	ADC_SAMPT1(ADC0)=1;             
	ADC_CTL1(ADC0)|=1;                                      //ADC0 Enable
	//Калибровка АЦП0
	ADC_CTL1(ADC0)|=1<<3;									//Сброс коэффициентов калибровки
	ADC_CTL1(ADC0)|=1<<2;                                   //Запуск калибровки
	while ((ADC_CTL1(ADC0)&4)!=0) ;                         //Ожидаем окончание калибровки

	RCU_APB1EN|=1;											//Настраиваем Timer1
	TIMER_PSC(TIMER1)=39;
	TIMER_CNT(TIMER1)=0;
	TIMER_CAR(TIMER1)=9999;                  		        //100Гц
	TIMER_CTL0(TIMER1)=1;
	TIMER_DMAINTEN(TIMER1)=1;
	NVIC_SetPriority(TIMER1_IRQn, 0); 
	NVIC_EnableIRQ(TIMER1_IRQn);      

	SendBuf[0][0]=18;
	SendBuf[1][0]=18;

	//WatchDog
	FWDGT_CTL=0x5555;
	FWDGT_PSC=1;											//40000/8=5000Гц
	FWDGT_RLD=2499;											//1c
	FWDGT_CTL=0xAAAA;
	FWDGT_CTL=0xCCCC;

	while (1)
	{ 
		if ((command[0]>=50)&&((state&1)!=0))	
		{
			state&=~1;
			ChangeState(state);										//Команда НИ
//			WriteFlash(state);
		}
		if ((command[1]>=50)&&((state&1)==0))	
		{
			state|=1;
			ChangeState(state);										//Команда БИ
//			WriteFlash(state);
		}
		if ((command[2]>=50)&&((state&2)==0))	
		{
			state|=2;
			ChangeState(state);										//Команда Вкл РПУ
//			WriteFlash(state);
		}
		if ((command[3]>=50)&&((state&2)!=0))	
		{
			state&=~2;
			ChangeState(state);										//Команда Выкл РПУ
//			WriteFlash(state);
		}

		if (Flag_100Hz==1)
		{
			Flag_100Hz=0;   
			ADC_RSQ2(ADC0)=0;
			for (i=0;i<100;i++);
			ADC_CTL1(ADC0)|=1<<22;									//Запуск преобразования АЦП
			while ((ADC_STAT(ADC0)&2)==0) ;
			U_PWR=(ADC_RDATA(ADC0)>>3)&0x1FF;

			ADC_RSQ2(ADC0)=1;
			for (i=0;i<100;i++);
			ADC_CTL1(ADC0)|=1<<22;															
			while ((ADC_STAT(ADC0)&2)==0) ;
			U_BI=(ADC_RDATA(ADC0)>>3)&0x1FF;

			ADC_RSQ2(ADC0)=2;
			for (i=0;i<100;i++);
			ADC_CTL1(ADC0)|=1<<22;															
			while ((ADC_STAT(ADC0)&2)==0) ;
			U_NI=(ADC_RDATA(ADC0)>>3)&0x1FF;

			SendBuf[SendBufWrite][2]=U_PWR;
			SendBuf[SendBufWrite][3]=U_PWR>>8;
			SendBuf[SendBufWrite][4]=U_NI;
			SendBuf[SendBufWrite][5]=U_NI>>8;
			SendBuf[SendBufWrite][6]=U_BI;
			SendBuf[SendBufWrite][7]=U_BI>>8;
		}
	}

}  

void TIMER1_IRQHandler(void)                 
{      
	int i;
	TIMER_INTF(TIMER1)&=~1;
	FWDGT_CTL=0xAAAA;

	if ((GPIO_ISTAT(GPIOA)&0x80)!=0) command[0]++;
		else command[0]=0;
	if ((GPIO_ISTAT(GPIOA)&0x10)!=0) command[1]++;
		else command[1]=0;
	if ((GPIO_ISTAT(GPIOA)&0x40)!=0) command[2]++;
		else command[2]=0;
	if ((GPIO_ISTAT(GPIOA)&0x8)!=0) command[3]++;
		else command[3]=0;  
} 

void USART0_IRQHandler(void)                 
{ 
	int i,j;
//GPIO_BOP(GPIOB)=1<<14;
	if ((USART_STAT0(USART0)&0x20)!=0)
	{
		Query[QueryCounter]=USART_DATA(USART0);
		if ((QueryCounter!=0)||((QueryCounter==0)&&(Query[QueryCounter]==18))) QueryCounter++;
		if (QueryCounter==4) 
		{
			if (Crc8(Query,3)==Query[3]) 
			{
				QueryCounter=0;

				GPIO_OCTL(GPIOB)=(GPIO_OCTL(GPIOB)&0xFFF)|((Query[2]&0xF)<<12);							//Временные команды

				SendBuf[SendBufWrite][1]=(state&2)<<1;
				if ((state&1)==0) SendBuf[SendBufWrite][1]=(SendBuf[SendBufWrite][1]&0xFE)|(1<<1);		//НИ
					else SendBuf[SendBufWrite][1]=(SendBuf[SendBufWrite][1]&0xFD)|1;					//БИ
				
				
				GPIO_BOP(GPIOC)=1<<9;									//DIR в 1

				//Отправка в УАРТ ответа через ДМА
				i=SendBufWrite;
				SendBufWrite=SendBufRead;
				SendBufRead=i;

				DMA_CH3CTL(DMA0)=(1<<4)|(1<<7)|(1<<12)|(1<<13);         //Память-периферия, изменение адреса памяти, приоритет ultra high
				DMA_CH3MADDR(DMA0)=(uint32_t)SendBuf[SendBufRead];
				DMA_CH3PADDR(DMA0)=(uint32_t)(&(USART_DATA(USART0)));
				DMA_CH3CNT(DMA0)=14;                                    //14 байт
				DMA_CH3CTL(DMA0)|=1;                            		//DMA Enable

				Flag_100Hz=1;
			}
			else																		//Если CRC не совпала, то ищем в других байтах адрес 18
			{
				for (i=1;i<4;i++)												
				{
					if (Query[i]==18)
					{
						for (j=0;j<(4-i);j++) Query[j]=Query[i+j];
						QueryCounter=4-i;
						break;
					}
				}
				if (i==4) QueryCounter=0;
			}
		}
	}

	if ((USART_STAT0(USART0)&0x40)!=0) 
	{
		GPIO_BC(GPIOC)=1<<9;						//Выключаем DIR если передача окончена
		USART_STAT0(USART0)&=~(1<<6);
	}
//GPIO_BC(GPIOB)=1<<14;
}