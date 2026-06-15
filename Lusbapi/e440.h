//---------------------------------------------------------------------------
#ifndef __E440APIH
#define __E440APIH
//---------------------------------------------------------------------------
	#include "LUsbBase.h"

	//---------------------------------------------------------------------------
	// реализация интерфейса для работы с модулем E14-440
	//---------------------------------------------------------------------------
	class TLE440 : public ILE440, public TLUSBBASE
	{
		// функции общего назначения
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

		// функции работы с DSP
		virtual BOOL WINAPI RESET_MODULE(BYTE ResetFlag);
		virtual BOOL WINAPI LOAD_MODULE(const char * const FileName);
		virtual BOOL WINAPI TEST_MODULE(void);
		virtual BOOL WINAPI SEND_COMMAND(WORD Command);

		// функции для работы с АЦП
		virtual BOOL WINAPI GET_ADC_PARS(ADC_PARS_E440 * const AdcPars);
		virtual BOOL WINAPI SET_ADC_PARS(ADC_PARS_E440 * const AdcPars);
		virtual BOOL WINAPI START_ADC(void);
		virtual BOOL WINAPI STOP_ADC(void);
		virtual BOOL WINAPI ADC_KADR(SHORT * const Data);
		virtual BOOL WINAPI ADC_SAMPLE(SHORT * const AdcData, WORD AdcChannel);
		virtual BOOL WINAPI ReadData(IO_REQUEST_LUSBAPI * const ReadRequest);

		// функции для работы с ЦАП
		virtual BOOL WINAPI GET_DAC_PARS(DAC_PARS_E440 * const DacPars);
		virtual BOOL WINAPI SET_DAC_PARS(DAC_PARS_E440 * const DacPars);
		virtual BOOL WINAPI START_DAC(void);
		virtual BOOL WINAPI STOP_DAC(void);
		virtual BOOL WINAPI WriteData(IO_REQUEST_LUSBAPI * const WriteRequest);
		virtual BOOL WINAPI DAC_SAMPLE(SHORT * const DacData, WORD DacChannel);

		// функции для работы с ТТЛ линиями
		virtual BOOL WINAPI ENABLE_TTL_OUT(BOOL EnableTtlOut);
		virtual BOOL WINAPI TTL_IN(WORD * const TtlIn);
		virtual BOOL WINAPI TTL_OUT(WORD TtlOut);

		// функции для работы ППЗУ
		virtual BOOL WINAPI ENABLE_FLASH_WRITE(BOOL EnableFlashWrite);
		virtual BOOL WINAPI READ_FLASH_WORD(WORD FlashAddress, SHORT * const FlashWord);
		virtual BOOL WINAPI WRITE_FLASH_WORD(WORD FlashAddress, SHORT FlashWord);

		// функции для работы со служебной информацией из ППЗУ
		virtual BOOL WINAPI GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E440 * const md);
		virtual BOOL WINAPI SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E440 * const md);

		// функции для работы с памятью DSP
		virtual BOOL WINAPI PUT_LBIOS_WORD(WORD Address, SHORT Data);
		virtual BOOL WINAPI GET_LBIOS_WORD(WORD Address, SHORT * const Data);
		virtual BOOL WINAPI PUT_DM_WORD(WORD Address, SHORT Data);
		virtual BOOL WINAPI GET_DM_WORD(WORD Address, SHORT * const Data);
		virtual BOOL WINAPI PUT_PM_WORD(WORD Address, long Data);
		virtual BOOL WINAPI GET_PM_WORD(WORD Address, long * const Data);
		virtual BOOL WINAPI PUT_DM_ARRAY(WORD BaseAddress, WORD NPoints, SHORT * const Data);
		virtual BOOL WINAPI GET_DM_ARRAY(WORD BaseAddress, WORD NPoints, SHORT * const Data);
		virtual BOOL WINAPI PUT_PM_ARRAY(WORD BaseAddress, WORD NPoints, long * const Data);
		virtual BOOL WINAPI GET_PM_ARRAY(WORD BaseAddress, WORD NPoints, long * const Data);

		// функции для работы с загрузочным ППЗУ
		virtual BOOL WINAPI ERASE_BOOT_FLASH(void);
		virtual BOOL WINAPI PUT_ARRAY_BOOT_FLASH(DWORD BaseAddress, DWORD NBytes, BYTE *Data);
		virtual BOOL WINAPI GET_ARRAY_BOOT_FLASH(DWORD BaseAddress, DWORD NBytes, BYTE *Data);

		public:
			// коструктор
			TLE440(HINSTANCE hInst);
			// деструктор
			~TLE440();

		private:
			// инициализация всех необходимых переменных
		   BOOL InitVars(void);
			// пробуем загрузить DSP модуля из загрузочного ППЗУ
			BOOL LoadModuleFromBootFlash(void);
			// пробуем загрузить DSP модуля из ресурса библиотеки Lusbapi
			BOOL LoadModuleFromResource(void);
			// пробуем загрузить DSP модуля из файла
			BOOL LoadModuleFromFile(const char * const FileName);
			// преобразование загрузочного массива
			long *Transform_Buffer_E440(WORD PmPoints, long * const Buffer);
			// работа с дескриптором модуля (системная часть ППЗУ модуля)
			BOOL GetModuleDescriptor(void);
			BOOL PutModuleDescriptor(void);
			// получение информации о LBIOS
			BOOL GetDspDescriptor(void);
			// получение информации о драйвере микроконтроллера
			BOOL GetMcuDescriptor(void);
			// установка дополнительных атрибутов работы микроконтроллера
			BOOL SetMcuAttributes(WORD McuAttributes);
			// функция корректного оснатова АЦП для модуля с ревизия младше 'C'
			BOOL StopAdcForOldRevision(void);
			// функция корректного остонова ЦАП для модуля с ревизия младше 'C'
			BOOL StopDacForOldRevision(void);


			// выравнивание структур
			#pragma pack(1)
			// структура с информацией о модуле, которая хранится в системной части ППЗУ модуля
			struct MODULE_DESCRIPTOR
			{
				BYTE SerialNumber[9];				// серийный номер
				BYTE Name[7];							// название модуля
				BYTE Revision;							// ревизия модуля
				BYTE DspType[5];						// тип установленного DSP
				BYTE IsDacPresented; 				// флажок наличия ЦАП
				ULONG QuartzFrequency; 				// частота кварца в Гц
				BYTE ReservedWord[13];				// зарезервировано
				SHORT AdcOffsetCoefs[ADC_CALIBR_COEFS_QUANTITY_E440];	// смещение	АЦП: 4диапазона
				WORD AdcScaleCoefs[ADC_CALIBR_COEFS_QUANTITY_E440];	// масштаб АЦП	: 4диапазона
				SHORT DacOffsetCoefs[DAC_CALIBR_COEFS_QUANTITY_E440];	// смещение	ЦАП: 2канала x 1диапазон
				WORD DacScaleCoefs[DAC_CALIBR_COEFS_QUANTITY_E440];	// масштаб ЦАП	: 2канала x 1диапазон
			};

			// структура с информацией о программе в DSP (LBIOS)
			struct DSP_DESCRIPTOR
			{
				BYTE DspName[NAME_LINE_LENGTH_LUSBAPI];	// название DSP
				BYTE Version[10];									// версия LBIOS
				BYTE Created[14];									// дата сборки LBIOS
				BYTE Manufacturer[NAME_LINE_LENGTH_LUSBAPI];	// фирма-производитель LBIOS
				BYTE Author[NAME_LINE_LENGTH_LUSBAPI];			// автор LBIOS
				BYTE Comment[128];								// строка комментария
			};

			// старая структура с информацией о программе в микроконтроллере MCU
			struct OLD_FIRMWARE_DESCRIPTOR
			{
				BYTE Version[6];									// версия драйвера MCU
				BYTE Created[14];									// дата сборки драйвера MCU
				BYTE Manufacturer[NAME_LINE_LENGTH_LUSBAPI];	// фирма-производитель драйвера MCU
				BYTE Author[NAME_LINE_LENGTH_LUSBAPI];		// автор драйвера MCU
			};

			// новая структура с информацией о программе в микроконтроллере MCU
			struct FIRMWARE_DESCRIPTOR
			{
				BYTE McuName[NAME_LINE_LENGTH_LUSBAPI];	// название микроконтроллера
				BYTE Version[10];									// версия драйвера MCU
				BYTE Created[14];									// дата сборки драйвера MCU
				BYTE Manufacturer[NAME_LINE_LENGTH_LUSBAPI];	// фирма-производитель драйвера MCU
				BYTE Author[NAME_LINE_LENGTH_LUSBAPI];			// автор драйвера MCU
				BYTE Comment[128];								// строка комментария
			};
			#pragma pack()

			// флажок разрешения глобальной записи в ППЗУ
			BOOL EnableFlashWrite;
			// флажок управления записью в служебную ообласть ППЗУ модуля
			BOOL IsServiceFlashWriting;
			// структура системной части ППЗУ модуля
			MODULE_DESCRIPTOR ModuleDescriptor;
			// структура с информацией о LBIOS
			DSP_DESCRIPTOR DspDescriptor;
  			// старая структура с информацией о программе в микроконтроллере MCU
			OLD_FIRMWARE_DESCRIPTOR OldFirmwareDescriptor;
			// новая структура с информацией о драйвере микроконтроллера
			FIRMWARE_DESCRIPTOR FirmwareDescriptor;
			// структура с параметрами функционирования АЦП
			ADC_PARS_E440 AdcPars;
			// структура с параметрами функционирования ЦАП
			DAC_PARS_E440 DacPars;
			// адрес начала сегмента кода с LBIOS в памяти программ
			WORD ProgramBaseAddress;
			// адрес начала сегмента блока данных в памяти программ
			WORD DataBaseAddress;
			// флажок статуса выходных цифровых линий модуля
			BOOL EnableTtlOut;
			// дополнительные аттрибуты работы MCU
			WORD McuAttributes;
			// локальная структура для запроса данных
//			IO_REQUEST_LUSBAPI IoReq;

			// номера доступных команд LBIOS
			enum 	{
						C_TEST,
						C_ENABLE_FLASH_WRITE, C_READ_FLASH_WORD, C_WRITE_FLASH_WORD,
						C_START_ADC, C_STOP_ADC, C_ADC_KADR, C_ADC_SAMPLE,
						C_START_DAC, C_STOP_DAC, C_DAC_SAMPLE,
						C_ENABLE_TTL_OUT, C_TTL_IN, C_TTL_OUT,
						C_NALADKA_MODE,
						C_LAST_COMMAND
					};

			// номера доступных пользовательских запросов для USB (vendor request)
			enum 	{
						V_RESET_MODULE,
						V_PUT_ARRAY, V_GET_ARRAY,
						V_START_ADC, V_START_DAC,
						V_COMMAND_IRQ,
						V_GET_MCU_DESCRIPTOR, V_SET_MCU_ATTRIBUTES,
						V_ERASE_BOOT_FLASH, V_PUT_ARRAY_BOOT_FLASH, V_GET_ARRAY_BOOT_FLASH,
						V_GET_MODULE_NAME,
						V_LOAD_LBIOS_FROM_FLASH
					};
			// дополнительные аттрибуты для микроконтроллера MCU
			enum 	{
						NO_MCU_ATTRIBUTES				= 0x0,
						ENABLE_RESET_BULK_PIPES		= (0x1 << 0x0)
					};
			// флаги доступа к памяти DSP
			enum 	{
						DSP_DM							= 0x4000,		// к памяти данных
						DSP_PM							= 0x0000			// к памяти программ
					};
			// максимальный размер блока данных, который можно однократно
			// передавать по контольному каналу USB (control pipe)
			enum 	{	MAX_USB_CONTROL_PIPE_BLOCK	= 4096 };
			// базовый номер ошибки библиотеки при работе с модулем E14-440
//			enum 	{	ERROR_ID_BASE					= 100 };
	};

	#define	MAKE_PM_BUFFER_E440(PmPoints, Buffer)		(WORD)(PmPoints), Transform_Buffer_E440((WORD)(PmPoints), (long *)(Buffer))

#endif

