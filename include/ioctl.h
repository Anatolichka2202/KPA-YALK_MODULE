#ifndef __VXDAPI_IOCTL
#define __VXDAPI_IOCTL 1
// Board Type macro definitions


//#define LABVIEW_FW

#define NONE  0 // no board in slot
#define L1250 1 // L1250 board
#define N1250 2 // N1250 board (may be not work)
#define L1251 3 // L1251 board
#define L1221 4 // L1221 board
#define PCIA  5 // PCI rev A board
#define PCIB  6 // PCI rev B board
#define L264  8 // L264 ISA board
#define L305  9 // L305 ISA board
#define L1450C 10
#define L1450 11
#define L032 12
#define HI8 13
#define PCIC 14

#define LYNX2  15
#define TIGER2 16
#define TIGER3 17
#define LION   18

#define L791     19
#define LCPI     20 // pci lcpi

#define E440     30
#define E140     31
#define E2010    32
#define E270     33
#define CAN_USB  34
#define AK9      35
#define LTR010   36
#define LTR021   37
#define E154     38
#define E2010B   39
#define LTR031   40
#define LTR030   41

#define E310     77



#define CA01     90 // cardio

//defines for GetParemeter/SetParameter common for all boards
#define L_BOARD_TYPE       10000
#define L_POINT_SIZE       10001
#define L_SYNC_ADDR_LO     10002
#define L_SYNC_ADDR_HI     10003
#define L_DATA_ADDR_LO     10004
#define L_DATA_ADDR_HI     10005
#define L_SYNC1_ADDR_LO     10006
#define L_SYNC1_ADDR_HI     10007
#define L_DATA1_ADDR_LO    10008
#define L_DATA1_ADDR_HI    10009

#define L_USER_BASE        10100 // 128 user prpoperty to store data
// next prop 10228

// ERROR CODES
#define L_SUCCESS 0
#define L_NOTSUPPORTED 1
#define L_ERROR 2
#define L_ERROR_NOBOARD 3
#define L_ERROR_INUSE 4
#define L_ERROR_TIMEOUT 5


#define  SETBIT(Var, Bit) (Var |= (0x1L << Bit))
#define  CLEARBIT(Var, Bit) (Var &= ~(0x1L << Bit))  
#define  TOGGLEBIT(Var, Bit) (Var ^= (0x1L << Bit))  
#define  CHECKBIT(Var, Bit) (Var & (0x1L << Bit))
#define  CLEARVAR(Var) (Var = 0)

// ERROR BITS FOR _EX interface + 0x20000000 for actual code
#define L_STREAM_ADC_ERROR 0
#define L_STREAM_DAC_ERROR 1


// define s_Type for FillDAQparameters
#define L_ADC_PARAM 1
#define L_DAC_PARAM 2

#define L_ASYNC_ADC_CFG 3
#define L_ASYNC_TTL_CFG 4
#define L_ASYNC_DAC_CFG 5

#define L_ASYNC_ADC_INP 6
#define L_ASYNC_TTL_INP 7

#define L_ASYNC_TTL_OUT 8
#define L_ASYNC_DAC_OUT 9

#define L_ASYNC_FREQ_IN 10

#define L_DDS_FM_PARAM 11


#define L_STREAM_NULL 0
#define L_STREAM_ADC 1
#define L_STREAM_DAC 2
#define L_STREAM_TTLIN 3
#define L_STREAM_TTLOUT 4
#define L_STREAM_FMETER 5
#define L_STREAM_DDS 6

#define L_MEM_ALLOC  10 // смещение в массиве overlapped структур для overlapped аллокатора пямяти + stream_id

#define L_EVENT_ADC_BUF 1
#define L_EVENT_DAC_BUF 2

#define L_EVENT_ADC_OVF 3
#define L_EVENT_ADC_FIFO 4
#define L_EVENT_DAC_USER 5
#define L_EVENT_DAC_UNF 6
#define L_EVENT_PWR_OVR 7

#pragma pack(1)

// some math function
#define l_fabs(x) (((x)>=0) ? (x):(-x))
#define l_ceil(x) ((double)((int)(x)+1))


// used internal ////////////////
typedef struct _PORT_PARAM_
{
   ULONG port;
   ULONG datatype;
} PORT_PAR, *PPORT_PAR;
/////////////////////////////////

typedef struct __SLOT_PARAM
{
   ULONG Base;
   ULONG BaseL;
   ULONG Base1;
   ULONG BaseL1;
   ULONG Mem;
   ULONG MemL;
   ULONG Mem1;
   ULONG MemL1;
   ULONG Irq;
   ULONG BoardType;
   ULONG DSPType;
   ULONG Dma;
   ULONG DmaDac;
   ULONG      DTA_REG;
   ULONG      IDMA_REG;
   ULONG      CMD_REG;
   ULONG      IRQ_RST;
   ULONG      DTA_ARRAY;
   ULONG      RDY_REG;
   ULONG      CFG_REG;
} SLOT_PAR, *PSLOT_PAR;

#ifdef __cplusplus

   typedef struct _DAQ_PARAM_
   {
      ULONG s_Type;
      ULONG FIFO;
      ULONG IrqStep;
      ULONG Pages;
   } DAQ_PAR, *PDAQ_PAR;


   // descr async i/o routines for adc,dac & ttl
   typedef struct _ASYNC_PARAM_
    : public DAQ_PAR
   {
      double   dRate;
      ULONG Rate;
      ULONG NCh;
      ULONG Chn[128];
      ULONG Data[128];
      ULONG Mode;
   } ASYNC_PAR, *PASYNC_PAR;

   typedef struct _DAC_PARAM_U_0
    : public DAQ_PAR
   {
      ULONG AutoInit;
      double dRate;
      ULONG Rate;
      ULONG IrqEna;
      ULONG DacEna;
      ULONG DacNumber;
   } DAC_PAR_0, *PDAC_PAR_0;

   typedef struct _DAC_PARAM_U_1
    : public DAQ_PAR
   {
      ULONG AutoInit;
      double dRate;
      ULONG Rate;
      ULONG IrqEna;
      ULONG DacEna;
      ULONG Reserved1;
   } DAC_PAR_1, *PDAC_PAR_1;


   typedef struct _E310_TTL_PARAM_0
   {
      UCHAR  Mode;
      USHORT Param;
      USHORT inTTL;
      USHORT outTTL;
      UCHAR  Reserved[9];
   } E310_TTL_PARAM, *PE310_TTL_PARAM;

   typedef struct _E310_ADC_PARAM_0
   {
      UCHAR  StartSrc;
      UCHAR  ChMask;
      UCHAR  Reserved[5];
   } E310_ADC_PARAM, *PE310_ADC_PARAM;


   // hardware struct for dds and fm
   typedef struct _E310_HW_PARAM_0
   {
   // dds
      USHORT DDS_CtrlReg;     // AAAA DDDD 00D0 DD00 
      USHORT IncStepsReg;     // AAAA DDDD DDDD DDDD
      USHORT LoDeltaFreqReg;  // AAAA DDDD DDDD DDDD
      USHORT HiDeltaFreqReg;  // AAAA SDDD DDDD DDDD
      USHORT IncIntervalReg;  // AAAM MDDD DDDD DDDD
      USHORT LoStartFreqReg;  // AAAA DDDD DDDD DDDD
      USHORT HiStartFreqReg;  // AAAA DDDD DDDD DDDD
      USHORT LoStopFreqReg;   // AAAA DDDD DDDD DDDD
      USHORT HiStopFreqReg;   // AAAA DDDD DDDD DDDD
      UCHAR  AutoScanType; 
      double MasterClock;  // 8байт
      UCHAR  Reserved0[15];
      // control reg
      UCHAR  CtrlReg;         //       GG2 GG1 ODDD
      // dig potentiom
      USHORT OUT_10_OFFSET;   //0000 000A DDDD DDDD
      USHORT FM_POROG;        //0000 000A DDDD DDDD
      // freq meter
      UCHAR FM_Ena;
      UCHAR FM_Mode;
      UCHAR FM_InDiv;
      UCHAR FM_BCR_DivIdx;
      ULONG FM_ClockRate;
      ULONG FM_BCR;
      UCHAR Reserved1[21];
   } E310_HW_PARAM, *PE310_HW_PARAM;

   typedef struct _DAC_PARAM_U_2
    : public DAQ_PAR
   {
      double MasterClk; // если ext больше int то установить int и базовй
      USHORT MasterSrc; // ext int

      USHORT DDS_CtrlReg;     // AAAA DDDD 00D0 DD00 биты синхросигнала источник перывания цифровой выход меандр

      UCHAR  CyclicScan; // none циклический пила цикл треуг

      USHORT ScanSteps;     // 0 4095

      double ScanFreqInc; // AAAA DDDD DDDD DDDD

      USHORT ScanPeriodBase; // mclock or current freq
      USHORT ScanPeriodQnt;  // AAAM MDDD DDDD DDDD // 2 - 2047
      USHORT ScanPeriodMult;

      double StartFreq; // AAAA DDDD DDDD DDDD

      double StopFreq; // AAAA DDDD DDDD DDDD // return если треуг то она стартовой записывается

      // in ddr_ctrl_reg
      USHORT SyncOutType;
      USHORT SyncOutEna;

      UCHAR  TTLWaveEna;
      UCHAR  IRQLineType;

      USHORT ScanIncType; // auto ctrl_line
      UCHAR  ScanCtrlType; // int ext

      USHORT SignalForm; // sin tri
      // dig potentiom
      double Out10_offset; //0 - 255 +4 -4V
      UCHAR  Out10_offsrc;    // int ext

      UCHAR  OutGain;  // коды усиления

      double FM_POROG;      ////0 - 255 +4 -4V

      // freq meter
      UCHAR FM_Ena;
      UCHAR FM_Mode;
      UCHAR FM_InDiv;
      UCHAR FM_BCR_DivIdx;
      ULONG FM_ClockRate;
      ULONG FM_BCR;
      UCHAR Reserved1[21];

      E310_HW_PARAM dds_fm;
   } DAC_PAR_2, *PDAC_PAR_2;


   typedef union _DAC_PARAM_U_
   {
      DAC_PAR_0 t1;
      DAC_PAR_1 t2;
      DAC_PAR_2 t3;          // dds e310
   } DAC_PAR, *PDAC_PAR;




   typedef struct _ADC_PARAM_U_0
    : public DAQ_PAR
   {
      ULONG AutoInit;
      double dRate;
      double dKadr;
      double dScale;
      ULONG Rate;
      ULONG Kadr;
      ULONG Scale;
      ULONG FPDelay;
      ULONG SynchroType;
      ULONG SynchroSensitivity;
      ULONG SynchroMode;
      ULONG AdChannel;
      ULONG AdPorog;
      ULONG NCh;
      ULONG Chn[128];
      ULONG IrqEna;
      ULONG AdcEna;
   } ADC_PAR_0, *PADC_PAR_0;



   typedef struct _ADC_PARAM_U_1
    : public DAQ_PAR
   {
      ULONG AutoInit;
      double dRate;
      double dKadr;
      USHORT Reserved1;
      USHORT DigRate;  // divider for stream digital input(4 5 6 chan) 0 65535
      ULONG DM_Ena;    // data marker ena/dis
      ULONG Rate;
      ULONG Kadr;
      ULONG StartCnt;    // задержка сбора при старте в количестве кадров
      ULONG StopCnt;     // остановка сбора после количества кадров
      ULONG SynchroType;   // in e20-10 start type
      ULONG SynchroMode;    // advanced synchro mode + chan number
      ULONG AdPorog;         // порог синхронизации
      ULONG SynchroSrc;    // in e20-10 clock source
      ULONG AdcIMask;  // cange from Reserved4 to AdcIMask for e20-10 adc input config
      ULONG NCh;
      ULONG Chn[128];
      ULONG IrqEna;
      ULONG AdcEna;
   } ADC_PAR_1, *PADC_PAR_1;

   typedef union _ADC_PARAM_U_
   {
      ADC_PAR_0 t1;
      ADC_PAR_1 t2;
   } ADC_PAR, *PADC_PAR;

#endif

// wlcomp wrappper C defs /////////////////////////////////////////////////

typedef struct W_ASYNC_PARAM_
{
   ULONG s_Type;
   ULONG FIFO;
   ULONG IrqStep;
   ULONG Pages;

   double dRate;
   ULONG Rate;
   ULONG NCh;
   ULONG Chn[128];
   ULONG Data[128];
   ULONG Mode;
} WASYNC_PAR, *PWASYNC_PAR;

typedef struct W_DAC_PARAM_U_0
{
   ULONG s_Type;
   ULONG FIFO;
   ULONG IrqStep;
   ULONG Pages;

   ULONG AutoInit;

   double dRate;
   ULONG Rate;

   ULONG IrqEna;
   ULONG DacEna;
   ULONG DacNumber;
} WDAC_PAR_0, *PWDAC_PAR_0;

typedef struct W_DAC_PARAM_U_1
{
   ULONG s_Type;
   ULONG FIFO;
   ULONG IrqStep;
   ULONG Pages;

   ULONG AutoInit;

   double dRate;
   ULONG Rate;

   ULONG IrqEna;
   ULONG DacEna;
   ULONG Reserved1;
} WDAC_PAR_1, *PWDAC_PAR_1;


typedef struct W_ADC_PARAM_U_0
{
   ULONG s_Type;
   ULONG FIFO;
   ULONG IrqStep;
   ULONG Pages;

   ULONG AutoInit;

   double dRate;
   double dKadr;
   double dScale;
   ULONG Rate;
   ULONG Kadr;
   ULONG Scale;
   ULONG FPDelay;

   ULONG SynchroType;
   ULONG SynchroSensitivity;
   ULONG SynchroMode;
   ULONG AdChannel;
   ULONG AdPorog;
   ULONG NCh;
   ULONG Chn[128];
   ULONG IrqEna;
   ULONG AdcEna;
} WADC_PAR_0, *PWADC_PAR_0;



typedef struct W_ADC_PARAM_U_1
{
   ULONG s_Type;
   ULONG FIFO;
   ULONG IrqStep;
   ULONG Pages;

   ULONG AutoInit;

   double dRate;
   double dKadr;
   USHORT Reserved1;
   USHORT DigRate;   // dig line input rate divider
   ULONG  DM_Ena;    // data marker ena/dis

   ULONG Rate;
   ULONG Kadr;
   ULONG StartCnt;    // задержка сбора при старте в количестве кадров
   ULONG StopCnt;     // остановка сбора после количества кадров

   ULONG SynchroType;
   ULONG SynchroMode;    // advanced synchro mode + chan number
   ULONG AdPorog;         
   ULONG SynchroSrc;
   ULONG AdcIMask;

   ULONG NCh;
   ULONG Chn[128];
   ULONG IrqEna;
   ULONG AdcEna;
} WADC_PAR_1, *PWADC_PAR_1;


typedef struct __USHORT_IMAGE
{
   USHORT data[512];
} USHORT_IMAGE, *PUSHORT_IMAGE;

typedef union W_DAQ_PARAM_
{
   WDAC_PAR_0 t1;
   WDAC_PAR_1 t2;
   WADC_PAR_0 t3;
   WADC_PAR_1 t4;
   USHORT_IMAGE wi;
} WDAQ_PAR, *PWDAQ_PAR;

////////////////////////////////////////////////////////////////////////


//exported
typedef struct __PLATA_DESCR
{
   char      SerNum[9];
   char      BrdName[5];
   char      Rev;
   char      DspType[5];
   ULONG  Quartz;
   USHORT  IsDacPresent;
   USHORT  Reserv1[7];
   USHORT  KoefADC[8];
   USHORT  KoefDAC[4];
   USHORT  Custom[32];
} PLATA_DESCR, *PPLATA_DESCR;

//exported
typedef struct __PLATA_DESCR_1450
{
   char      SerNum[9];
   char      BrdName[7];
   char      Rev;
   char      DspType[5];
   char      IsDacPresent;
   char      IsExtMemPresent;
   ULONG  Quartz;
   USHORT  Reserv1[6];
   USHORT  KoefADC[8];
   USHORT  KoefDAC[4];
   USHORT  Custom[32];
} PLATA_DESCR_1450, *PPLATA_DESCR_1450;


typedef struct __PLATA_DESCR_L791
{
   USHORT CRC16;
   char     SerNum[16];            
   char     BrdName[16];          
   char     Rev;                  
   char     DspType[5];          
   ULONG Quartz;               
   USHORT IsDacPresent;       
   float    KoefADC[16];          
   float    KoefDAC[4];         
   USHORT Custom;
} PLATA_DESCR_L791, *PPLATA_DESCR_L791;

typedef struct __PLATA_DESCR_E440
{
   char     SerNum[9];
   char     BrdName[7];
   char     Rev;
   char     DspType[5];
   char     IsDacPresent;
   ULONG Quartz;
   char     Reserv2[13];
   USHORT KoefADC[8];
   USHORT KoefDAC[4];
   USHORT Custom[32];
} PLATA_DESCR_E440, *PPLATA_DESCR_E440;

typedef struct __PLATA_DESCR_E140
{
   char     SerNum[9];
   char     BrdName[11];
   char     Rev;
   char     DspType[11];
   char     IsDacPresent;
   ULONG Quartz;
   char     Reserv2[3];
   float    KoefADC[8]; // 4 offs 4 scale
   float    KoefDAC[4];  // 2 off 2 scale
   USHORT Custom[20];
} PLATA_DESCR_E140, *PPLATA_DESCR_E140;

typedef struct __PACKED_PLATA_DESCR_E140
{
   UCHAR   SerNum1; //0-9
   char      SerNum2; // L,C
   ULONG  SerNum3; // serial long
   char      Name[10];
   char      Rev;
   char      DspType[10];
   ULONG  Quartz;
   UCHAR   CRC1; // from 0 to 30 (31)
   /////////////
   UCHAR   IsDacPresent;
   float     AdcOffs[4];
   float     AdcScale[4];
   float     DacOffs[2];
   float     DacScale[2];
   UCHAR   Reserv[46];
   UCHAR   CRC2; // from 32 to end (95)
} PACKED_PLATA_DESCR_E140, *PPACKED_PLATA_DESCR_E140;

typedef struct __PLATA_DESCR_E154
{
   char      SerNum[9];
   char      BrdName[11];
   char      Rev;
   char      DspType[11];
   char      IsDacPresent;
   ULONG  Quartz;
   char      Reserv2[3];
   float     KoefADC[8]; // 4 offs 4 scale
   float     KoefDAC[4];  // 2 off 2 scale
   USHORT  Custom[20];
} PLATA_DESCR_E154, *PPLATA_DESCR_E154;

typedef struct __PACKED_PLATA_DESCR_E154
{
   UCHAR   SerNum1; //0-9
   char      SerNum2; // L,C
   ULONG  SerNum3; // serial long
   char      Name[10];
   char      Rev;
   char      DspType[10];
   ULONG  Quartz;
   UCHAR   CRC1; // from 0 to 30 (31)
   /////////////
   UCHAR   IsDacPresent;
   float     AdcOffs[4];
   float     AdcScale[4];
   float     DacOffs[2];
   float     DacScale[2];
   UCHAR   Reserv[46];
   UCHAR   CRC2; // from 32 to end (95)
} PACKED_PLATA_DESCR_E154, *PPACKED_PLATA_DESCR_E154;

typedef struct __WORD_IMAGE
{
   USHORT data[64];
} WORD_IMAGE, *PWORD_IMAGE;

typedef struct __BYTE_IMAGE
{
   UCHAR  data[128];
} BYTE_IMAGE, *PBYTE_IMAGE;

typedef union __PLATA_DESCR_U
{
   PLATA_DESCR             t1;
   PLATA_DESCR_1450        t2;
   PLATA_DESCR_L791        t3;
   PLATA_DESCR_E440        t4;
   PLATA_DESCR_E140        t5;
   PACKED_PLATA_DESCR_E140 pt5;
   
   WORD_IMAGE wi;
   BYTE_IMAGE bi;
} PLATA_DESCR_U, *PPLATA_DESCR_U;

// введены тк у платы 2010 флеш 256 байт и никак его не втиснуть в 128
// соответсвенно объедененный образ увеличен до 256
// size - 256 byte
typedef struct __PLATA_DESCR_E2010
{
   char     BrdName[16];
   char     SerNum[16];
   char     DspType[16];
   ULONG  Quartz;
   char     Rev;
   char     IsDacPresent;
   float    KoefADC[24]; // 12 offs 12 scale
   float    KoefDAC[4];  // 2 off 2 scale
   USHORT Custom[44];
   USHORT CRC;
} PLATA_DESCR_E2010, *PPLATA_DESCR_E2010;


typedef struct __PLATA_DESCR_E310
{
   char     BrdName[16];          // название модуля
   char     SerNum[16];  // серийный номер модуля
   char     DspType[25];                  // название установленного MCU
   char     Rev;                                           // ревизия платы
   ULONG Quartz;                                         // тактовая частота MCU в Гц
   char     Reserved[192];                                  // зарезервировано
   USHORT CRC;                                              // контрольная сумма структуры
}  PLATA_DESCR_E310, *PPLATA_DESCR_E310;;

typedef struct __WORD_IMAGE_256
{
   USHORT data[128];
} WORD_IMAGE_256, *PWORD_IMAGE_256;

typedef struct __BYTE_IMAGE_256
{
   UCHAR data[256];
} BYTE_IMAGE_256, *PBYTE_IMAGE_256;

typedef union __PLATA_DESCR_U2
{
   PLATA_DESCR             t1;
   PLATA_DESCR_1450        t2;
   PLATA_DESCR_L791        t3;
   PLATA_DESCR_E440        t4;
   PLATA_DESCR_E140        t5;
   PACKED_PLATA_DESCR_E140 pt5;
   PLATA_DESCR_E2010       t6;
   PLATA_DESCR_E154        t7;
   PACKED_PLATA_DESCR_E154 pt7;
   PLATA_DESCR_E310        t8;

   WORD_IMAGE              wi;
   BYTE_IMAGE              bi;
   WORD_IMAGE_256          wi256;
   BYTE_IMAGE_256          bi256;    
} PLATA_DESCR_U2, *PPLATA_DESCR_U2;


//  used internally in driver for e140 //////////////////
#define MAKE_E140CHAN(w) ((w&0x3)<<2)|((w&0xC)>>2)|(w&0xF0)

typedef struct _ADC_PARAM_E140_PACK                   //
{                                                     //
   UCHAR  Chn[128];                                 //
                                                      //
   USHORT Rate;                                     //
   UCHAR  NCh;                                      //
   UCHAR  Kadr;                                     //
                                                      //
   UCHAR  SynchroType;                              //
   UCHAR  AdChannel;                                //
   USHORT AdPorog;                                  //
} ADC_PAR_E140_PACK, *PADC_PAR_E140_PACK;             //
                                                      //
typedef struct __BYTE_IMAGE_E140                      //
{                                                     //
   UCHAR data[136];                                   //
} BYTE_IMAGE_E140, *PBYTE_IMAGE_E140;                 //
                                                      //
typedef union __ADC_PARAM_E140_PACK                   //
{                                                     //
   ADC_PAR_E140_PACK t1;                              //
   BYTE_IMAGE_E140 bi;                                //
} ADC_PAR_E140_PACK_U, *PADC_PAR_E140_PACK_U;         //
                                                      //

typedef struct _DAC_PARAM_E140_PACK                   //
{                                                     //
   USHORT DacDiv;                                     //
   UCHAR  SyncWithADC;                                 //
   UCHAR  ZeroOnStop;                                  //
   UCHAR  Reserved[12];                                //
} DAC_PAR_E140_PACK, *PDAC_PAR_E140_PACK;             //

typedef struct __BYTE_IMAGE_E140_16                   //
{                                                     //
   UCHAR data[16];                                    //
} BYTE_IMAGE_E140_16, *PBYTE_IMAGE_E140_16;           //
                                                      //
typedef union __DAC_PARAM_E140_PACK                   //
{                                                     //
   DAC_PAR_E140_PACK t1;                              //
   BYTE_IMAGE_E140_16 bi;                             //
} DAC_PAR_E140_PACK_U, *PDAC_PAR_E140_PACK_U;         //


////////////////////////////////////////////////////////


//  used internally in driver for e154 //////////////////
typedef struct _ADC_PARAM_E154_PACK                   //
{                                                     //
   UCHAR  Chn[16];                                  //
                                                      //
   USHORT Rate;                                     //
   UCHAR  NCh;                                      //
   UCHAR  Kadr;                                     //
                                                      //
   UCHAR  SynchroType;                              //
   UCHAR  AdChannel;                                //
   USHORT AdPorog;                                  //
   UCHAR  Scale;                                    //
   UCHAR  Kadr1;                                    //
} ADC_PAR_E154_PACK, *PADC_PAR_E154_PACK;             //
                                                      //
typedef struct __BYTE_IMAGE_E154                      //
{                                                     //
   UCHAR data[26];                                  //
} BYTE_IMAGE_E154, *PBYTE_IMAGE_E154;                 //
                                                      //
typedef union __ADC_PARAM_E154_PACK                   //
{                                                     //
   ADC_PAR_E154_PACK t1;                              //
   BYTE_IMAGE_E154 bi;                                //
} ADC_PAR_E154_PACK_U, *PADC_PAR_E154_PACK_U;         //
                                                      //
////////////////////////////////////////////////////////


//  used internaly in driver for e2010 ///////////////////
typedef struct _ADC_PARAM_E2010_PACK                    //
{                                                       //
   UCHAR    SyncMode;                                 //
   UCHAR    Rate;                                     //
   USHORT   Kadr;                                     //
   USHORT   ChanMode;                                 //
   UCHAR    NCh;                                      //
   UCHAR    Chn[256];                                 //
} ADC_PAR_E2010_PACK, *PADC_PAR_E2010_PACK;             //
                                                        //
typedef struct __BYTE_IMAGE_E2010                       //
{                                                       //
   UCHAR data[256+7];                                 //
} BYTE_IMAGE_E2010, *PBYTE_IMAGE_E2010;                 //
                                                        //
typedef union __ADC_PARAM_E2010_PACK                    //
{                                                       //
   ADC_PAR_E2010_PACK t1;                               //
   BYTE_IMAGE_E2010 bi;                                 //
} ADC_PAR_E2010_PACK_U, *PADC_PAR_E2010_PACK_U;         //
                                                        //
typedef struct _ADC_PAR_EXTRA_E2010_PACK                //
{                                                       //
   ULONG  StartCnt;                                  //
   ULONG  StopCnt;                                   //
   USHORT  SynchroMode;                               //
   USHORT  AdPorog;                                   //
   UCHAR   DM_Ena;                                    //
} ADC_PAR_EXTRA_E2010_PACK, *PADC_PAR_EXTRA_E2010_PACK; //
                                                        //
typedef struct __BYTE_IMAGE_E2010_13                    //
{                                                       //
   UCHAR data[13];                                    //
} BYTE_IMAGE_E2010_13, *PBYTE_IMAGE_E2010_13;           //
                                                        //
typedef union __ADC_PAR_EXTRA_E2010_PACK                //
{                                                       //
   ADC_PAR_EXTRA_E2010_PACK t1;                         //
   BYTE_IMAGE_E2010_13 bi;                              //
} ADC_PAR_EXTRA_E2010_PACK_U, *PADC_PAR_EXTRA_E2010_PACK_U; //
                                                        //
////////////////////////////////////////////////////////

// ioctl struct for ioctl access...
typedef struct __IOCTL_BUFFER
{
   int inSize; // size in bytes
   int outSize; // size in bytes 
   UCHAR inBuffer[4096];
   UCHAR outBuffer[4096];   
} IOCTL_BUFFER, *PIOCTL_BUFFER;

#pragma pack()

#ifndef _LDRIVER

const int DIOC_SETUP                         = 1;
const int DIOC_SETEVENT                      = 2;
const int DIOC_START                         = 3;
const int DIOC_STOP                          = 4;

const int DIOC_OUTP                          = 5;

const int DIOC_INP                           = 6;

const int DIOC_OUTM                          = 7;

const int DIOC_INM = 8;

const int DIOC_SETBUFFER = 9;

const int DIOC_ADD_BOARDS = 10;

const int DIOC_CLEAR_BOARDS = 11;

const int DIOC_INIT_SYNC = 12;
/*
const int DIOC_SETBUFFER_DAC = 13;
*/

const int DIOC_SETEVENT_DAC = 14;

const int DIOC_SEND_COMMAND = 15;

const int DIOC_COMMAND_PLX = 16;

const int DIOC_PUT_DATA_A = 17;

const int DIOC_GET_DATA_A = 18;

const int DIOC_PUT_DM_A = 19;

const int DIOC_GET_DM_A = 20;

const int DIOC_PUT_PM_A = 21;

const int DIOC_GET_PM_A = 22;

const int DIOC_GET_PARAMS = 23;

const int DIOC_SET_DSP_TYPE = 24;

const int DIOC_SETBUFFER_1 = 25;

const int DIOC_SETUP_DAC = 26;

const int DIOC_READ_FLASH_WORD = 27;

const int DIOC_WRITE_FLASH_WORD = 28;

const int DIOC_ENABLE_FLASH_WRITE = 29;

const int DIOC_SETEVENT_1 = 30;

const int DIOC_SETEVENT_2 = 31;

const int DIOC_SETEVENT_3 = 32;

const int DIOC_SETEVENT_4 = 33;

const int DIOC_SETEVENT_5 = 34;

const int DIOC_ADCSAMPLE = 35;

const int DIOC_LOAD_BIOS = 36;

const int DIOC_TTL_IN = 37;

const int DIOC_TTL_OUT = 38;

const int DIOC_TTL_CFG = 39;

const int DIOC_DAC_OUT = 40;

const int DIOC_RESET_PLX = 41;

const int DIOC_SEND_BIOS =  44;

const int DIOC_ENABLE_CORRECTION = 50;

const int DIOC_START_DAC = 51;

/*
const int DIOC_STOP_DAC = 52;
const int  DIOC_INIT_SYNC_ADC = 53;
const int  DIOC_INIT_SYNC_DAC = 54;
*/     

const int DIOC_ADC_CFG = 55;

const int DIOC_DAC_CFG = 56;

const int DIOC_SETUP_DDS = 57;


// api from ldevusb
/*
const int DIOC_START = 0x101;

const int DIOC_STOP = 0x102;

const int DIOC_LOAD_BIOS_USB = 0x103;

const int DIOC_SEND_COMMAND = 0x104;
*/

const int DIOC_RESET_PIPE1 = 0x105;

const int DIOC_RESET_PIPE3 = 0x106;

const int DIOC_ABORT_PIPE1 = 0x107;

const int DIOC_ABORT_PIPE3 = 0x108;

#endif

#endif
// следующий 51 &54
