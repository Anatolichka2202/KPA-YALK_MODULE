//---------------------------------------------------------------------------
#ifndef __E310API__
#define __E310API__
//---------------------------------------------------------------------------
	#include "LUsbBase.h"

	//---------------------------------------------------------------------------
	// реализация интерфейса для работы с модулем E-310
	//---------------------------------------------------------------------------
	class TLE310 : public ILE310, public TLUSBBASE
	{
		// ----------------- функции общего назначения --------------------------
		// открывает виртуальный слот для доступа к USB модулю
		virtual BOOL WINAPI OpenLDevice(WORD VirtualSlot);
		// освобождает текущий виртуальный слот
		virtual BOOL WINAPI CloseLDevice(void);
		// освобождает указатель на интерфейс устройства
		virtual BOOL WINAPI ReleaseLInstance(void);
		// получение дескриптора устройства USB
		virtual HANDLE WINAPI GetModuleHandle(void);
		// получение названия используемого модуля
		virtual BOOL WINAPI GetModuleName(char * const ModuleName);
		// получение текущей скорости работы шины USB
		virtual BOOL WINAPI GetUsbSpeed(BYTE * const UsbSpeed);
		// управления режимом низкого электропотребления модуля
		virtual BOOL WINAPI LowPowerMode(BOOL LowPowerFlag);
		// функция выдачи строки с последней ошибкой
		virtual BOOL WINAPI GetLastErrorInfo(LAST_ERROR_INFO_LUSBAPI * const LastErrorInfo);

		// ---------------- функции для работы с генератором ----------------------------
		// функция чтения параметров работы генератора
		virtual BOOL WINAPI GET_GENERATOR_PARS(GENERATOR_PARS_E310 * const GenPars);
		// функция установки gараметров работы DDS модуля
		virtual BOOL WINAPI SET_GENERATOR_PARS(GENERATOR_PARS_E310 * const GenPars);
		// старт работы генератора
		virtual BOOL WINAPI START_GENERATOR(void);
		// останов работы генератора
		virtual BOOL WINAPI STOP_GENERATOR(void);

		// ---------- функции для работы с частотометром (FM) -------------------
		// получение текущих параметров работы частотомера (FM)
		virtual BOOL WINAPI GET_FM_PARS(FM_PARS_E310 * const FmPars);
		// установка требуемых параметров работы частотомера (FM)
		virtual BOOL WINAPI SET_FM_PARS(FM_PARS_E310 * const FmPars);
		// старт работы частотомера (FM)
		virtual BOOL WINAPI START_FM(void);
		// останов работы частотомера (FM)
		virtual BOOL WINAPI STOP_FM(void);
		// считывание отсчета измерения частоты
		virtual BOOL WINAPI FM_SAMPLE(FM_SAMPLE_E310 * const FmSample);

		// ------------------ функции для работы АЦП ---------------------------
		// получение текущих параметров работы АЦП
		virtual BOOL WINAPI GET_ADC_PARS(ADC_PARS_E310 * const AdcPars);
		// установка требуемых параметров работы АЦП
		virtual BOOL WINAPI SET_ADC_PARS(ADC_PARS_E310 * const AdcPars);
		// считывание массива отсчетов с АЦП
		virtual BOOL WINAPI GET_ADC_DATA(ADC_DATA_E310 * const AdcData);

		// ---------- функции для работы с цифровыми линиями -------------------
		// конфигурирование цифровых линий: вход или выход
		virtual BOOL WINAPI CONFIG_TTL_LINES(WORD Pattern, BOOL AddTtlLinesEna);
		// чтение состояний цифровых входных линий
		virtual BOOL WINAPI TTL_IN (WORD * const TtlIn);
		// установка состояний цифровых выходных линий
		virtual BOOL WINAPI TTL_OUT(WORD * const TtlOut);

		// ------- функции для работы с пользовательской информацией ППЗУ  -------
		// разрешение/запрещение режима записи в пользовательскую область ППЗУ
		virtual BOOL WINAPI ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled);
		// чтенние пользовательской области ППЗУ
		virtual BOOL WINAPI READ_FLASH_ARRAY(USER_FLASH_E310 * const UserFlash);
		// запись пользовательской области ППЗУ
		virtual BOOL WINAPI WRITE_FLASH_ARRAY(USER_FLASH_E310 * const UserFlash);

		// ------- функции для работы со служебной информацией из ППЗУ -----------
		// получим служебную информацию о модуле из ППЗУ
		virtual BOOL WINAPI GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E310 * const md);
		// запишем служебную информацию о модуле в ППЗУ
		virtual BOOL WINAPI SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E310 * const md);

		// ----------- функции для прямого досупа к микроконтроллеру -------------
/*		virtual BOOL WINAPI GetArray(BYTE * const Data, WORD Size, WORD Address);
		virtual BOOL WINAPI PutArray(BYTE * const Data, WORD Size, WORD Address);*/

		//
		public:
			// коструктор
			TLE310(HINSTANCE hInst);
			// деструктор
			~TLE310();

		//
		private :
			// выравнивание структур
			#pragma pack(1)

			// структура с информаций о модуле, которая хранится в польовательском ППЗУ
			struct MODULE_DESCRIPTOR
			{
				BYTE ModuleName[16];    		// название модуля
				BYTE SerialNumber[SERIAL_NUMBER_STRING_LENGTH_LUSBAPI];	// серийный номер модуля
				BYTE McuName[NAME_LINE_LENGTH_LUSBAPI];						// название установленного MCU
				BYTE Revision;					    									// ревизия платы
				DWORD ClockRate;														// тактовая частота MCU в Гц
				BYTE  Reserved[252-60];												// зарезервировано
				WORD CRC16;																// контрольная сумма структуры
			};
			// структура с информацией об драйвере микроконтроллера
			struct FIRMWARE_DESCRIPTOR
			{
				BYTE Version[10];										// версия драйвера микроконтроллера
				BYTE Created[14];										// дата сборки драйвера микроконтроллера
				BYTE Manufacturer[NAME_LINE_LENGTH_LUSBAPI];	// производитель драйвера микроконтроллера
				BYTE Author[NAME_LINE_LENGTH_LUSBAPI];		 	// автор драйвера микроконтроллера
				BYTE Comment[128];									// строка комментария
			};
			// структура с информацией об загрузчике микроконтроллера
			struct BOOT_LOADER_DESCRIPTOR
			{
				BYTE Version[10];										// версия загрузчика микроконтроллера
				BYTE Created[14];										// дата сборки загрузчика микроконтроллера
				BYTE Manufacturer[NAME_LINE_LENGTH_LUSBAPI];	// производитель загрузчика микроконтроллера
				BYTE Author[NAME_LINE_LENGTH_LUSBAPI];// автор загрузчика микроконтроллера
//				BYTE Comment[128];									// строка комментария
			};

			// локальная структура с параметрами функционирования 'DDS'
			struct DDS_PARS
			{
				union CONTROL_REG
				{
					struct
					{
						WORD Reserved0					: 2;
						WORD SYNCOUTEN					: 1;
						WORD SYNCSEL					: 1;
						WORD Reserved1					: 1;
						WORD INT_OR_EXT_INCR			: 1;
						WORD Reserved2					: 2;
						WORD MSBOUTEN					: 1;
						WORD SIN_OR_TRI				: 1;
						WORD DAC_ENA					: 1;
						WORD B24 						: 1;
						WORD ADDR 						: 4;
					} BitFields;
					WORD Value;
				} ControlReg;

				union NUMBER_OF_INCREMENTS_REG
				{
					struct
					{
						WORD Value12bit				: 12;
						WORD ADDR 						: 4;
					} BitFields;
					WORD Value;
				} NumberOfIncrementsReg;

				union LOWER_DELTA_FREQUENCY_REG
				{
					struct
					{
						WORD Value12bit				: 12;
						WORD ADDR 						: 4;
					} BitFields;
					WORD Value;
				} LowerDeltaFrequencyReg;

				union HIGHER_DELTA_FREQUENCY_REG
				{
					struct
					{
						WORD Value11bit				: 11;
						WORD Sign						: 1;
						WORD ADDR 						: 4;
					} BitFields;
					WORD Value;
				} HigherDeltaFrequencyReg;

				union INCREMENT_INTERVAL_REG
				{
					struct
					{
						WORD Value11bit				: 11;
						WORD Multiplier				: 2;
						WORD ADDR 						: 3;
					} BitFields;
					WORD Value;
				} IncrementIntervalReg;

				union LOWER_START_FREQUENCY_REG
				{
					struct
					{
						WORD Value12bit				: 12;
						WORD ADDR 						: 4;
					} BitFields;
					WORD Value;
				} LowerStartFrequencyReg;

				union HIGHER_START_FREQUENCY_REG
				{
					struct
					{
						WORD Value12bit				: 12;
						WORD ADDR 						: 4;
					} BitFields;
					WORD Value;
				} HigherStartFrequencyReg;

				union LOWER_STOP_FREQUENCY_REG
				{
					struct
					{
						WORD Value12bit				: 12;
						WORD ADDR 						: 4;
					} BitFields;
					WORD Value;
				} LowerStopFrequencyReg;

				union HIGHER_STOP_FREQUENCY_REG
				{
					struct
					{
						WORD Value12bit				: 12;
						WORD ADDR 						: 4;
					} BitFields;
					WORD Value;
				} HigherStopFrequencyReg;

				// тип автосканирования частоты: пила, треугольник и т.д.
				BYTE AutoScanType;
				// величина тактирующего сигнала генератора в кГц
				double MasterClock;
				//
				BYTE Reserved[15];
			};

			// локальное объединение с параметрами 'Контрольного Pегистра'
			union CONTROL_REG
			{
				struct
				{
					BYTE DDS_CTRL_SOURCE 					: 1;
					BYTE DDS_INTERRUPT_SOURCE				: 1;
					BYTE DDS_MCLK_SOURCE						: 1;
					BYTE DDS_10OHM_SIGNAL_OFFSET_SOURCE	: 1;
					BYTE DDS_SIGNAL_GAIN1					: 2;
					BYTE DDS_SIGNAL_GAIN2					: 2;
				} BitFields;
				BYTE Value;
			};

			// локальная структура 'Цифровых Потенциометров' модуля E-310
			struct DIGITAL_POTENTIOMETERS_PARS
			{
				// внутренне смещение сигнала с 10 ОМ выхода генератора: от -4B до + 4В
				union OUTPUT_10OHM_OFFSET
				{
					struct
					{
						WORD Value8bit				: 8;
						WORD ADDR					: 1;
					} BitFields;
					WORD Value;
				} Output10OhmOffset;

				// смещение порогового уровня частотомера: от -4B до + 4В
				union FREQUENCY_METER_POROG
				{
					struct
					{
						WORD Value8bit				: 8;
						WORD ADDR					: 1;
					} BitFields;
					WORD Value;
				} FrequencyMeterPorog;
			};

			// структура с параметрами работы измерителя частоты (FM)
			struct FM_PARS
			{
				BYTE	FmEna;					   // текущее состояние работы измерителя частоты: работает или нет
				BYTE  Mode;							// режим работы измерителя частоты
				BYTE  InputDivider;				// управление входным делителем 1/8
				BYTE	BaseClockRateDivIndex;	// индекс делителя тактовой частоты
				DWORD ClockRate;					// тактовая частота счётчика в Гц
				DWORD BaseClockRate;				// базовая тактовая частота счётчика в Гц
				BYTE  Reserved[21];				// зарезервировано
			};

			// структура с параметрами работы модуля
			struct MODULE_PARS
			{
				DDS_PARS Dds;
				CONTROL_REG ControlReg;
				DIGITAL_POTENTIOMETERS_PARS DigitalPotentiometers;
				FM_PARS FrequencyMeasurement;
			};

			// структура с отсчетом измерения частоты входного сигнала
			struct FM_DATA
			{
				BYTE	IsActual;					// [out]	признак действительности получнных данных
				DWORD PeriodCode;					// [out]	период измеряемого сигнала в тактах счётчика
				DWORD DutyCycleCode;				// [out]	скважность измеряемого сигнала в тактах счётчика
				BYTE  Reserved[23];				// зарезервировано
			};

			// объединение с параметрами конфигурации цифровых линий
			union TTL_CONFIG
			{
				struct
				{
					WORD CONFIG_PATTERN				: 11;
					WORD RESERVED						: 4;
					WORD ADD_TTL_LINES_ENA			: 1;
				} BitFields;
				WORD Value;
			};

			// структура с параметрами цифровых линий
			struct TTL_PARS
			{
				BYTE 			Mode;					// что является активным: конфигурация, чтение или запись
				TTL_CONFIG	TtlConfig;		  	// конфигурации цифровых линий: вход или выход
				WORD			TtlIn;				// текущее состояний цифровых входных линий
				WORD			TtlOut;				// текущее состояний цифровых выходных линий
				BYTE			Reserved[0x9];		// зарезервировано
			};

			// структура c параметрами работы АЦП
			struct ADC_PARS
			{
				BYTE	AdcStartSource;	  		// источник сигнала запуска АЦП: внутренний или внешний
				BYTE	ChannelsMask;				// битовая маска активных каналов (младшие 4 бита)
				BYTE	Reserved[0x5];				// зарезервировано
			};

			// структура c данными АЦП
			struct ADC_DATA
			{
				SHORT DataInCode[ADC_CHANNEL_QUANTITY_E310];	// массив данных с АЦП в кодах
			};
			#pragma pack()

			// -=== локальные функции ===-
			// инициализация всех необходимых переменных
		   BOOL WINAPI InitVars(void);
			// состояние аналогового питания узлов модуля при переходе шины USB в режим suspend
			BOOL SetSuspendModeFlag(BOOL SuspendModeFlag);
			// функции для прямого досупа к микроконтроллеру -------------
			BOOL GetArray(BYTE * const Data, DWORD Size, DWORD Address);
			BOOL PutArray(BYTE * const Data, DWORD Size, DWORD Address);
			// чтение дескриптора модуля
			BOOL GetModuleDescriptor(void);
			// сохранение дескриптора модуля
			BOOL SaveModuleDescriptor(void);
			// функция запуска режима работы микроконтроллера
			BOOL RunMcuApplication(DWORD Address, WORD BitParam = NO_SWITCH_PARAMS);
			// -=========================-


			// -=== локальные переменные ===-
			// режим работы микроконтроллера: BootLoader или Application
			BOOL IsMcuApplicationActive;
			// флажок разрешения глобальной записи в ППЗУ
			BOOL IsUserFlashWriteEnabled;
			// структура c системной информацией модуля
			MODULE_DESCRIPTOR ModuleDescriptor;
			// структура с информацией о драйвере микроконтроллера
			FIRMWARE_DESCRIPTOR FirmwareDescriptor;
			// структура с информацией об загрузчике микроконтроллера
			BOOT_LOADER_DESCRIPTOR BootLoaderDescriptor;
			// локальная структура всех параметров работы модуля E-310
			MODULE_PARS ModulePars;
			// локальная структура с отсчетом измерения частоты входного сигнала
			FM_DATA LocFmSample;
			// структура с параметрами работы модуля
			TTL_PARS TtlPars;
			// структура c параметрами работы АЦП
			ADC_PARS LocAdcPars;
			// структура c данными АЦП
			ADC_DATA LocAdcData;
			// флажки занятости генератора и измерителя частот
			BOOL IsGeneratorBusy, IsFmBusy;
			// номер версии Firmware микроконтроллера
			double FirmwareVersionNumber;
			// величина внутреннего тактирующего сигнала генератора в кГц
			double INTERNAL_DDS_MASTER_CLOCK;
			// входной диапазон АЦП в В
			double ADC_INPUT_RANGE;
			// массив усилений выходного тракта генератора в дБ
			double GeneratorGaindBArray[ANALOG_OUTPUT_GAINS_QUANTITY_E310];
			// массив доступных аплитуд на 10 Ом выхода в В
			double Output10OhmVArray[ANALOG_OUTPUT_GAINS_QUANTITY_E310];
			// массив доступных амплитуд на выходе 10 Ом в дБ
			double Output10OhmdBArray[ANALOG_OUTPUT_GAINS_QUANTITY_E310];
			// массив доступных аплитуд на 50 Ом выхода в В
			double Output50OhmVArray[ANALOG_OUTPUT_GAINS_QUANTITY_E310];
			// массив доступных амплитуд на выходе 50 Ом в дБ
			double Output50OhmdBArray[ANALOG_OUTPUT_GAINS_QUANTITY_E310];
			// массив умножителей интервалов приращения для микросхемы DDS
			double DdsMultiplierArray[0x4];
			// максимальный код частоты DDS
			DWORD MAX_FREQ_DDS_CODE;
			// массив соотвествия индексов усиления битам
			// DDS_SIGNAL_GAIN1 и DDS_SIGNAL_GAIN1 в CONTROL_REG
			BYTE GeneratorGainIndexes[0x4][0x4];
			// индексы усиления
			BYTE Gain1Index[ANALOG_OUTPUT_GAINS_QUANTITY_E310], Gain2Index[ANALOG_OUTPUT_GAINS_QUANTITY_E310];
			// -============================-

			// адреса регистров DDS
			enum DDS_REGS_ADDR
			{
				CONTROL_BITS_ADDR_REG					= 0x0,
				NUMBER_OF_INCREMENTS_ADDR_REG			= 0x1,
				LOWER_DELTA_FREQUENCY_ADDR_REG		= 0x2,
				HIGHER_DELTA_FREQUENCY_ADDR_REG		= 0x3,
				CYCLE_INCREMENT_INTERVAL_ADDR_REG	= 0x4 >> 1,
				CLOCK_INCREMENT_INTERVAL_ADDR_REG	= 0x6 >> 1,
				LOWER_START_FREQUENCY_ADDR_REG		= 0xC,
				HIGHER_START_FREQUENCY_ADDR_REG		= 0xD
			};

			// индексы функциональных узлов модуля для запуска работы
			enum MODULE_START_MODE_INDEXES
			{
				GENERATOR_START_INDEX, FM_START_INDEX, INVALID_START_INDEX
			};
			// режимы работы со структурой TTL_PARS
			enum
			{
				NO_TLL_LINES_MODE, TLL_LINES_CONFIG, TTL_LINES_IN, TTL_LINES_OUT, INVALID_TTL_LINES_MODE
			};

			// различные адреса в памяти микроконтроллера
			enum
			{
				//
				BOOT_LOADER_START_ADDRESS 			= (0x00000000UL),		// условный адрес режима 'Загрузчика' (BootLoader)
				BOOT_LOADER_DESCRIPTOR_ADDRESS	= (0x00102E00UL),
				MODULE_DESCRIPTOR_ADDRESS			= (0x00103000UL),
				USER_FLASH_ADDRESS					= (0x00103100UL),
				FIRMWARE_DESCRIPTOR_ADDRESS		= (0x00103300UL),
				FIRMWARE_START_ADDRESS				= (0x00103500UL),
				//
				MODULE_PARS_ADDRESS					= (0x00201400UL),
				FM_SAMPLE_ADDRESS						= (0x00201480UL),
				TTL_PARS_ADDRESS						= (0x002014A0UL),
				ADC_PARS_ADDRESS						= (0x002014B0UL),
				ADC_DATA_ADDRESS						= (0x002014B8UL)
			};

			// номера доступных пользовательских запросов для USB (vendor request)
			enum USB_VENDOR_REQUEST
			{
					//
					V_PUT_ARRAY      		= 0x01,
					V_GET_ARRAY				= 0x02,
					//
					V_START_ADC	 			= 0x04,
					V_STOP_ADC				= 0x05,
					//
					V_CALL_APPLICATION  	= 0x0F
			};
			// различные битовые параметры, используемых при переключении режима
			// работы программы микроконтроллера: "Загрузчик" или "Приложение'
			enum
			{
				NO_SWITCH_PARAMS						= 0x0,	// нет параметров
				REINIT_SWITCH_PARAM					= 0x1		// при переходе в режим "Приложение" происходит
																		// полная переинициализация модуля E20-10
			};
			// различные константы
			enum
			{
				INVALID_PLD_FIRMWARE_VERSION = 0xFFFFFFFF,
				MCU_MASTER_CLOCK = 48000						// в кГц
			};
			// максимальный размер блока данных, который можно однократно
			// передавать по контольному каналу USB (control pipe)
			enum 	{	MAX_USB_CONTROL_PIPE_BLOCK	= 4096 };
			// --------------------------------------------------------------------
};
#endif

