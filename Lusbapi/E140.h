//---------------------------------------------------------------------------
#ifndef __E140API__
#define __E140API__
//---------------------------------------------------------------------------
	#include "LUsbBase.h"

	//---------------------------------------------------------------------------
	// реализация интерфейса для работы с модулем E14-140
	//---------------------------------------------------------------------------
	class TLE140 : public ILE140, public TLUSBBASE
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

		// ---------------- функции для работы с АЦП ----------------------------
		// получение текущих параметров работы АЦП
		virtual BOOL WINAPI GET_ADC_PARS(ADC_PARS_E140 * const AdcPars);
		// заполнение требуемых параметров работы АЦП
		virtual BOOL WINAPI SET_ADC_PARS(ADC_PARS_E140 * const AdcPars);
		// старт работы АЦП
		virtual BOOL WINAPI START_ADC(void);
		// стоп работы АЦП
		virtual BOOL WINAPI STOP_ADC(void);
		// ввод кадра отсчетов с АЦП модуля
		virtual BOOL WINAPI ADC_KADR(SHORT * const Data);
		// однократный ввод с заданного логического канала АЦП модуля
		virtual BOOL WINAPI ADC_SAMPLE(SHORT * const Data, WORD Channel);
		// потоковое чтение данных с АЦП модуля
		virtual BOOL WINAPI ReadData(IO_REQUEST_LUSBAPI * const ReadRequest);

		// ------------------- функции для работы с ЦАП --------------------------
		// получение текущих параметров работы ЦАП
		virtual BOOL WINAPI GET_DAC_PARS(DAC_PARS_E140 * const DacPars);
		// заполнение требуемых параметров работы ЦАП
		virtual BOOL WINAPI SET_DAC_PARS(DAC_PARS_E140 * const DacPars);
		// запуск потоковый работы ЦАП
		virtual BOOL WINAPI START_DAC(void);
		// останов потоковый работы ЦАП
		virtual BOOL WINAPI STOP_DAC(void);
		// потоковая передача данных ЦАП в модуль
		virtual BOOL WINAPI WriteData(IO_REQUEST_LUSBAPI * const WriteRequest);
		// однократный вывод на заданный канал ЦАП
		virtual BOOL WINAPI DAC_SAMPLE(SHORT * const DacData, WORD DacChannel);
		// однократный вывод сразу на оба канала ЦАП
		virtual BOOL WINAPI DAC_SAMPLES(SHORT * const DacData1, SHORT * const DacData2);

		// --------------- функции для работы с ТТЛ линиями ----------------------
		// функция разрешения выходных линий внешнего цифрового разъёма
		virtual BOOL WINAPI ENABLE_TTL_OUT(BOOL EnableTtlOut);
		// функция чтения входных линии внешнего цифрового разъёма
		virtual BOOL WINAPI TTL_IN(WORD * const TtlIn);
		// функция вывода на выходные линии внешнего цифрового разъёма
		virtual BOOL WINAPI TTL_OUT(WORD TtlOut);

		// ------- функции для работы с пользовательской информацией ППЗУ  -------
		// разрешение/запрещение режима записи в пользовательскую область ППЗУ
		virtual BOOL WINAPI ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled);
		// чтенние пользовательской области ППЗУ
		virtual BOOL WINAPI READ_FLASH_ARRAY(USER_FLASH_E140 * const UserFlash);
		// запись пользовательской области ППЗУ
		virtual BOOL WINAPI WRITE_FLASH_ARRAY(USER_FLASH_E140 * const UserFlash);

		// ------- функции для работы со служебной информацией из ППЗУ -----------
		// получим служебную информацию о модуле из ППЗУ
		virtual BOOL WINAPI GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E140 * const md);
		// запишем служебную информацию о модуле в ППЗУ
		virtual BOOL WINAPI SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E140 * const md);

		// ----------- функции для прямого досупа к микроконтроллеру -------------
		virtual BOOL WINAPI GetArray(BYTE * const Buffer, WORD Size, WORD Address);
		virtual BOOL WINAPI PutArray(BYTE * const Buffer, WORD Size, WORD Address);

		//
		public:
			// коструктор
			TLE140(HINSTANCE hInst);
			// деструктор
			~TLE140();

		//
		private :
			// выравнивание структур
			#pragma pack(1)
			// локальная структура, содержащая информацию о плате, которая хранится в поль. ППЗУ
			struct MODULE_DESCRIPTOR
			{
				BYTE Name[11];				    		// название модуля
				BYTE SerialNumber[9];    			// серийный номер
				BYTE Revision;					    	// ревизия платы
				BYTE CpuType[11];						// тип установленного MCU
				DWORD ClockRate;						// тактовая частота в Гц
				BYTE IsDacPresented;					// флажок наличия ЦАП
				double AdcOffsetCoefs[ADC_CALIBR_COEFS_QUANTITY_E140];	// смещение	АЦП: 4диапазона
				double AdcScaleCoefs[ADC_CALIBR_COEFS_QUANTITY_E140];		// масштаб АЦП	: 4диапазона
				double DacOffsetCoefs[DAC_CALIBR_COEFS_QUANTITY_E140];	// смещение	ЦАП: 2канала x 1диапазон
				double DacScaleCoefs[DAC_CALIBR_COEFS_QUANTITY_E140];		// масштаб ЦАП	: 2канала x 1диапазон
			};
       	// локальная структура, задающая режим потоковой работы ЦАП
			struct DAC_PARS
			{
				WORD DacRateDiv; 			  			// Делитель частоты, 0..7, f = 200/(RateDiv+1) кГц
				BYTE SyncWithADC;	  					// 0 = обычный пуск ЦАП; !0 = синхронизировать с пуском АЦП
				BYTE SetZeroOnStop;					// !0 = при остановке ЦАП установить на выходе 0 В
            BYTE Reserved[12];					// зарезервировано
			};

			//
			#pragma pack()

			// флажок разрешения глобальной записи в ППЗУ
			BOOL IsUserFlashWriteEnabled;
			// флажок управления записью в служебную ообласть ППЗУ модуля
			BOOL EnableSystemFlashWrite;
			// структура системного ППЗУ для модуля E-140
			MODULE_DESCRIPTOR ModuleDescriptor;
			// локальная структура, задающая режим потоковой работы ЦАП
			DAC_PARS DacPars;
			// локальная структура с параметрами функционирования АЦП
			ADC_PARS_E140 AdcPars;
			// флажок сбора данных
			BOOL IsDataAcquisitionInProgress;
			// номер версии Firmware микроконтроллера
			double FirmwareVersionNumber;

			// базовый номер ошибки библиотеки при работе с модулем E14-140
			enum 	{	ERROR_ID_BASE				= 300 };
			// максимальный размер блока данных, который можно однократно
			// передавать по контольному каналу USB (control pipe)
			enum 	{	MAX_USB_CONTROL_PIPE_BLOCK	= 4096 };
			// константы
			enum
			{
				ADC_PARS_BASE						= 0x0060,
				ADC_ONCE_FLAG						= (ADC_PARS_BASE + 136),
				ENABLE_FLASH_WRITE_FLAG	 		= (ADC_PARS_BASE + 137),
				DAC_PARS_BASE						= 0x0160,
				DOUT_REGISTER						= 0x0400,
				DIN_REGISTER						= 0x0400,
				DOUT_ENABLE_REGISTER		 		= 0x0402,
				ADC_DATA_REGISTER			 		= 0x0410,
				ADC_CHANNEL_SELECT_REGISTER	= 0x0412,
				ADC_START_REGISTER				= 0x0413,
				DAC_SAMPLE_REGISTER				= 0x0420,
				DAC_SAMPLES_REGISTER				= 0x0428,
				SUSPEND_MODE_FLAG					= 0x0430,
				USER_FLASH_ADDRESS 				= 0x0800,
				FIRMWARE_VERSION					= 0x1080,
				DESCRIPTOR_BASE					= 0x2780,
				RAM									= 0x8000
			};

			// номера доступных пользовательских запросов для USB (vendor request)
			enum
			{
					V_PUT_ARRAY      	= 0x0001,
					V_GET_ARRAY			= 0x0002,
					V_START_ADC	 		= 0x0003,
					V_STOP_ADC			= 0x0004,
					V_START_ADC_ONCE	= 0x0005,
					V_START_DAC			= 0x0006,
               V_STOP_DAC 			= 0x0007,               
					V_GET_MODULE_NAME	= 0x000B
			};
			// --------------------------------------------------------------------

			// инициализация всех необходимых переменных
		   BOOL InitVars(void);
			// состояние аналогового питания узлов модуля при переходе шины USB в режим suspend
			BOOL SetSuspendModeFlag(BOOL SuspendModeFlag);
			// функции чтения/записи адресного пространства модуля, в том числе в/из памяти MCU
			BOOL GetArrayFromMcu(BYTE * const Buffer, WORD Size, WORD Address);
			BOOL PutArrayToMcu(BYTE * const Buffer, WORD Size, WORD Address);
			// функции подсчета контрольной суммы
			BYTE CRC8ADD(BYTE a, BYTE b);
			BYTE CRC8CALC(BYTE *Buffer, WORD Size);
			// функции паковки и распаковки дескриптора модуля         
			BOOL PackModuleDescriptor(BYTE * const Buffer, MODULE_DESCRIPTOR * const ModuleDescriptor);
			BOOL UnpackModuleDescriptor(BYTE * const Buffer, MODULE_DESCRIPTOR * const ModuleDescriptor);
};
#endif

