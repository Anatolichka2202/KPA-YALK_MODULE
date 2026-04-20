//------------------------------------------------------------------------------
#ifndef __LUSBBASEAPIH__
#define __LUSBBASEAPIH__
//------------------------------------------------------------------------------
	#include <windows.h>
	#include <winioctl.h>
	#include "Ioctl.h"
	#include "Lusbapi.h"

	//---------------------------------------------------------------------------
	// реализация базового класса для работы с USB устройствами
	//---------------------------------------------------------------------------
	class TLUSBBASE : public ILUSBBASE
	{
		public :
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
			// управления режимом низкого электропотребления
			virtual BOOL WINAPI LowPowerMode(BOOL LowPowerFlag);
			// функция выдачи строки с последней ошибкой
			virtual BOOL WINAPI GetLastErrorInfo(LAST_ERROR_INFO_LUSBAPI * const LastErrorInfo);

			// коструктор/деструктор
			TLUSBBASE(HINSTANCE hInst);
			virtual ~TLUSBBASE();

		protected:
			// открытие устройства по его ID
			BOOL WINAPI OpenLDeviceByID(WORD VirtualSlot, DWORD DeviceID = ENUM_ALL_USB_DEVICE_ID);
			// получение названия используемого модуля
			BOOL WINAPI GetModuleNameByLength(char * const ModuleName, WORD ModuleNameLength = 0x0);
			// получение первичной информации об открытом устройсве
			BOOL WINAPI GetDeviceInitialInfo(void);
			// посылка сообщения драйверу USB
			BOOL WINAPI LDeviceIoControl(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
																LPVOID lpOutBuffer, DWORD nOutBufferSize, DWORD dwTimeOut);
			// вспомогательная функция для вычисление CRC16
			WORD WINAPI CalculateCrc16(BYTE *ptr, WORD NBytes);
			// функция сброса последней ошибки
			void WINAPI ResetLastErrorInfo(void);

			// идентификатор DLL
			HINSTANCE hInstance;
			// идентификатор устройства
			HANDLE hDevice;
			// скорость работы модуля на шине USB (0 -> USB11, 1 -> USB20)
			BYTE UsbSpeed;
			// штатная величина таймаута
			DWORD TimeOut;
			// критическая секция для потокобезопасной работы
			CRITICAL_SECTION cs;
			// номер последней ошибки
			WORD LastErrorNumber;

			// самое маленькое число типа float
			float FloatEps;
			// самое маленькое число типа double
			double DoubleEps;

			// структура c первичной информацией об устройстве
			#pragma pack(1)
			struct DEVICE_INITIAL_INFO
			{
				ULONG		Base;
				ULONG		BaseL;
				ULONG		Base1;
				ULONG		BaseL1;
				ULONG		Mem;
				ULONG		MemL;
				ULONG		Mem1;
				ULONG		MemL1;
				ULONG		Irq;
				ULONG		DeviceID;
				ULONG		DSPType;
				ULONG		Dma;
				ULONG		DmaDac;
				ULONG		DTA_REG;
				ULONG		IDMA_REG;
				ULONG		CMD_REG;
				ULONG		IRQ_RST;
				ULONG		DTA_ARRAY;
				ULONG		RDY_REG;
				ULONG		CFG_REG;
			};
			#pragma pack()
			// структура с первичной информацией об открытом устройстве
			DEVICE_INITIAL_INFO DeviceInitialInfo;

			// индексы поддерживаемых библиотекой Lusbapi USB устройств
			enum 	{
						E154_INDEX, E14_140_INDEX, E14_440_INDEX, E2010_INDEX, E2010B_INDEX,
						E_310_INDEX,
						E270_INDEX,								/*не поддерживается*/
						LTR010_INDEX, LTR021_INDEX,		/*не поддерживается*/
						SUPPORTED_USB_DEVICES_QUANTITY
					};

			// ID поддерживаемых библиотекой Lusbapi USB устройств
			enum 	{
						E14_440_ID	= 30,
						E14_140_ID	= 31,
						E2010_ID		= 32,
						E270_ID		= 33,
						LTR010_ID	= 36,
						LTR021_ID	= 37,
						E154_ID		= 38,
						E2010B_ID	= 39,
						E310_ID		= 77,

						ENUM_ALL_USB_DEVICE_ID = 0xFFFE,
						INVALID_USB_DEVICE_ID  = 0xFFFF
					};
			// массив поддерживаемых ID USB устройств
			DWORD DEVICES_ID_ARRAY[SUPPORTED_USB_DEVICES_QUANTITY];

			// длина строки с названием модуля
			enum
			{
				SHORT_MODULE_NAME_STRING_LENGTH	= 0x7,
				LONG_MODULE_NAME_STRING_LENGTH	= 0x10
			};
         
		private:
			// получение текущей версии Windows
//			DWORD GetWindowsVersion(void);

			// название модуля
			char ModuleName[LONG_MODULE_NAME_STRING_LENGTH];

			// номера доступных пользовательских запросов для USB устройств (vendor request)
			enum
			{
				V_GET_USB_SPEED 		= 6,
				V_GET_MODULE_NAME  	= 11
			};

			// индексы версий Windows
			enum 	{
						UNKNOWN_WINDOWS_VERSION,
						WINDOWS_32S,
						WINDOWS_95, WINDOWS_98_OR_LATER,
						WINDOWS_NT,
						WINDOWS_2000, WINDOWS_XP, WINDOWS_2003_SERVER,
						WINDOWS_VISTA, WINDOWS_7, WINDOWS_FUTURE
					};
	};

#endif

