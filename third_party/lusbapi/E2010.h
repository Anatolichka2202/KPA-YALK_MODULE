//------------------------------------------------------------------------------
#ifndef __E2010H__
#define __E2010H__

	#include "LUsbBase.h"

	//---------------------------------------------------------------------------
	// реализация интерфейса для работы с модулем E20-10
	//---------------------------------------------------------------------------
	class TLE2010 : public ILE2010, public TLUSBBASE
	{
		// функции общего назначения для работы с USB устройствами
		virtual BOOL WINAPI OpenLDevice(WORD VirtualSlot);
		virtual BOOL WINAPI CloseLDevice(void);
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

		// загрузка и проверка ПЛИС
		virtual BOOL WINAPI LOAD_MODULE(const char * const FileName);
		virtual BOOL WINAPI TEST_MODULE(WORD TestModeMask);

		// работа с АЦП
		virtual BOOL WINAPI GET_ADC_PARS(ADC_PARS_E2010 * const AdcPars);
		virtual BOOL WINAPI SET_ADC_PARS(ADC_PARS_E2010 * const AdcPars);
		virtual BOOL WINAPI START_ADC(void);
		virtual BOOL WINAPI STOP_ADC(void);
		virtual BOOL WINAPI GET_DATA_STATE(DATA_STATE_E2010 * const DataState);
		virtual BOOL WINAPI ReadData(IO_REQUEST_LUSBAPI * const ReadRequest);

		// работа с ЦАП
		virtual BOOL WINAPI DAC_SAMPLE(SHORT * const DacData, WORD DacChannel);

		// работа с цифровыми линиями
		virtual BOOL WINAPI ENABLE_TTL_OUT(BOOL EnableTtlOut);
		virtual BOOL WINAPI TTL_IN (WORD * const TtlIn);
		virtual BOOL WINAPI TTL_OUT(WORD TtlOut);

		// функции для работы с пользовательской информацией ППЗУ
		virtual BOOL WINAPI ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled);
		virtual BOOL WINAPI READ_FLASH_ARRAY(USER_FLASH_E2010 * const UserFlash);
		virtual BOOL WINAPI WRITE_FLASH_ARRAY(USER_FLASH_E2010 * const UserFlash);

		// информация о модуле
		virtual BOOL WINAPI GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E2010 * const ModuleDescription);
		virtual BOOL WINAPI SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E2010 * const ModuleDescription);

		// получение отладочной информации
//		virtual BOOL WINAPI GET_DEBUG_INFO(WORD * const DebugInfo);

		public :
			// конструктор/деструктор
			TLE2010(HINSTANCE hInst);
			virtual ~TLE2010();

		private:
			#pragma pack(1)
			// локальная структура с описанием модуля
			struct MODULE_DESCRIPTOR
			{
				BYTE ModuleName[16];	 		// название модуля
				BYTE SerialNumber[16];		// серийный номер модуля
				BYTE McuType[16];				// тип используемого микроконтроллера
				DWORD ClockRate;				// тактовая частота работы микроконтроллера в Гц
				BYTE Revision;					// ревизия модуля (латинская литера)
				BYTE IsDacPresented;			// признак наличия ЦАП на борту
				float AdcOffsetCoefs[ADC_CALIBR_COEFS_QUANTITY_E2010];	// смещение	АЦП: 4канала x 3диапазона
				float AdcScaleCoefs[ADC_CALIBR_COEFS_QUANTITY_E2010];		// масштаб  АЦП: 4канала x 3диапазона
				float DacOffsetCoefs[DAC_CALIBR_COEFS_QUANTITY_E2010];	// смещение	ЦАП: 2канала x 1диапазон
				float DacScaleCoefs[DAC_CALIBR_COEFS_QUANTITY_E2010];		// масштаб  ЦАП: 2канала x 1диапазон
				BYTE Modification;			// исполнение модуля (число);
				BYTE Reserved[254-167];		// зарезервировано
				WORD CRC16;						// контрольная сумма структуры
			};
			// структура с информацией об драйвере микроконтроллера
			struct FIRMWARE_DESCRIPTOR
			{
				BYTE McuName[NAME_LINE_LENGTH_LUSBAPI]; 		// название микроконтроллера
				BYTE Version[10];										// версия драйвера микроконтроллера
				BYTE Created[14];										// дата сборки драйвера микроконтроллера
				BYTE Manufacturer[NAME_LINE_LENGTH_LUSBAPI];	// производитель драйвера микроконтроллера
				BYTE Author[NAME_LINE_LENGTH_LUSBAPI];		 	// автор драйвера микроконтроллера
				BYTE Comment[128];									// строка комментария
			};
			// структура с информацией об загрузчике микроконтроллера
			struct BOOT_LOADER_DESCRIPTOR
			{
				BYTE McuName[20]; 							 		// название микроконтроллера
				BYTE Version[10];										// версия загрузчика микроконтроллера
				BYTE Created[14];										// дата сборки загрузчика микроконтроллера
				BYTE Manufacturer[15];								// производитель загрузчика микроконтроллера
				BYTE Author[20];		 								// автор загрузчика микроконтроллера
//				BYTE Comment[128];									// строка комментария
			};
			// структура с дополнительными параметрами синхронизации ввода данных
			struct EXTRA_SYNCHRO_PARS
			{
				DWORD StartDelay; 					// задержка старта сбора данных в кадрах отсчётов АЦП
				DWORD StopAfterNKadrs;				// останов сбора данных после задаваемого здесь кол-ва собранных кадров отсчётов АЦП
				WORD	SynchroAdMode;					// параметры аналоговой сихронизации: разрешение, канал, переход или уровень
				SHORT SynchroAdPorog;				// порог срабатывания АЦП при аналоговой синхронизации
				BYTE IsBlockDataMarkerEnabled;	// маркирование блока данных (например, при аналоговой синхронизации ввода по уровню)
			};
			// структура с информацией о текущем состоянии процесса сбора данных
			struct DATA_STATUS
			{
				BYTE Status;
				DWORD CurBufferFilling;
				DWORD MaxOfBufferFilling;
				DWORD BufferSize;
			};
			union PLD_VERSION
			{
				struct
				{
					BYTE LowNumber;
					BYTE MiddleNumber;
					BYTE HighNumber;
					BYTE Reserved;
				} BYTES;
				DWORD Value;
			};
			#pragma pack()

			// инициализация всех необходимых переменных
		   BOOL WINAPI InitVars(void);
			// сброс микросхемы ПЛИС
			BOOL WINAPI ResetPld(void);
			// запуск загруженной микросхемы ПЛИС
			BOOL WINAPI StartPld(void);
			// загрузка микросхемы ПЛИС
			BOOL WINAPI LoadPld(BYTE * const PldData, DWORD NBytes);
			// разбор текущего режима работы модуля         
			void WINAPI GetSynchroParsForRevA(BYTE ModeParam, ADC_PARS_E2010 * const AdcPars);
			void WINAPI GetSynchroParsForRevBandAbove(BYTE ModeParam, ADC_PARS_E2010 * const AdcPars);
			// чтение дополнительных параметров синхронизации сбора данных
			BOOL WINAPI GetExtraSynchroParsForRevBandAbove(ADC_PARS_E2010 * const AdcPars);
			// формирование режима работы модуля
			void WINAPI MakeSynchroParsForRevA(BYTE &ModeParam, ADC_PARS_E2010 * const AdcPars);
			void WINAPI MakeSynchroParsForRevBandAbove(BYTE &ModeParam, ADC_PARS_E2010 * const AdcPars);
			// дополнительные параметры синхронизации сбора данных
			BOOL WINAPI SetExtraSynchroParsForRevBandAbove(ADC_PARS_E2010 * const AdcPars);
			// чтение дескриптора модуля
			BOOL WINAPI GetModuleDescriptor(MODULE_DESCRIPTOR const * ModuleDescriptor);
			// попробуем определить номер версии прошивки ПЛИС
			BOOL WINAPI GetPldVersionNumber(double * const fPVN, DWORD * const dwPVN);
			// функции доступа к памяти микроконтроллера
			BOOL WINAPI GetArray(DWORD Address, BYTE * const Data, DWORD Size);
			BOOL WINAPI PutArray(DWORD Address, BYTE * const Data, DWORD Size);
			// сохранение дескриптора модуля
			BOOL WINAPI SaveModuleDescriptor(void);
			// перевод модуля в соотвествующие тестовые режимы
			BOOL WINAPI SetTestMode(WORD TestModeMask);
			// функция запуска режима работы микроконтроллера
			BOOL WINAPI RunMcuApplication(DWORD Address, WORD BitParam = NO_SWITCH_PARAMS);
			//         
			BOOL WINAPI GET_DEBUG_INFO(WORD * const DebugInfo);

			// размеры внутренних структур и массивов
			enum
			{
				PLD_INFO_SIZE						= (2*256),
				ADC_PARAM_SIZE						= (3+2+2+256),
				EXTENDED_ADC_PARAM_SIZE			= (3+2+2+MAX_EXTENDED_CONTROL_TABLE_LENGTH_E2010),
				HALF_EXTENDED_ADC_PARAM_SIZE	= (3+2+2+MAX_EXTENDED_CONTROL_TABLE_LENGTH_E2010/2),
				ADC_CALIBR_COEFS_QUANTITY		= (2*ADC_CALIBR_COEFS_QUANTITY_E2010)
			};

			// структура c системной информацией модуля
			MODULE_DESCRIPTOR ModuleDescriptor;
			// структура с информацией о драйвере микроконтроллера
			FIRMWARE_DESCRIPTOR FirmwareDescriptor;
			// структура с информацией об загрузчике микроконтроллера
			BOOT_LOADER_DESCRIPTOR BootLoaderDescriptor;
			// структура с дополнительными параметрами синхронизации ввода данных
			EXTRA_SYNCHRO_PARS ExtraSynchroPars;
			// структура с информацией о текущем состоянии процесса сбора данных
			DATA_STATUS LocDataStatus;
			// внутренние структуры и массивы
			BYTE PldInfoString[PLD_INFO_SIZE];  	// информация об используемом ПЛИС
			BYTE AdcParamsArray[EXTENDED_ADC_PARAM_SIZE];			// локальная структура для работы с параметрами АЦП
			WORD AdcCalibrCoefsArray[ADC_CALIBR_COEFS_QUANTITY];	// локальная структура для работы корректировочными коэффициентами АЦП
			WORD MaxControlTableLength;				// максимально возможное кол-во каналов в управляющей таблице
			WORD AdcParamSize;							// реально используемый размер массива AdcParamsArray
			BOOL IsMcuApplicationActive;				// режим работы микроконтроллера
			BOOL IsDataAcquisitionInProgress;		// флажок сбора данных
			BOOL IsUserFlashWriteEnabled;				// флажок разрешения записи в пользователькое ППЗУ
			BOOL IsExtendedControlTableEnabled;		// индикатор возможности использования расширенной управляющей таблицы (до 8192 элементов)
			double FirmwareVersionNumber;				// номер версии Firmware микроконтроллера
			double	fPldVersionNumber;				// номер версии прошивки ПЛИС в виде float
			DWORD		dwPldVersionNumber;				// номер версии прошивки ПЛИС в виде DWORD

			// пользовательские запросы USB
			enum
			{
				V_RESET_PLD        	= 0,
				V_PUT_ARRAY         	= 1,
				V_GET_ARRAY         	= 2,
				V_START_PLD         	= 3,
				V_START_ADC         	= 4,
				V_STOP_ADC          	= 5,
				V_TEST_PLD         	= 7,
				V_CALL_APPLICATION  	= 15
			};
			// размер используемой микросхемы ПЛИС
			enum	{	EP1K10_SIZE		= ( 22*1024 + 100) };
			// максимальный размер блока данных, который можно однократно
			// передавать по контольному каналу USB (control pipe)
			enum 	{	MAX_USB_CONTROL_PIPE_BLOCK	= 4096 };
			// некоторые максимальные размеры
			enum
			{
				MAX_START_DELAY			= (16777214),
				MAX_STOP_AFTER_NKADRS	= (16777215)
			};

			// виртуальные адреса для доступа к различным ресурсам модуля
			enum
			{
				SEL_TEST_MODE			= (0x81000000L),
				SEL_AVR_DM				= (0x82000000L),
				SEL_AVR_PM				= (0x83000000L),
				SEL_DIO_PARAM			= (0x84000000L),
				SEL_DIO_DATA		  	= (0x85000000L),
				SEL_ADC_PARAM		  	= (0x86000000L),
				SEL_EXT_ADC_PARAM	  	= (0x87000000UL),
				SEL_BULK_REQ_SIZE	  	= (0x88000000L),
				SEL_DAC_DATA		  	= (0x89000000L),
				SEL_ADC_CALIBR_KOEFS	= (0x8A000000L),
				SEL_PLD_DATA		  	= (0x8B000000UL),	// данные для заливки в ПЛИС
				SEL_MODULE_TG_DATA	= (0x8C000000UL),	// признак Temperature Grade у железа модуля (с версии прошивки ПЛИС 02.0x.12)
				SEL_PLD_TG_DATA		= (0x8D000000UL),	// признак Temperature Grade у прошивки ПЛИС'а (с версии прошивки ПЛИС 02.0x.12)
				SEL_DIG_RATE		  	= (0x8E000000UL),	// делитель частоты цифрового ввода данных (с версии прошивки ПЛИС 02.0x.13)
				SEL_PLD_INFO			= (0x8F000000UL)	// чтение строки PLD Info (для Rev.'C')
			};

			// различные адреса памяти программ микроконтроллера
			enum
			{
				FIRMWARE_START_ADDRESS				= (SEL_AVR_PM | 0x0000L),
				USER_FLASH_ADDRESS					= (SEL_AVR_PM | 0x2D00L),
				FIRMWARE_DESCRIPTOR_ADDRESS		= (SEL_AVR_PM | 0x2F00L),
				MODULE_DESCRIPTOR_ADDRESS			= (SEL_AVR_PM | 0x3000L),
				BOOT_LOADER_START_ADDRESS 			= (SEL_AVR_PM | 0x3C00L),
				BOOT_LOADER_DESCRIPTOR_ADDRESS	= (SEL_AVR_PM | 0x3FB0L)
			};
			// различные адреса переменных в памяти данных микроконтроллера
			enum
			{
				DATA_STATE_ADDRESS					= (SEL_AVR_DM | (0x0150L + 0x00L)),
				EXTRA_SYNCHRO_PARS_ADDRESS			= (SEL_AVR_DM | (0x0150L + sizeof(DATA_STATUS))),
				ADC_CORRECTION_ADDRESS				= (SEL_AVR_DM | (0x0150L + sizeof(DATA_STATUS) + sizeof(EXTRA_SYNCHRO_PARS))),
				LUSBAPI_OR_LCOMP_ADDRESS			= (SEL_AVR_DM | (0x0150L + sizeof(DATA_STATUS) + sizeof(EXTRA_SYNCHRO_PARS) + 0x1))
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
				MASTER_QUARTZ = 60000							// в кГц					            
			};
			// индексы битов режима аналоговой синхронизации
			enum
			{
				AD_CH0, AD_CH1,  
				AD_M0, AD_M1,
				AD_ENA = 7
			};
	};

#endif

