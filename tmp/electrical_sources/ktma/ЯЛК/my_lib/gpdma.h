#ifndef __GPDMA_H_
#define __GPDMA_H_
#include "peripheral.h"
#include "type.h"
/*
Вобще устройство то одно и надо бы еще подумать как его оформить, 
но я сделал виртуально два канала,как два разных устройства, 
соотвественно они доступны через GPDMA::Get(0/1);

*/

//опции ДМА
struct GPDMAOption{
	byte endian_mode; 		//0 -little 1 - big
	byte DWidth;		    
	byte SWidth;
	byte DBSize;
	byte SBSize;
	byte SI;				//Source increment
	byte DI;			    //Destanation Increment
	byte Prot;			    
	byte IntEnable;
	byte SrcP;
	byte DestP;
	byte FlowCntl;
	byte IE;
	byte ITC;
	byte L;
	byte H;	
};	
enum DMAChannelName{
	CHAN_0=(uint)0xFFE04100,
	CHAN_1=(uint)0xFFE04120,
};

class GPDMA:public Peripheral {
	private:
	enum{
	   IntStatus,
	   IntTCStatus,
	   IntClear,
	   IntErrorStatus,
	   IntErrClr,
	   RawIntTCStatus,
	   RawIntErrorStatus,
	   EnbldChns,
	   SoftBReq,
	   SoftSReq,
	   SoftLBreq,
	   SoftLSReq,
	   Configuration,
	   Sync
	};

	enum{
		CH_SrcAddr,
		CH_DestAddr,
		CH_LLI,
		CH_Control,
		CH_Configuration
	};
	ulong* chan;   //канал
	uint chan_mask; 
	static GPDMA* DEV[2]; //
	public:
		enum FlowControl{
			M2M_DMA,   //Memory to memory. DMA
			M2P_DMA,   //Memory to peripheral. DMA
			P2M_DMA,   //Peripheral to memory. DMA
			P2P_DMA,   //Source peripheral to destination peripheral. DMA
			P2P_DP,	   //Source peripheral to destination peripheral. Destination peripheral.
			M2P_P,	   //Memory to peripheral. Peripheral.
			P2M_P,	   //Peripheral to memory. Peripheral.
			P2P_SP,	   //Source peripheral to destination peripheral. Source peripheral.
		};
		enum SRCDESTPeripheral{
			SSP0_TX,
			SSP0_RX,
			SSP1_TX,
			SSP1_RX,
			SD_MMC,
			I2S_CH0,
			I2S_CH1	
		};
		//настройки BurtsSize
		enum BurstSize{
			BS_1=0,
			BS_4=1,
			BS_8=2,
			BS_16=3,
			BS_32=4,
			BS_64=5
		};
		enum TransferWidth{
			BYTE_8,
			HALFWORLD_16,
			WORD_32
		}; 
		//статус прерываний
		enum{

		};
		GPDMA(DMAChannelName name);
		~GPDMA();
		void Init(const GPDMAOption* opt);
		void SetSrc(void* addr);
		void SetDest(void* addr);
		void Start(const uint tsize=0);
		void Stop();
		uint GetIntStatus();
		void ClearInt();
		
		static void Register(GPDMA* const obj,const uint num);
		static GPDMA* Get(uint num);
};

#define DMA_CHAN_SIZE 2
#define PREPARE_GPDMA GPDMA gpdma_dev_chan0(CHAN_0),gpdma_dev_chan1(CHAN_1);\
GPDMA::Register(&gpdma_dev_chan0,0);GPDMA::Register(&gpdma_dev_chan1,1);

#endif 
