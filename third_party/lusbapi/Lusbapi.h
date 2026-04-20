#ifndef __LusbapiH__
#define __LusbapiH__

	// --------------------------------------------------------------------------
	// ---------------------------- COMMON PART ---------------------------------
	// --------------------------------------------------------------------------
	#include <windows.h>
	#include "LusbapiTypes.h"

	// версия библиотеки
	#define 	VERSION_MAJOR_LUSBAPI 			(0x3)		// только одна цифра
	#define 	VERSION_MINOR_LUSBAPI 			(0x4)		// только одна цифра
	#define 	CURRENT_VERSION_LUSBAPI			((VERSION_MAJOR_LUSBAPI << 0x10) | VERSION_MINOR_LUSBAPI)

	#define InitLDevice(Slot) OpenLDevice(Slot)

	// экспортирукемые функции
	extern "C" DWORD WINAPI GetDllVersion(void);
	extern "C" LPVOID WINAPI CreateLInstance(const char * const DeviceName);

	// возможные индексы скорости работы модуля на шине USB
	enum { USB11_LUSBAPI, USB20_LUSBAPI, INVALID_USB_SPEED_LUSBAPI };
	// полное отсутствме каких-либо модификиций модуля
	enum { NO_MODULE_MODIFICATION_LUSBAPI = -1 };
	// максимально возможное кол-во опрашиваемых виртуальных слотов
	const WORD MAX_VIRTUAL_SLOTS_QUANTITY_LUSBAPI = 127;


	// ==========================================================================
	// *************************** L-Card USB BASE ******************************
	// ==========================================================================
	struct ILUSBBASE
	{
		// функции общего назначения для работы с USB устройствами
		virtual BOOL WINAPI OpenLDevice(WORD VirtualSlot) = 0;
		virtual BOOL WINAPI CloseLDevice(void) = 0;
		virtual BOOL WINAPI ReleaseLInstance(void) = 0;
		// получение дескриптора устройства USB
		virtual HANDLE WINAPI GetModuleHandle(void) = 0;
		// получение названия используемого модуля
		virtual BOOL WINAPI GetModuleName(char * const ModuleName) = 0;
		// получение текущей скорости работы шины USB
		virtual BOOL WINAPI GetUsbSpeed(BYTE * const UsbSpeed) = 0;
		// управления режимом низкого электропотребления модуля
		virtual BOOL WINAPI LowPowerMode(BOOL LowPowerFlag) = 0;
		// функция выдачи строки с последней ошибкой
		virtual BOOL WINAPI GetLastErrorInfo(LAST_ERROR_INFO_LUSBAPI * const LastErrorInfo) = 0;
	};




	// ==========================================================================
	// *************************** Модуль E14-140 *******************************
	// ==========================================================================
	// доступные индексы диапазонов входного напряжения модуля E14-140
	enum {	ADC_INPUT_RANGE_10000mV_E140, ADC_INPUT_RANGE_2500mV_E140, ADC_INPUT_RANGE_625mV_E140, ADC_INPUT_RANGE_156mV_E140, INVALID_ADC_INPUT_RANGE_E140 };
	// доступные индексы источника тактовых импульсов для АЦП
	enum {	INT_ADC_CLOCK_E140, EXT_ADC_CLOCK_E140, INVALID_ADC_CLOCK_E140 };
	// доступные индексы управления трансляцией тактовых импульсов АЦП
	// на линию SYN внешнего цифрового разъёма (только при внутреннем
	// источнике тактовых импульсоц АЦП)
	enum {	ADC_CLOCK_TRANS_DISABLED_E140, ADC_CLOCK_TRANS_ENABLED_E140, INVALID_ADC_CLOCK_TRANS_E140 };
	// возможные типы синхронизации модуля E14-140
	enum { 	NO_SYNC_E140, TTL_START_SYNC_E140, TTL_KADR_SYNC_E140, ANALOG_SYNC_E140, INVALID_SYNC_E140 };
	// возможные опции наличия микросхемы ЦАП
	enum {	DAC_INACCESSIBLED_E140, DAC_ACCESSIBLED_E140, INVALID_DAC_OPTION_E140 };
	// доступные индексы ревизий модуля E14-140
	enum {	REVISION_A_E140, REVISION_B_E140, INVALID_REVISION_E140 };
	// доступные индексы синхронизации потоковой работы ЦАП и АЦП
	enum {	DIS_ADC_DAC_SYNC_E140, ENA_ADC_DAC_SYNC_E140, INVALID_ADC_DAC_SYNC_E140 };
	// доступные индексы режимов остановки потокового ЦАП
	enum {	NORMAL_DAC_ON_STOP_E140, ZERO_DAC_ON_STOP_E140, INVALID_DAC_ON_STOP_E140 };

	// константы для работы с модулем
	enum 	{
				MAX_CONTROL_TABLE_LENGTH_E140 = 128,
				ADC_INPUT_RANGES_QUANTITY_E140 = INVALID_ADC_INPUT_RANGE_E140,
				ADC_CALIBR_COEFS_QUANTITY_E140 = ADC_INPUT_RANGES_QUANTITY_E140,
				DAC_CHANNELS_QUANTITY_E140 = 0x2, DAC_CALIBR_COEFS_QUANTITY_E140 = DAC_CHANNELS_QUANTITY_E140,
				TTL_LINES_QUANTITY_E140 = 0x10,	  		// кол-во цифровых линий
				USER_FLASH_SIZE_E140 = 0x200,   			// размер области пользовательского ППЗУ в байтах
				REVISIONS_QUANTITY_E140 = INVALID_REVISION_E140,		// кол-во ревизий (модификаций) модуля
			};
	// диапазоны входного напряжения АЦП в В
	const double ADC_INPUT_RANGES_E140[ADC_INPUT_RANGES_QUANTITY_E140] =
	{
		10.0, 10.0/4.0, 10.0/16.0, 10.0/64.0
	};
	// диапазоны выходного напряжения ЦАП в В
	const double DAC_OUTPUT_RANGE_E140 = 5.0;
	// доступные ревизии модуля
	const BYTE REVISIONS_E140[REVISIONS_QUANTITY_E140] = { 'A', 'B' };

	#pragma pack(1)
	// структура с информацией об модуле E14-140
	struct MODULE_DESCRIPTION_E140
	{
		MODULE_INFO_LUSBAPI     Module;		// общая информация о модуле
		INTERFACE_INFO_LUSBAPI  Interface;	// информация об используемом интерфейсе
		MCU_INFO_LUSBAPI<VERSION_INFO_LUSBAPI>		Mcu;	// информация о микроконтроллере
		ADC_INFO_LUSBAPI        Adc;			// информация о АЦП
		DAC_INFO_LUSBAPI        Dac;			// информация о ЦАП
		DIGITAL_IO_INFO_LUSBAPI DigitalIo;	// информация о цифровом вводе-выводе
	};
	// структура пользовательского ППЗУ модуля E14-140
	struct USER_FLASH_E140
	{
		BYTE Buffer[USER_FLASH_SIZE_E140];
	};
	// структура, задающая режим работы АЦП для модуля E14-140
	struct ADC_PARS_E140
	{
		WORD ClkSource;							// источник тактовых импульсов для запуска АПП
		WORD EnableClkOutput;					// разрешение трансляции тактовых импульсов запуска АЦП
		WORD InputMode;							// режим ввода даных с АЦП
		WORD SynchroAdType;						// тип аналоговой синхронизации
		WORD SynchroAdMode; 						// режим аналоговой сихронизации
		WORD SynchroAdChannel;  				// канал АЦП при аналоговой синхронизации
		SHORT SynchroAdPorog; 					// порог срабатывания АЦП при аналоговой синхронизации
		WORD ChannelsQuantity;					// число активных каналов
		WORD ControlTable[128];					// управляющая таблица с активными каналами
		double AdcRate;							// частота работы АЦП в кГц
		double InterKadrDelay;					// межкадровая задержка в мс
		double KadrRate;							// частота кадра в кГц
	};
	// структура, задающая режим потоковой работы ЦАП для модуля E14-140
	struct DAC_PARS_E140
	{
		BYTE SyncWithADC;							// 0 = обычный пуск ЦАП; !0 = синхронизировать с пуском АЦП
		BYTE SetZeroOnStop;						// !0 = при остановке потокового вывода установить на выходе ЦАП 0 В
		double DacRate;							// частота работы ЦАП в кГц
	};
	#pragma pack()

	//-----------------------------------------------------------------------------
	// интерфейс для модуля E14-140
	//-----------------------------------------------------------------------------
	struct ILE140 : public ILUSBBASE
	{
		// функции для работы с АЦП
		virtual BOOL WINAPI GET_ADC_PARS(ADC_PARS_E140 * const AdcPars) = 0;
		virtual BOOL WINAPI SET_ADC_PARS(ADC_PARS_E140 * const AdcPars) = 0;
		virtual BOOL WINAPI START_ADC(void) = 0;
		virtual BOOL WINAPI STOP_ADC(void) = 0;
		virtual BOOL WINAPI ADC_KADR(SHORT * const Data) = 0;
		virtual BOOL WINAPI ADC_SAMPLE(SHORT * const AdcData, WORD AdcChannel) = 0;
		virtual BOOL WINAPI ReadData(IO_REQUEST_LUSBAPI * const ReadRequest) = 0;

		// функции для работы с ЦАП
		virtual BOOL WINAPI GET_DAC_PARS(DAC_PARS_E140 * const DacPars) = 0;
		virtual BOOL WINAPI SET_DAC_PARS(DAC_PARS_E140 * const DacPars) = 0;
		virtual BOOL WINAPI START_DAC(void) = 0;
		virtual BOOL WINAPI STOP_DAC(void) = 0;
		virtual BOOL WINAPI WriteData(IO_REQUEST_LUSBAPI * const WriteRequest) = 0;
		virtual BOOL WINAPI DAC_SAMPLE(SHORT * const DacData, WORD DacChannel) = 0;
		virtual BOOL WINAPI DAC_SAMPLES(SHORT * const DacData1, SHORT * const DacData2) = 0;

		// функции для работы с ТТЛ линиями
		virtual BOOL WINAPI ENABLE_TTL_OUT(BOOL EnableTtlOut) = 0;
		virtual BOOL WINAPI TTL_IN(WORD * const TtlIn) = 0;
		virtual BOOL WINAPI TTL_OUT(WORD TtlOut) = 0;

		// функции для работы с пользовательской информацией ППЗУ
		virtual BOOL WINAPI ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled) = 0;
		virtual BOOL WINAPI READ_FLASH_ARRAY(USER_FLASH_E140 * const UserFlash) = 0;
		virtual BOOL WINAPI WRITE_FLASH_ARRAY(USER_FLASH_E140 * const UserFlash) = 0;

		// функции для работы со служебной информацией ППЗУ
		virtual BOOL WINAPI GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E140 * const ModuleDescription) = 0;
		virtual BOOL WINAPI SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E140 * const ModuleDescription) = 0;

		// функции для прямого досупа к микроконтроллеру
		virtual BOOL WINAPI GetArray(BYTE * const Buffer, WORD Size, WORD Address) = 0;
		virtual BOOL WINAPI PutArray(BYTE * const Buffer, WORD Size, WORD Address) = 0;
	};





	// ==========================================================================
	// *************************** Модуль E-154 *******************************
	// ==========================================================================
	// доступные индексы диапазонов входного напряжения модуля E-154
	enum {	ADC_INPUT_RANGE_5000mV_E154, ADC_INPUT_RANGE_1600mV_E154, ADC_INPUT_RANGE_500mV_E154, ADC_INPUT_RANGE_160mV_E154, INVALID_ADC_INPUT_RANGE_E154 };
	// доступные индексы источника тактовых импульсов для АЦП, сохранены для совместимости с E14-140
	enum {	INT_ADC_CLOCK_E154, EXT_ADC_CLOCK_E154, INVALID_ADC_CLOCK_E154 };
	// доступные индексы управления трансляцией тактовых импульсов АЦП
	// на линию SYN внешнего цифрового разъёма (только при внутреннем
	// источнике тактовых импульсоц АЦП),   сохранены для совместимости с E14-140
	enum {	ADC_CLOCK_TRANS_DISABLED_E154, ADC_CLOCK_TRANS_ENABLED_E154, INVALID_ADC_CLOCK_TRANS_E154 };
	// возможные типы синхронизации модуля E-154
	enum { 	NO_SYNC_E154, TTL_START_SYNC_E154, TTL_KADR_SYNC_E154, ANALOG_SYNC_E154, INVALID_SYNC_E154 };
	// возможные опции наличия микросхемы ЦАП
	enum {	DAC_INACCESSIBLED_E154, DAC_ACCESSIBLED_E154, INVALID_DAC_OPTION_E154 };
	// доступные индексы ревизий модуля E-154
	enum {	REVISION_A_E154, INVALID_REVISION_E154 };

	// константы для работы с модулем
	enum 	{
				MAX_CONTROL_TABLE_LENGTH_E154 = 16,
				ADC_INPUT_RANGES_QUANTITY_E154 = INVALID_ADC_INPUT_RANGE_E154,
				ADC_CALIBR_COEFS_QUANTITY_E154 = ADC_INPUT_RANGES_QUANTITY_E154,
				DAC_CHANNELS_QUANTITY_E154 = 0x1, DAC_CALIBR_COEFS_QUANTITY_E154 = DAC_CHANNELS_QUANTITY_E154,
				TTL_LINES_QUANTITY_E154 = 0x08,	  		// кол-во цифровых линий
				USER_FLASH_SIZE_E154 = 0x80,   			// размер области пользовательского ППЗУ в байтах
				REVISIONS_QUANTITY_E154 = INVALID_REVISION_E154,		// кол-во ревизий (модификаций) модуля
			};
	// диапазоны входного напряжения АЦП в В
	const double ADC_INPUT_RANGES_E154[ADC_INPUT_RANGES_QUANTITY_E154] =
	{
		5.0, 1.6, 0.5, 0.16
	};
	// диапазоны выходного напряжения ЦАП в В
	const double DAC_OUTPUT_RANGE_E154 = 5.0;
	// доступные ревизии модуля
	const BYTE REVISIONS_E154[REVISIONS_QUANTITY_E154] = { 'A' };

	#pragma pack(1)
	// структура с информацией об модуле E-154
	struct MODULE_DESCRIPTION_E154
	{
		MODULE_INFO_LUSBAPI     Module;		// общая информация о модуле
		INTERFACE_INFO_LUSBAPI  Interface;	// информация об используемом интерфейсе
		MCU_INFO_LUSBAPI<VERSION_INFO_LUSBAPI>		Mcu;	// информация о микроконтроллере
		ADC_INFO_LUSBAPI        Adc;			// информация о АЦП
		DAC_INFO_LUSBAPI        Dac;			// информация о ЦАП
		DIGITAL_IO_INFO_LUSBAPI DigitalIo;	// информация о цифровом вводе-выводе
	};
	// структура, задающая режим работы АЦП для модуля E14-154
	struct ADC_PARS_E154
	{
		WORD ClkSource;							// источник тактовых импульсов для запуска АПП
		WORD EnableClkOutput;					// разрешение трансляции тактовых импульсов запуска АЦП
		WORD InputMode;							// режим ввода даных с АЦП
		WORD SynchroAdType;						// тип аналоговой синхронизации
		WORD SynchroAdMode; 						// режим аналоговой сихронизации
		WORD SynchroAdChannel;  				// канал АЦП при аналоговой синхронизации
		SHORT SynchroAdPorog; 					// порог срабатывания АЦП при аналоговой синхронизации
		WORD ChannelsQuantity;					// число активных каналов
		WORD ControlTable[16];					// управляющая таблица с активными каналами
		double AdcRate;	  			  			// частота работы АЦП в кГц
		double InterKadrDelay;		  			// межкадровая задержка в мс
		double KadrRate;							// частота кадра в кГц
	};

	#pragma pack()

	//-----------------------------------------------------------------------------
	// интерфейс для модуля E14-154
	//-----------------------------------------------------------------------------
	struct ILE154 : public ILUSBBASE
	{
		// функции для работы с АЦП
		virtual BOOL WINAPI GET_ADC_PARS(ADC_PARS_E154 * const AdcPars) = 0;
		virtual BOOL WINAPI SET_ADC_PARS(ADC_PARS_E154 * const AdcPars) = 0;
		virtual BOOL WINAPI START_ADC(void) = 0;
		virtual BOOL WINAPI STOP_ADC(void) = 0;
		virtual BOOL WINAPI ADC_KADR(SHORT * const Data) = 0;
		virtual BOOL WINAPI ADC_SAMPLE(SHORT * const AdcData, WORD AdcChannel) = 0;
		virtual BOOL WINAPI ReadData(IO_REQUEST_LUSBAPI * const ReadRequest) = 0;
		virtual BOOL WINAPI ProcessArray(SHORT *src, double *dest, DWORD size, BOOL calibr, BOOL volt) = 0;
		virtual BOOL WINAPI ProcessOnePoint(SHORT src, double *dest, DWORD channel, BOOL calibr, BOOL volt) = 0;
		virtual BOOL WINAPI FIFO_STATUS(DWORD *FifoOverflowFlag, double *FifoMaxPercentLoad, DWORD *FifoSize, DWORD *MaxFifoBytesUsed) = 0;

		// функции для работы с ЦАП
		virtual BOOL WINAPI DAC_SAMPLE(SHORT * const DacData, WORD DacChannel) = 0;
		virtual BOOL WINAPI DAC_SAMPLE_VOLT(double  const DacData, BOOL calibr) = 0;

		// функции для работы с ТТЛ линиями
		virtual BOOL WINAPI ENABLE_TTL_OUT(BOOL EnableTtlOut) = 0;
		virtual BOOL WINAPI TTL_IN(WORD * const TtlIn) = 0;
		virtual BOOL WINAPI TTL_OUT(WORD TtlOut) = 0;

		// функции для работы с пользовательской информацией ППЗУ
		virtual BOOL WINAPI ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled) = 0;
		virtual BOOL WINAPI READ_FLASH_ARRAY(BYTE * const UserFlash) = 0;
		virtual BOOL WINAPI WRITE_FLASH_ARRAY(BYTE * const UserFlash) = 0;

		// функции для работы со служебной информацией ППЗУ
		virtual BOOL WINAPI GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E154 * const ModuleDescription) = 0;
		virtual BOOL WINAPI SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E154 * const ModuleDescription) = 0;

		// функции для прямого досупа к микроконтроллеру
		virtual BOOL WINAPI GetArray(BYTE * const Buffer, WORD Size, WORD Address) = 0;
		virtual BOOL WINAPI PutArray(BYTE * const Buffer, WORD Size, WORD Address) = 0;
	};




	// ==========================================================================
	// *************************** Модуль E14-440 *******************************
	// ==========================================================================
	// доступные состояния сброса модуля E14-440
	enum {	INIT_E440, RESET_E440, INVALID_RESET_TYPE_E440 };
	// доступные индексы источника тактовых импульсов для АЦП
	enum	{	INT_ADC_CLOCK_E440, INT_ADC_CLOCK_WITH_TRANS_E440, EXT_ADC_CLOCK_E440, INVALID_ADC_CLOCK_E440 };
	// доступные индексы диапазонов входного напряжения модуля E14-440
	enum {	ADC_INPUT_RANGE_10000mV_E440, ADC_INPUT_RANGE_2500mV_E440, ADC_INPUT_RANGE_625mV_E440, ADC_INPUT_RANGE_156mV_E440, INVALID_ADC_INPUT_RANGE_E440 };
	// возможные типы синхронизации модуля E14-440
	enum {	NO_SYNC_E440, TTL_START_SYNC_E440, TTL_KADR_SYNC_E440, ANALOG_SYNC_E440, INVALID_SYNC_E440 };
	// возможные опции наличия микросхемы ЦАП
	enum {	DAC_INACCESSIBLED_E440, DAC_ACCESSIBLED_E440, INVALID_DAC_OPTION_E440 };
	// возможные типы DSP (сейчас только ADSP-2185)
	enum {	ADSP2184_E440, ADSP2185_E440, ADSP2186_E440, INVALID_DSP_TYPE_E440 };
	// возможные тактовые частоты модудя (сейчас только 24000 кГц)
	enum {	F14745_E440, F16667_E440, F20000_E440, F24000_E440, INVALID_QUARTZ_FREQ_E440 };
	// доступные индексы ревизий модуля E14-440
	enum	{
				REVISION_A_E440, REVISION_B_E440, REVISION_C_E440, REVISION_D_E440,
				REVISION_E_E440, REVISION_F_E440, REVISION_G_E440,
				INVALID_REVISION_E440
			};

	// константы для работы с модулем
	enum 	{
				MAX_CONTROL_TABLE_LENGTH_E440 = 128,
				ADC_INPUT_RANGES_QUANTITY_E440 = INVALID_ADC_INPUT_RANGE_E440,
				ADC_CALIBR_COEFS_QUANTITY_E440 = ADC_INPUT_RANGES_QUANTITY_E440,
				MAX_ADC_FIFO_SIZE_E440 = 0x3000,			// 12288
				DAC_CHANNELS_QUANTITY_E440 = 0x2, DAC_CALIBR_COEFS_QUANTITY_E440 = DAC_CHANNELS_QUANTITY_E440,
				MAX_DAC_FIFO_SIZE_E440 = 0x0FC0,			// 4032
				TTL_LINES_QUANTITY_E440 = 0x10, 			// кол-во цифровых линий
				REVISIONS_QUANTITY_E440 = INVALID_REVISION_E440,		// кол-во ревизий (модификаций) модуля
			};
	// диапазоны входного напряжения АЦП в В
	const double ADC_INPUT_RANGES_E440[ADC_INPUT_RANGES_QUANTITY_E440] =
	{
		10.0, 10.0/4.0, 10.0/16.0, 10.0/64.0
	};
	// диапазоны выходного напряжения ЦАП в В
	const double DAC_OUTPUT_RANGE_E440 = 5.0;
	// доступные ревизии модуля
	const BYTE REVISIONS_E440[REVISIONS_QUANTITY_E440] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G' };

	#pragma pack(1)
	// структура с информацией об модуле E14-440
	struct MODULE_DESCRIPTION_E440
	{
		MODULE_INFO_LUSBAPI     Module;		// общая информация о модуле
		INTERFACE_INFO_LUSBAPI  Interface;	// информация об используемом интерфейсе
		MCU_INFO_LUSBAPI<VERSION_INFO_LUSBAPI>		Mcu;	// информация о микроконтроллере
		DSP_INFO_LUSBAPI        Dsp;			// информация о DSP
		ADC_INFO_LUSBAPI        Adc;			// информация о АЦП
		DAC_INFO_LUSBAPI        Dac;			// информация о ЦАП
		DIGITAL_IO_INFO_LUSBAPI DigitalIo;	// информация о цифровом вводе-выводе
	};
	// структура, задающая режим работы АЦП для модуля E-440
	struct ADC_PARS_E440
	{
		BOOL IsAdcEnabled;		 			// статус работы АЦП (только при чтении)
		BOOL IsCorrectionEnabled;			// управление разрешением корректировкой данных на уровне драйвера DSP
		WORD AdcClockSource;					// источник тактовых импульсов запуска АЦП: внутренние или внешние
		WORD InputMode;						// режим ввода даных с АЦП
		WORD SynchroAdType;					// тип аналоговой синхронизации
		WORD SynchroAdMode; 					// режим аналоговой сихронизации
		WORD SynchroAdChannel;  			// канал АЦП при аналоговой синхронизации
		SHORT SynchroAdPorog; 				// порог срабатывания АЦП при аналоговой синхронизации
		WORD ChannelsQuantity;				// число активных каналов
		WORD ControlTable[MAX_CONTROL_TABLE_LENGTH_E440];		// управляющая таблица с активными каналами
		double AdcRate;	  			  		// частота работы АЦП в кГц
		double InterKadrDelay;		  		// Межкадровая задержка в мс
		double KadrRate;					// частота кадра в кГц
		WORD AdcFifoBaseAddress;			// базовый адрес FIFO буфера АЦП
		WORD AdcFifoLength;					// длина FIFO буфера АЦП
		double AdcOffsetCoefs[ADC_CALIBR_COEFS_QUANTITY_E440];	// смещение	АЦП: 4диапазона
		double AdcScaleCoefs[ADC_CALIBR_COEFS_QUANTITY_E440];		// масштаб АЦП	: 4диапазона
	};

	// структура, задающая режим работы ЦАП для модуля E-440
	struct DAC_PARS_E440
	{
		BOOL DacEnabled;						// разрешение/запрещение работы ЦАП
		double DacRate;	  			  		// частота работы ЦАП в кГц
		WORD DacFifoBaseAddress;			// базовый адрес FIFO буфера ЦАП
		WORD DacFifoLength;					// длина FIFO буфера ЦАП
	};
	#pragma pack()

	// адрес начала сегмента блока данных в памяти программ драйвера DSP
	const WORD DataBaseAddress_E440 = 0x30;
	// переменные штатного LBIOS для модуля E14-440 (раполагаются в памяти программ DSP)
	#define 	  	L_PROGRAM_BASE_ADDRESS_E440				(DataBaseAddress_E440 + 0x0)
	#define 	  	L_READY_E440 									(DataBaseAddress_E440 + 0x1)
	#define	  	L_TMODE1_E440 									(DataBaseAddress_E440 + 0x2)
	#define	  	L_TMODE2_E440 									(DataBaseAddress_E440 + 0x3)
	#define	  	L_TEST_LOAD_E440	 							(DataBaseAddress_E440 + 0x4)
	#define	  	L_COMMAND_E440	 			      			(DataBaseAddress_E440 + 0x5)

	#define		L_DAC_SCLK_DIV_E440							(DataBaseAddress_E440 + 0x7)
	#define		L_DAC_RATE_E440								(DataBaseAddress_E440 + 0x8)
	#define	  	L_ADC_RATE_E440  			      			(DataBaseAddress_E440 + 0x9)
	#define		L_ADC_ENABLED_E440	 						(DataBaseAddress_E440 + 0xA)
	#define		L_ADC_FIFO_BASE_ADDRESS_E440				(DataBaseAddress_E440 + 0xB)
	#define		L_CUR_ADC_FIFO_LENGTH_E440					(DataBaseAddress_E440 + 0xC)
	#define		L_ADC_FIFO_LENGTH_E440						(DataBaseAddress_E440 + 0xE)
	#define		L_CORRECTION_ENABLED_E440					(DataBaseAddress_E440 + 0xF)
	#define		L_LBIOS_VERSION_E440							(DataBaseAddress_E440 + 0x10)
	#define		L_ADC_SAMPLE_E440								(DataBaseAddress_E440 + 0x11)
	#define		L_ADC_CHANNEL_E440	 						(DataBaseAddress_E440 + 0x12)
	#define		L_INPUT_MODE_E440								(DataBaseAddress_E440 + 0x13)
	#define		L_SYNCHRO_AD_CHANNEL_E440					(DataBaseAddress_E440 + 0x16)
	#define		L_SYNCHRO_AD_POROG_E440						(DataBaseAddress_E440 + 0x17)
	#define		L_SYNCHRO_AD_MODE_E440 						(DataBaseAddress_E440 + 0x18)
	#define		L_SYNCHRO_AD_TYPE_E440 						(DataBaseAddress_E440 + 0x19)

	#define		L_CONTROL_TABLE_LENGHT_E440				(DataBaseAddress_E440 + 0x1B)
	#define		L_FIRST_SAMPLE_DELAY_E440					(DataBaseAddress_E440 + 0x1C)
	#define		L_INTER_KADR_DELAY_E440						(DataBaseAddress_E440 + 0x1D)

	#define		L_DAC_SAMPLE_E440								(DataBaseAddress_E440 + 0x20)
	#define		L_DAC_ENABLED_E440					 		(DataBaseAddress_E440 + 0x21)
	#define		L_DAC_FIFO_BASE_ADDRESS_E440				(DataBaseAddress_E440 + 0x22)
	#define		L_CUR_DAC_FIFO_LENGTH_E440					(DataBaseAddress_E440 + 0x24)
	#define		L_DAC_FIFO_LENGTH_E440						(DataBaseAddress_E440 + 0x25)

	#define		L_FLASH_ENABLED_E440 						(DataBaseAddress_E440 + 0x26)
	#define		L_FLASH_ADDRESS_E440 						(DataBaseAddress_E440 + 0x27)
	#define		L_FLASH_DATA_E440 							(DataBaseAddress_E440 + 0x28)

	#define		L_ENABLE_TTL_OUT_E440						(DataBaseAddress_E440 + 0x29)
	#define		L_TTL_OUT_E440									(DataBaseAddress_E440 + 0x2A)
	#define		L_TTL_IN_E440									(DataBaseAddress_E440 + 0x2B)

	#define		L_ADC_CLOCK_SOURCE_E440						(DataBaseAddress_E440 + 0x2F)

	#define		L_SCALE_E440									(DataBaseAddress_E440 + 0x30)
	#define		L_ZERO_E440										(DataBaseAddress_E440 + 0x34)

	#define		L_CONTROL_TABLE_E440							(0x80)

	#define		L_DSP_INFO_STUCTURE_E440					(0x200)

	//-----------------------------------------------------------------------------
	// интерфейс модуля E14-440
	//-----------------------------------------------------------------------------
	struct ILE440 : public ILUSBBASE
	{
		// функции работы с DSP
		virtual BOOL WINAPI RESET_MODULE(BYTE ResetFlag = INIT_E440) = 0;
		virtual BOOL WINAPI LOAD_MODULE(const char * const FileName = NULL) = 0;
		virtual BOOL WINAPI TEST_MODULE(void) = 0;
		virtual BOOL WINAPI SEND_COMMAND(WORD Command) = 0;

		// функции для работы с АЦП
		virtual BOOL WINAPI GET_ADC_PARS(ADC_PARS_E440 * const AdcPars) = 0;
		virtual BOOL WINAPI SET_ADC_PARS(ADC_PARS_E440 * const AdcPars) = 0;
		virtual BOOL WINAPI START_ADC(void) = 0;
		virtual BOOL WINAPI STOP_ADC(void) = 0;
		virtual BOOL WINAPI ADC_KADR(SHORT * const Data) = 0;
		virtual BOOL WINAPI ADC_SAMPLE(SHORT * const AdcData, WORD AdcChannel) = 0;
		virtual BOOL WINAPI ReadData(IO_REQUEST_LUSBAPI * const ReadRequest) = 0;

		// функции для работы с ЦАП
		virtual BOOL WINAPI GET_DAC_PARS(DAC_PARS_E440 * const DacPars) = 0;
		virtual BOOL WINAPI SET_DAC_PARS(DAC_PARS_E440 * const DacPars) = 0;
		virtual BOOL WINAPI START_DAC(void) = 0;
		virtual BOOL WINAPI STOP_DAC(void) = 0;
		virtual BOOL WINAPI WriteData(IO_REQUEST_LUSBAPI * const WriteRequest) = 0;
		virtual BOOL WINAPI DAC_SAMPLE(SHORT * const DacData, WORD DacChannel) = 0;

		// функции для работы с цифровыми линиями
		virtual BOOL WINAPI ENABLE_TTL_OUT(BOOL EnableTtlOut) = 0;
		virtual BOOL WINAPI TTL_IN(WORD * const TtlIn) = 0;
		virtual BOOL WINAPI TTL_OUT(WORD TtlOut) = 0;

		// функции для работы пользовательским ППЗУ модуля
		virtual BOOL WINAPI ENABLE_FLASH_WRITE(BOOL EnableFlashWrite) = 0;
		virtual BOOL WINAPI READ_FLASH_WORD(WORD FlashAddress, SHORT * const FlashWord) = 0;
		virtual BOOL WINAPI WRITE_FLASH_WORD(WORD FlashAddress, SHORT FlashWord) = 0;

		// функции для работы со служебной информацией из ППЗУ
		virtual BOOL WINAPI GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E440 * const ModuleDescription) = 0;
		virtual BOOL WINAPI SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E440 * const ModuleDescription) = 0;

		// функции для работы с памятью DSP
		virtual BOOL WINAPI PUT_LBIOS_WORD(WORD Address, SHORT Data) = 0;
		virtual BOOL WINAPI GET_LBIOS_WORD(WORD Address, SHORT * const Data) = 0;
		virtual BOOL WINAPI PUT_DM_WORD(WORD Address, SHORT Data) = 0;
		virtual BOOL WINAPI GET_DM_WORD(WORD Address, SHORT * const Data) = 0;
		virtual BOOL WINAPI PUT_PM_WORD(WORD Address, long Data) = 0;
		virtual BOOL WINAPI GET_PM_WORD(WORD Address, long * const Data) = 0;
		virtual BOOL WINAPI PUT_DM_ARRAY(WORD BaseAddress, WORD NPoints, SHORT * const Data) = 0;
		virtual BOOL WINAPI GET_DM_ARRAY(WORD BaseAddress, WORD NPoints, SHORT * const Data) = 0;
		virtual BOOL WINAPI PUT_PM_ARRAY(WORD BaseAddress, WORD NPoints, long * const Data) = 0;
		virtual BOOL WINAPI GET_PM_ARRAY(WORD BaseAddress, WORD NPoints, long * const Data) = 0;

		// функции для работы с загрузочным ППЗУ модуля
		virtual BOOL WINAPI ERASE_BOOT_FLASH(void) = 0;
		virtual BOOL WINAPI PUT_ARRAY_BOOT_FLASH(DWORD BaseAddress, DWORD NBytes, BYTE *Data) = 0;
		virtual BOOL WINAPI GET_ARRAY_BOOT_FLASH(DWORD BaseAddress, DWORD NBytes, BYTE *Data) = 0;
	};




	// ==========================================================================
	// *************************** Модуль E20-10 ********************************
	// ==========================================================================
	// доступные индексы источника сигнала старта сбора данных
	enum {
				INT_ADC_START_E2010, INT_ADC_START_WITH_TRANS_E2010,
				EXT_ADC_START_ON_RISING_EDGE_E2010, EXT_ADC_START_ON_FALLING_EDGE_E2010,
//				EXT_ADC_START_ON_HIGH_LEVEL_E2010, EXT_ADC_START_ON_LOW_LEVEL_E2010,		// для Rev.B и выше (пока нет)
				INVALID_ADC_START_E2010
			};
	// доступные индексы источника тактовых импульсов для запуска АЦП
	enum {	INT_ADC_CLOCK_E2010, INT_ADC_CLOCK_WITH_TRANS_E2010, EXT_ADC_CLOCK_ON_RISING_EDGE_E2010, EXT_ADC_CLOCK_ON_FALLING_EDGE_E2010, INVALID_ADC_CLOCK_E2010 };
	// возможные типы аналоговой синхронизации ввода данных (для Rev.B и выше)
	enum {
				NO_ANALOG_SYNCHRO_E2010,			// отсутствие аналоговой синхронизации
				ANALOG_SYNCHRO_ON_RISING_CROSSING_E2010, ANALOG_SYNCHRO_ON_FALLING_CROSSING_E2010,	// аналоговая синхронизация по переходу
				ANALOG_SYNCHRO_ON_HIGH_LEVEL_E2010, ANALOG_SYNCHRO_ON_LOW_LEVEL_E2010,		// аналоговая синхронизация по уровню
				INVALID_ANALOG_SYNCHRO_E2010
			};
	// доступные индексы диапазонов входного напряжения модуля E20-10
	enum {	ADC_INPUT_RANGE_3000mV_E2010, ADC_INPUT_RANGE_1000mV_E2010, ADC_INPUT_RANGE_300mV_E2010, INVALID_ADC_INPUT_RANGE_E2010 };
	// возможные типы подключения входного тракта модуля E20-10
	enum {	ADC_INPUT_ZERO_E2010, ADC_INPUT_SIGNAL_E2010, INVALID_ADC_INPUT_E2010 };
	// возможные индексы для управления входным током смещения модуля E20-10 (для Rev.B и выше)
	enum {	INPUT_CURRENT_OFF_E2010, INPUT_CURRENT_ON_E2010, INVALID_INPUT_CURRENT_E2010 };
	// возможные режимы фиксации факта перегрузки входных каналов при сборе данных (только для Rev.A)
	enum {	CLIPPING_OVERLOAD_E2010, MARKER_OVERLOAD_E2010, INVALID_OVERLOAD_E2010 };
	// доступные номера битов ошибок при выполнении сбора данных с АЦП
	enum {
				// битовое поле BufferOverrun структуры DATA_STATE_E2010
				BUFFER_OVERRUN_E2010 = 0x0,		// переполнение внутреннего буфера модуля
				// битовое поле ChannelsOverFlow структуры DATA_STATE_E2010 (для Rev.B и выше)
				OVERFLOW_OF_CHANNEL_1_E2010 = 0x0, OVERFLOW_OF_CHANNEL_2_E2010,	// биты локальных признаков переполнения разрядной сетки соответствующего канала
				OVERFLOW_OF_CHANNEL_3_E2010, OVERFLOW_OF_CHANNEL_4_E2010,			// за время выполнения одного запроса сбора данных ReadData()
				OVERFLOW_E2010 = 0x7					// бит глобального признака факта переполнения разрядной сетки модуля за всё время сбора данных от момента START_ADC() до STOP_ADC()
			};
	// возможные опции наличия микросхемы ЦАП для модуля E20-10
	enum {	DAC_INACCESSIBLED_E2010, DAC_ACCESSIBLED_E2010, INVALID_DAC_OPTION_E2010 };
	// доступные индексы ревизий модуля E20-10
	enum {	REVISION_A_E2010, REVISION_B_E2010, INVALID_REVISION_E2010 };
	// доступные индексы ФНЧ входного сигнала модуля E20-10
	enum	{
				LPF_1250KHz_E2010, 			// полоса входных частот 1.25 МГц
				LPF_5000KHz_E2010, 			// полоса входных частот 5.00 МГц
				INVALID_LPF_TYPE_E2010,
            LPF_TYPES_QUANTITY_E2010 = INVALID_LPF_TYPE_E2010
			};
	// доступные индексы температурного диапазона модуля E20-10
	enum	{
				COMMERCIAL_TEMPERATURE_GRADE_E2010,		// температурный диапазон Commercial:  +5° to +55°C
				INDUSTRIAL_TEMPERATURE_GRADE_E2010,		// температурный диапазон Industrial: -40° to +60°C
				INVALID_TEMPERATURE_GRADE_E2010,
				TEMPERATURE_GRADES_QUANTITY_E2010 = INVALID_TEMPERATURE_GRADE_E2010
			};

	// доступные индексы диапазона входного напряжения питания модуля
	enum	{
				POWER_SUPPLY_RANGE_IS_FROM_9_TO_27_VOLT_E2010,
				POWER_SUPPLY_RANGE_IS_FROM_8_TO_40_VOLT_E2010,
				INVALID_POWER_SUPPLY_RANGE_E2010,
				POWER_SUPPLY_RANGES_QUANTITY_E2010 = INVALID_POWER_SUPPLY_RANGE_E2010
			};

	// названия диапазонов входного напряжения питания модуля
	const char * const POWER_SUPPLY_RANGES_E2010[POWER_SUPPLY_RANGES_QUANTITY_E2010] =
	{
		"from +9V to +27V", "from +8V to +40V"
	};

	// доступные битовые константы для задания тестовых режимов работы модуля E20-10
	enum { NO_TEST_MODE_E2010, TEST_MODE_1_E2010 };

	// константы для работы с модулем
	enum 	{
				ADC_CHANNELS_QUANTITY_E2010 = 0x4,
				MAX_CONTROL_TABLE_LENGTH_E2010 = 256, MAX_EXTENDED_CONTROL_TABLE_LENGTH_E2010 = 8192,
				ADC_INPUT_RANGES_QUANTITY_E2010 = INVALID_ADC_INPUT_RANGE_E2010,
				ADC_INPUT_TYPES_QUANTITY_E2010 = INVALID_ADC_INPUT_E2010,
				ADC_CALIBR_COEFS_QUANTITY_E2010 = ADC_CHANNELS_QUANTITY_E2010 * ADC_INPUT_RANGES_QUANTITY_E2010,
				DAC_CHANNELS_QUANTITY_E2010 = 0x2, DAC_CALIBR_COEFS_QUANTITY_E2010 = DAC_CHANNELS_QUANTITY_E2010,
				TTL_LINES_QUANTITY_E2010 = 0x10,		// кол-во входных и выходных цифровых линий
				USER_FLASH_SIZE_E2010 = 0x200,  		// размер области пользовательского ППЗУ в байтах
				REVISIONS_QUANTITY_E2010 = INVALID_REVISION_E2010,				// кол-во ревизий модуля
				ADC_PLUS_OVERLOAD_MARKER_E2010 = 0x5FFF,	// признак 'плюс' перегрузки отсчёта с АЦП (только для Rev.A)
				ADC_MINUS_OVERLOAD_MARKER_E2010 = 0xA000	// признак 'минус' перегрузки отсчёта с АЦП (только для Rev.A)
			};

	// диапазоны входного напряжения АЦП в В
	const double ADC_INPUT_RANGES_E2010[ADC_INPUT_RANGES_QUANTITY_E2010] =
	{
		3.0, 1.0, 0.3
	};
	// диапазон выходного напряжения ЦАП в В
	const double DAC_OUTPUT_RANGE_E2010 = 5.0;
	// доступные ревизии модуля
	const BYTE REVISIONS_E2010[REVISIONS_QUANTITY_E2010] = { 'A', 'B' };
	// названия доступных ФНЧ входного сигнала модуля E20-10
	const char * const LPF_TYPES_E2010[LPF_TYPES_QUANTITY_E2010] = { "1.25 MHz", "5.00 MHz" };
	// названия доступных температурных диапазонов модуля E20-10
	const char * const TEMPERATURE_GRADES_E2010[TEMPERATURE_GRADES_QUANTITY_E2010] =
	{
		#if defined (__BORLANDC__) || defined (_MSC_VER)
			// -=== Borland & Microsoft Visual Studio ===-
			#if defined (__CONSOLE__)
				"Commercial (from +5\xF8\x43 to +55\xF8\x43)", "Industrial (from -40\xF8\x43 to +60\xF8\x43)"
			#else
				"Commercial (from +5°C to +55°C)", "Industrial (from -40°C to +60°C)"
			#endif
		#elif defined (__MINGW32__) || defined (__MINGW64__)
			// -=== MinGW32 & MinGW-w64 ===-
				"Commercial (from +5°C to +55°C)", "Industrial (from -40°C to +60°C)"
		#elif defined(__GNUC__) || defined(__GNUG__)
			// -=== GNU GCC/G++ ===-
		#else
			// . . .
			"Commercial (from +5°C to +55°C)", "Industrial (from -40°C to +60°C)"
		#endif
	};

	#pragma pack(1)
	// структура с общей информацией об модуле E20-10
	struct MODULE_DESCRIPTION_E2010
	{
		MODULE_INFO_LUSBAPI     Module;		// общая информация о модуле
		INTERFACE_INFO_LUSBAPI  Interface;	// информация об интерфейсе
		MCU_INFO_LUSBAPI<MCU_VERSION_INFO_LUSBAPI>	Mcu;	// информация о микроконтроллере
		PLD_INFO_LUSBAPI        Pld;			// информация о ПЛИС
		ADC_INFO_LUSBAPI        Adc;			// информация о АЦП
		DAC_INFO_LUSBAPI        Dac;			// информация о ЦАП
		DIGITAL_IO_INFO_LUSBAPI DigitalIo;	// информация о цифровом вводе-выводе
	};
	// структура пользовательского ППЗУ
	struct USER_FLASH_E2010
	{
		BYTE Buffer[USER_FLASH_SIZE_E2010];
	};
	// структура с параметрами синхронизации ввода данных с АЦП
	struct SYNCHRO_PARS_E2010
	{
		WORD	StartSource;				  	// тип и источник сигнала начала сбора данных с АЦП (внутренний или внешний и т.д.)
		DWORD StartDelay; 					// задержка старта сбора данных в кадрах отсчётов c АЦП (для Rev.B и выше)
		WORD	SynhroSource;					// источник тактовых импульсов запуска АЦП (внутренние или внешние и т.д.)
		DWORD StopAfterNKadrs;				// останов сбора данных после задаваемого здесь кол-ва собранных кадров отсчётов АЦП (для Rev.B и выше)
		WORD	SynchroAdMode;   				// режим аналоговой сихронизации: переход или уровень (для Rev.B и выше)
		WORD	SynchroAdChannel;				// физический канал АЦП для аналоговой синхронизации (для Rev.B и выше)
		SHORT SynchroAdPorog;  				// порог срабатывания при аналоговой синхронизации (для Rev.B и выше)
		BYTE	IsBlockDataMarkerEnabled;	// маркирование начала блока данных (удобно, например, при аналоговой синхронизации ввода по уровню) (для Rev.B и выше)
	};
	// структура с параметрами работы АЦП
	struct ADC_PARS_E2010
	{
		BOOL IsAdcCorrectionEnabled;		// управление разрешением автоматической корректировкой получаемых данных на уровне модуля (для Rev.B и выше)
		WORD OverloadMode;					// режим фиксации факта перегрузки входных каналов модуля (только для Rev.A)
		WORD InputCurrentControl;			// управление входным током смещения (для Rev.B и выше)
		SYNCHRO_PARS_E2010 SynchroPars;	// параметры синхронизации ввода данных с АЦП
		WORD ChannelsQuantity;				// кол-во активных каналов (размер кадра отсчётов)
		WORD ControlTable[MAX_CONTROL_TABLE_LENGTH_E2010];	// управляющая таблица с активными логическими каналами
		WORD InputRange[ADC_CHANNELS_QUANTITY_E2010]; 	// индексы диапазонов входного напряжения физических каналов: 3.0В, 1.0В или 0.3В
		WORD InputSwitch[ADC_CHANNELS_QUANTITY_E2010];	// индексы типа подключения физических каналов: земля или сигнал
		double AdcRate;						// частота работы АЦП в кГц
		double InterKadrDelay;				// межкадровая задержка в мс
		double KadrRate;						// частота кадра в кГц
		double AdcOffsetCoefs[ADC_INPUT_RANGES_QUANTITY_E2010][ADC_CHANNELS_QUANTITY_E2010];	// массив коэффициентов для корректировки смещение отсчётов АЦП: (3 диапазона)*(4 канала) (для Rev.B и выше)
		double AdcScaleCoefs[ADC_INPUT_RANGES_QUANTITY_E2010][ADC_CHANNELS_QUANTITY_E2010];		// массив коэффициентов для корректировки масштаба отсчётов АЦП: (3 диапазона)*(4 канала) (для Rev.B и выше)
	};
	// структура с информацией о текущем состоянии процесса сбора данных
	struct DATA_STATE_E2010
	{
		BYTE ChannelsOverFlow;			// битовые признаки перегрузки входных аналоговых каналов (для Rev.B и выше)
		BYTE BufferOverrun;				// битовые признаки переполнения внутреннего буфера модуля
		DWORD CurBufferFilling;			// заполненность внутреннего буфера модуля Rev.B и выше, в отсчётах
		DWORD MaxOfBufferFilling;		// за время сбора максимальная заполненность внутреннего буфера модуля Rev.B и выше, в отсчётах
		DWORD BufferSize;					// размер внутреннего буфера модуля Rev.B и выше, в отсчётах
		double CurBufferFillingPercent;		// текущая степень заполнения внутреннего буфера модуля Rev.B и выше, в %
		double MaxOfBufferFillingPercent;	// за время сбора максимальная степень заполнения внутреннего буфера модуля Rev.B и выше, в %
	};
	// флажки-признаки, определяющие наличие различных модификации модуля и прошивки
	union MODIFICATION_E2010
	{
		struct _BITS_
		{
			BYTE	LowPassFilterType			: 2;		// тип используемого входного ФНЧ
			BYTE	ModuleTemperatureGrade	: 2;		// признак Temperature Grade у железа модуля: Commercial or Industrial
			BYTE	PldTemperatureGrade		: 2;		// признак Temperature Grade у прошивки ПЛИС'а (с версии 02.0x.12): Commercial or Industrial
			BYTE	RESERVED						: 2;		// must be 0x0
		} BITS;
		BYTE	Value;
	};
	#pragma pack()

	//-----------------------------------------------------------------------------
	// интерфейс модуля E20-10
	//-----------------------------------------------------------------------------
	struct ILE2010 : public ILUSBBASE
	{
		// загрузка ПЛИС модуля
		virtual BOOL WINAPI LOAD_MODULE(const char * const FileName = NULL) = 0;
		virtual BOOL WINAPI TEST_MODULE(WORD TestModeMask = 0x0) = 0;

		// работа с АЦП
		virtual BOOL WINAPI GET_ADC_PARS(ADC_PARS_E2010 * const AdcPars) = 0;
		virtual BOOL WINAPI SET_ADC_PARS(ADC_PARS_E2010 * const AdcPars) = 0;
		virtual BOOL WINAPI START_ADC(void) = 0;
		virtual BOOL WINAPI STOP_ADC(void) = 0;
		virtual BOOL WINAPI GET_DATA_STATE(DATA_STATE_E2010 * const DataState) = 0;
		virtual BOOL WINAPI ReadData(IO_REQUEST_LUSBAPI * const ReadRequest) = 0;

		// однократная синхронная работа с ЦАП
		virtual BOOL WINAPI DAC_SAMPLE(SHORT * const DacData, WORD DacChannel) = 0;

		// работа с цифровыми линиями
		virtual BOOL WINAPI ENABLE_TTL_OUT(BOOL EnableTtlOut) = 0;
		virtual BOOL WINAPI TTL_IN (WORD * const TtlIn) = 0;
		virtual BOOL WINAPI TTL_OUT(WORD TtlOut) = 0;

		// функции для работы с пользовательской информацией ППЗУ
		virtual BOOL WINAPI ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled) = 0;
		virtual BOOL WINAPI READ_FLASH_ARRAY(USER_FLASH_E2010 * const UserFlash) = 0;
		virtual BOOL WINAPI WRITE_FLASH_ARRAY(USER_FLASH_E2010 * const UserFlash) = 0;

		// информация о модуле
		virtual BOOL WINAPI GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E2010 * const ModuleDescription) = 0;
		virtual BOOL WINAPI SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E2010 * const ModuleDescription) = 0;
	};





	// ==========================================================================
	// **************************** Модуль E-310 ********************************
	// ==========================================================================

	// -============= генератор ==============-
	// типы циклического автосканирования по частоте для генератора
	enum CYCLIC_AUTO_SCAN_TYPES_E310
	{
		NO_CYCLIC_AUTOSCAN_E310,
		CYCLIC_PILA_AUTO_SCAN_E310, CYCLIC_TRIANGLE_AUTO_SCAN_E310,
		INVALID_CYCLIC_AUTOSCAN_TYPE_E310
	};
	// типы приращений частоты генератора
	enum INCREMENT_INTERVAL_TYPES_E310
	{
		WAVEFORM_CYCLE_INCREMENT_INTERVAL_E310,
		CLOCK_PERIOD_INCREMENT_INTERVAL_E310,
		INVALID_INCREMENT_INTERVAL_TYPE_E310
	};
	// доступные индексы умножителя приращений частоты генератора
	enum INCREMENT_INTERVAL_MULTIPLIERS_INDEXES_E310
	{
		INCREMENT_INTERVAL_MULTIPLIERS_001_E310,
		INCREMENT_INTERVAL_MULTIPLIERS_005_E310,
		INCREMENT_INTERVAL_MULTIPLIERS_100_E310,
		INCREMENT_INTERVAL_MULTIPLIERS_500_E310,
		INVALID_INCREMENT_INTERVAL_MULTIPLIER_E310
	};
	// доступные индексы источника тактирующих импульсов для генератора
	enum MASTER_CLOCK_SOURCES_E310
	{
		INTERNAL_MASTER_CLOCK_E310, EXTERNAL_MASTER_CLOCK_E310, INVALID_MASTER_CLOCK_SOURCE_E310
	};
	// возможные типы инкрементации частоты генератора
	enum INCREMENT_TYPES_E310
	{
		AUTO_INCREMENT_E310, CTRL_LINE_INCREMENT_E310, INVALID_INCREMENT_TYPE_E310
	};
	// возможные типы линии CTRL
	enum CTRL_LINE_TYPES_E310
	{
		INTERNAL_CTRL_LINE_E310, EXTERNAL_CTRL_LINE_E310, INVALID_CTRL_LINE_TYPE_E310
	};
	// возможные типы линии INTERRUPT
	enum INTERRUPT_LINE_TYPES_E310
	{
		INTERNAL_INTERRUPT_LINE_E310, EXTERNAL_INTERRUPT_LINE_E310, INVALID_INTERRUPT_LINE_TYPE_E310
	};
	// возможные типы формирования выходного синхросигнала на линии "SYNCOUT"
	enum SYNCOUT_TYPES_E310
	{
		SYNCOUT_ON_EACH_INCREMENT_E310, SYNCOUT_AT_END_OF_SCAN_E310, INVALID_SYNCOUT_TYPES_E310
	};
	// доступные типы аналоговых выходных сигналов
	enum ANALOG_OUTPUT_TYPES_E310
	{
		TRIANGULAR_ANALOG_OUTPUT_E310, SINUSOIDAL_ANALOG_OUTPUT_E310, INVALID_ANALOG_OUTPUT_TYPE_E310
	};
	// доступные индексы усиления выходного тракта генератора
	enum ANALOG_OUTPUT_GAINS_INDEXES_E310
	{
		ANALOG_OUTPUT_GAIN_PLUS_10_DB_E310, ANALOG_OUTPUT_GAIN_PLUS_06_DB_E310,
		ANALOG_OUTPUT_GAIN_PLUS_04_DB_E310, ANALOG_OUTPUT_GAIN_PLUS_03_DB_E310,
		ANALOG_OUTPUT_GAIN_00_DB_E310,
		ANALOG_OUTPUT_GAIN_MINUS_02_DB_E310, ANALOG_OUTPUT_GAIN_MINUS_03_DB_E310,
		ANALOG_OUTPUT_GAIN_MINUS_06_DB_E310, ANALOG_OUTPUT_GAIN_MINUS_09_DB_E310,
		ANALOG_OUTPUT_GAIN_MINUS_12_DB_E310, ANALOG_OUTPUT_GAIN_MINUS_14_DB_E310,
		ANALOG_OUTPUT_GAIN_MINUS_18_DB_E310, ANALOG_OUTPUT_GAIN_MINUS_21_DB_E310,
		ANALOG_OUTPUT_GAIN_MINUS_24_DB_E310,
		INVALID_ANALOG_OUTPUT_GAINS_INDEX_E310,
		ANALOG_OUTPUT_GAINS_QUANTITY_E310 = INVALID_ANALOG_OUTPUT_GAINS_INDEX_E310
	};
	// доступные индексы смещения на выходе 10 Ом
	enum OUTPUT_10_OHM_OFFSET_SOURCES_E310
	{
		INTERNAL_OUTPUT_10_OHM_OFFSET_E310,
		EXTERNAL_OUTPUT_10_OHM_OFFSET_E310,
		INVALID_OUTPUT_10_OHM_OFFSET_SOURCE_E310
	};
	// -======================================-

	// -============= частотомер FM ==============-
	// доступные индексы делителя входного сигнала FM
	enum FM_MODES_E310
	{
		PERIOD_MODE_FM_E310, GATE_MODE_FM_E310, CAPTURE_MODE_FM_E310, INVALID_FM_MODE_E310
	};
	// возможные индексы делители входного сигнала измерителя частоты FM
	enum INPUT_DIVIDER_FM_E310
	{
		INPUT_DIVIDER_8_FM_E310, INPUT_DIVIDER_1_FM_E310, INVALID_INPUT_DIVIDER_FM_E310
	};
	// доступные индексы делителя базовой частоты FM
	enum BASE_CLOCK_DIV_INDEXES_FM_E310
	{
		BASE_CLOCK_DIV_01_INDEX_FM_E310, BASE_CLOCK_DIV_02_INDEX_FM_E310,
		BASE_CLOCK_DIV_04_INDEX_FM_E310, BASE_CLOCK_DIV_08_INDEX_FM_E310,
		BASE_CLOCK_DIV_16_INDEX_FM_E310, BASE_CLOCK_DIV_32_INDEX_FM_E310,
		BASE_CLOCK_DIV_64_INDEX_FM_E310,
		INVALID_BASE_CLOCK_DIV_INDEX_FM_E310
	};
	// -==========================================-

	// -============= А Ц П ==============-
	// доступные источники запуска АЦП
	enum ADC_START_SOURCES_E310
	{
		INTERNAL_ADC_START_E310, EXTERNAL_ADC_START_E310, INVALID_ADC_START_SOURCES_E310
	};
	// доступные номера бит для битовой маски каналов АЦП
	enum ADC_CHANNEL_BIT_NUMBERS_E310
	{
		ADC_CHANNEL_1_E310, ADC_CHANNEL_2_E310, ADC_CHANNEL_3_E310, ADC_CHANNEL_4_E310, 
		INVALID_ADC_CHANNEL_BIT_NUMBER_E310,
		ADC_CHANNEL_QUANTITY_E310 = INVALID_ADC_CHANNEL_BIT_NUMBER_E310
	};
	// -==================================-

	// размер области пользовательского ППЗУ в байтах
	enum {	USER_FLASH_SIZE_E310 = 0x200 };
	// доступные индексы ревизий модуля E-310
	enum {	REVISION_A_E310, /*REVISION_B_E310,*/ INVALID_REVISION_E310, REVISIONS_QUANTITY_E310 = INVALID_REVISION_E310, };

	// доступные ревизии модуля
	const BYTE REVISIONS_E310[REVISIONS_QUANTITY_E310] = { 'A'/*, 'B'*/ };

	#pragma pack(1)
	// структура с общей информацией об модуле E-310
	struct MODULE_DESCRIPTION_E310
	{
		MODULE_INFO_LUSBAPI		Module;		// общая информация о модуле
		INTERFACE_INFO_LUSBAPI	Interface;	// информация об интерфейсе
		MCU_INFO_LUSBAPI<MCU_VERSION_INFO_LUSBAPI>	Mcu;	// информация о микроконтроллере
		ADC_INFO_LUSBAPI			Adc;			// информация о АЦП
		DAC_INFO_LUSBAPI        Dac;			// информация о ЦАП
		DIGITAL_IO_INFO_LUSBAPI DigitalIo;	// информация о цифровом вводе-выводе
	};

	// структура пользовательского ППЗУ
	struct USER_FLASH_E310
	{
		BYTE Buffer[USER_FLASH_SIZE_E310];
	};

	// структура с параметрами частотного приращения
	struct INCREMENT_INTRERVAL_PARS_E310
	{
		BYTE	 BaseIntervalType;		// [in-out]	тип базового интервала приращения, который может быть кратен: периоду MCLK или периоду выходного сигнала
		BYTE	 MultiplierIndex;			// [in-out]	индекс умножителя для базового интервала приращения (тип INCREMENT_INTERVAL_MULTIPLIERS_INDEXES_E310)
		double MultiplierValue;			// [out]		величина умножителя для базового интервала приращения: 1, 5, 100 или 500
		WORD	 BaseIntervalsNumber;	// [in-out]	кол-во базовых интервалов в интервале приращения
		double Duration; 					// [out] 	общая длительность приращения в мс (только для интервала приращений по MCLK, иначе 0
	};

	// структура с параметрами аналоговых выходов генератора
	struct ANALOG_OUTPUTS_PARS_E310
	{
		BYTE	 SignalType;				// [in-out]	тип аналогового сигнала на выходах 10 и 50 Ом: синусоидальный или треугольный
		BYTE	 GainIndex;					// [in-out]	индекс усиления выходного тракта генератора (тип ANALOG_OUTPUT_GAINS_INDEXES_E310)
		double GaindB;						// [out]		усиление выходного тракта генератора в дБ
		double Output10OhmInV;			// [out]		амплитуда сигнала на выходе 10 Ом в В
		double Output10OhmIndB;			// [out]		амплитуда сигнала на выходе 10 Ом в дБ
		double Output10OhmOffset;		// [in-out]	величина внутреннего смещения на выходе 10 Ом в В
		BYTE	 Output10OhmOffsetSource;//[in-out]	тип смещения на выходе 10 Ом: внутреннее или внешнее
		double Output50OhmInV;			// [out]		амплитуда сигнала на выходе 50 Ом в В
		double Output50OhmIndB;			// [out]		амплитуда сигнала на выходе 50 Ом в дБ
	};

	// структура с параметрами работы генератора
	struct GENERATOR_PARS_E310
	{
		BOOL	 GeneratorEna;				// [in]		текущее состояние работы генератора
		double StartFrequency;			// [in-out]	начальная частота в кГц
		double FinalFrequency;			// [out]		конечная частота в кГц
		double FrequencyIncrements;	// [in-out]	частота приращения в кГц
		WORD	 NumberOfIncrements;		// [in-out]	кол-во приращений частоты сканирования
		INCREMENT_INTRERVAL_PARS_E310 IncrementIntervalPars;	// [in-out] параметры частотного приращения
		double MasterClock;				// [in-out]	частота тактирующего сигнала генератора в кГц
		BYTE	 MasterClockSource;		// [in-out]	источник тактирующего сигнала генератора: внутренний или внешний
		BYTE	 CyclicAutoScanType;		// [in-out]	тип циклического автосканирования выходного сигнала: нет циклического сканирования, 'пила' или 'треугольник'
		BYTE	 IncrementType;			// [in-out]	тип инкрементации частоты генератора: внутренняя (автоматическая) или с помощью управляющей линии "CTRL"
		BYTE	 CtrlLineType;				// [in-out]	тип линии "CTRL" для (управления инкрементацией частоты)/старта генератора: внутренняя (от MCU) или внешняя
		BYTE	 InterrupLineType;		// [in-out]	тип линии "INTERRUPT" для останова работы генератора: внутренняя (от MCU) или внешняя
		BOOL	 SquareWaveOutputEna;	// [in-out]	разрешение сигнала генератора на цифровом выходе "Меандр"
		BOOL	 SynchroOutEna;			// [in-out]	разрешение синхросигнала генератора на выходной линии "SYNCOUT"
		BYTE	 SynchroOutType;			// [in-out]	тип формирования синхросигнала генератора: при каждом приращении частоты или только по окончании сканирования
		ANALOG_OUTPUTS_PARS_E310 AnalogOutputsPars;	//	[in-out] параметры работы аналоговых выходов
	};

	// структура с параметрами работы измерителя частоты (FM)
	struct FM_PARS_E310
	{
		BOOL	 FmEna;					   // [out]		текущее состояние работы измерителя частоты
		BYTE	 Mode;						// [in-out]	режим работы измерителя частоты
		BYTE	 InputDivider;				// [in-out]	управление входным частоты делителем 1/8 (тип INPUT_DIVIDER_FM_E310)
		BYTE	 BaseClockRateDivIndex;	// [in-out]	индекс делителя базовой тактовой частоты (тип BASE_CLOCK_DIV_INDEXES_FM_E310)
		DWORD  ClockRate;					// [out]		рабочая тактовая частота счётчика FM в Гц
		DWORD  BaseClockRate;			// [const]	базовая тактовая частота счётчика FM 25 000 000 Гц
		double Offset;						// [in-out]	смещение порога измерителя частоты в В
	};

	// структура с информацией о частоте измеряемого сигнала
	struct FM_SAMPLE_E310
	{
		BOOL	 IsActual;					// [out]	признак действительности получeнных данных
		double Frequency;					// [out]	частота измеряемого сигнала в кГц
		double Period;						// [out]	период измеряемого сигнала в мс
		double DutyCycle;					// [out]	скважность измеряемого сигнала в мс
	};

	// структура для работы с АЦП
	struct ADC_PARS_E310
	{
		BYTE	 AdcStartSource;	  		// [in-out]	источник сигнала запуска АЦП: внутренний или внешний
		BYTE	 ChannelsMask;				// [in-out]	битовая маска активных каналов (младшие 4 бита)
		double InputRange;				// [out]		входной диапазон АЦП в В
	};

	// структура с отсчётами АЦП
	struct ADC_DATA_E310
	{
		SHORT 	DataInCode[ADC_CHANNEL_QUANTITY_E310];		// [out] массив данных с АЦП в кодах
		double	DataInV[ADC_CHANNEL_QUANTITY_E310];			// [out] массив данных с АЦП в В
	};
	#pragma pack()

	//-----------------------------------------------------------------------------
	// интерфейс модуля E-310
	//-----------------------------------------------------------------------------
	struct ILE310 : public ILUSBBASE
	{
		// ---------------- функции для работы с генератором ----------------------------
		// функция чтения параметров работы генератора
		virtual BOOL WINAPI GET_GENERATOR_PARS(GENERATOR_PARS_E310 * const GenPars) = 0;
		// функция установки gараметров работы генератора
		virtual BOOL WINAPI SET_GENERATOR_PARS(GENERATOR_PARS_E310 * const GenPars) = 0;
		// старт работы генератора
		virtual BOOL WINAPI START_GENERATOR(void) = 0;
		// останов работы генератора
		virtual BOOL WINAPI STOP_GENERATOR(void) = 0;

		// ---------- функции для работы с частотометром (FM) -------------------
		// получение текущих параметров работы частотометра (FM)
		virtual BOOL WINAPI GET_FM_PARS(FM_PARS_E310 * const FmPars) = 0;
		// установка требуемых параметров работы частотометра (FM)
		virtual BOOL WINAPI SET_FM_PARS(FM_PARS_E310 * const FmPars) = 0;
		// старт работы частотометра (FM)
		virtual BOOL WINAPI START_FM(void) = 0;
		// останов работы частотометра (FM)
		virtual BOOL WINAPI STOP_FM(void) = 0;
		// считывание отсчета измерения частоты
		virtual BOOL WINAPI FM_SAMPLE(FM_SAMPLE_E310 * const FmSample) = 0;

		// ------------------ функции для работы АЦП ---------------------------
		// получение текущих параметров работы АЦП
		virtual BOOL WINAPI GET_ADC_PARS(ADC_PARS_E310 * const AdcPars) = 0;
		// установка требуемых параметров работы АЦП
		virtual BOOL WINAPI SET_ADC_PARS(ADC_PARS_E310 * const AdcPars) = 0;
		// считывание массива отсчетов с АЦП
		virtual BOOL WINAPI GET_ADC_DATA(ADC_DATA_E310 * const AdcData) = 0;

		// ---------- функции для работы с цифровыми линиями -------------------
		// конфигурирование цифровых линий: вход или выход
		virtual BOOL WINAPI CONFIG_TTL_LINES(WORD Pattern, BOOL AddTtlLinesEna = FALSE) = 0;
		// чтение состояний цифровых входных линий
		virtual BOOL WINAPI TTL_IN (WORD * const TtlIn) = 0;
		// установка состояний цифровых выходных линий
		virtual BOOL WINAPI TTL_OUT(WORD * const TtlOut) = 0;

		// ------- функции для работы с пользовательской информацией ППЗУ  -------
		// разрешение/запрещение режима записи в пользовательскую область ППЗУ
		virtual BOOL WINAPI ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled) = 0;
		// чтенние пользовательской области ППЗУ
		virtual BOOL WINAPI READ_FLASH_ARRAY(USER_FLASH_E310 * const UserFlash) = 0;
		// запись пользовательской области ППЗУ
		virtual BOOL WINAPI WRITE_FLASH_ARRAY(USER_FLASH_E310 * const UserFlash) = 0;

		// ------- функции для работы со служебной информацией из ППЗУ -----------
		// получим служебную информацию о модуле из ППЗУ
		virtual BOOL WINAPI GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E310 * const md) = 0;
		// запишем служебную информацию о модуле в ППЗУ
		virtual BOOL WINAPI SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E310 * const md) = 0;
	};

#endif
