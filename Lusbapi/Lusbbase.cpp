//------------------------------------------------------------------------------
#if defined(_MSC_VER)
#define NOMINMAX	// https://demin.ws/blog/russian/2009/02/15/min-max-in-visual-studio/
#endif
#include <algorithm>
#include <stdio.h>
#include "LUsbBase.h"
#include "ErrorBaseIds.h"
#include "Lusbapi.h"
//------------------------------------------------------------------------------

#define 	LAST_ERROR_NUMBER(ErrorNumber)	LastErrorNumber = LUSBBASE_BASE_ERROR_ID + ErrorNumber;

//------------------------------------------------------------------------------
//  коструктор
//------------------------------------------------------------------------------
TLUSBBASE::TLUSBBASE(HINSTANCE hInst)
{
	// самое маленькое число типа float
	FloatEps = (float)1.0E-7;
	// самое маленькое число типа double
	DoubleEps = 1.0E-14;

	// идентификатор модуля DLL
	hInstance = hInst;
	// идентификатор устройства
	hDevice = INVALID_HANDLE_VALUE;
	// сбросим скорость работы модуля на шине USB
	UsbSpeed = INVALID_USB_SPEED_LUSBAPI;
	// сбросим номер последней ошибки
	LAST_ERROR_NUMBER(0x0);
	// обнулим строчку с названием модуля
	ZeroMemory(ModuleName, sizeof(ModuleName));
	// штатная величина таймаута в мс
	TimeOut = 5000;

	// инициализация массива поддерживаемых ID USB устройств
	DEVICES_ID_ARRAY[E154_INDEX]		= E154_ID;
	DEVICES_ID_ARRAY[E14_140_INDEX]	= E14_140_ID;
	DEVICES_ID_ARRAY[E14_440_INDEX]	= E14_440_ID;
	DEVICES_ID_ARRAY[E2010_INDEX]		= E2010_ID;
	DEVICES_ID_ARRAY[E270_INDEX]		= E270_ID;
	DEVICES_ID_ARRAY[LTR010_INDEX] 	= LTR010_ID;
	DEVICES_ID_ARRAY[LTR021_INDEX] 	= LTR021_ID;
	DEVICES_ID_ARRAY[E2010B_INDEX]	= E2010B_ID;
	DEVICES_ID_ARRAY[E_310_INDEX]	= E310_ID;

	// оживим критичекую секцию для работы модуля
	InitializeCriticalSection(&cs);
}

//------------------------------------------------------------------------------
//  деструктор
//------------------------------------------------------------------------------
TLUSBBASE::~TLUSBBASE() { }

//------------------------------------------------------------------------------
//  Откроем виртуальный слот для доступа к USB модулю
//------------------------------------------------------------------------------
BOOL WINAPI TLUSBBASE::OpenLDevice(WORD VirtualSlot)
{
	// пробуем открыть хоть какое-нибудь USB устройство
	if(!OpenLDeviceByID(VirtualSlot, ENUM_ALL_USB_DEVICE_ID)) { return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
//  Освободим текущий виртуальный слот
//------------------------------------------------------------------------------
BOOL WINAPI TLUSBBASE::CloseLDevice(void)
{
	BOOL Status = TRUE;

	// освободим устройство
	if(hDevice != INVALID_HANDLE_VALUE)
	{
		if(!CloseHandle(hDevice)) { LAST_ERROR_NUMBER(6); Status = FALSE; }
		hDevice = INVALID_HANDLE_VALUE;
	}

	return Status;
}

//------------------------------------------------------------------------------
//  Освободим указатель на интерфейс модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLUSBBASE::ReleaseLInstance(void)
{
	BOOL Status = TRUE;

	// освободим ресурс критической секции
	DeleteCriticalSection(&cs);
	// освободим идентификатор виртуального слота
	if(!CloseLDevice()) Status = FALSE;
	// освободим указатель на устройство
	delete this;

	return Status;
}

//------------------------------------------------------------------------------
//  возвращается дескриптор устройства
//------------------------------------------------------------------------------
HANDLE WINAPI TLUSBBASE::GetModuleHandle(void) { return hDevice; }

//------------------------------------------------------------------------------
//  Чтение название модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLUSBBASE::GetModuleName(char * const ModuleName)
{
	// пробуем зачитать название модуля
	if(!GetModuleNameByLength(ModuleName)) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// получение текущей скорости работы шины USB
//------------------------------------------------------------------------------
BOOL WINAPI TLUSBBASE::GetUsbSpeed(BYTE * const UsbSpeed)
{
	// виртуальный слот доступен?
	if(hDevice == INVALID_HANDLE_VALUE) { LAST_ERROR_NUMBER(7); return FALSE; }
	// для модулей E14-140 и E14-440 устанавливаем режим FULL-Speed (12 Мbit/s)
	else if(!strcmp(ModuleName, "E140") || !strcmp(ModuleName, "E440") || !strcmp(ModuleName, "E154")) *UsbSpeed = USB11_LUSBAPI;
	// для модуля E-310 и E20-10 пробуем узнать текущую скорость работы USB шины
	else if(!strcmp(ModuleName, "E-310") ||!strcmp(ModuleName, "E20-10"))
	{
		// формируем запрос на получение скорости работы модуля на шине USB
		WORD InBuf[4] = { 0x01, V_GET_USB_SPEED, 0x00, 0x00 };

		// засылаем запрос в модуль
		if(!LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), UsbSpeed, 0x1/*bytes*/, TimeOut)) { LAST_ERROR_NUMBER(9); return FALSE; }
	}
	// неправильное название модуля
	else { LAST_ERROR_NUMBER(10); return FALSE; }

	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// управления режимом низкого электропотребления
//------------------------------------------------------------------------------
BOOL WINAPI TLUSBBASE::LowPowerMode(BOOL LowPowerFlag)
{
	// там где нужно - перегрузим, а пока возвращаем ошибку
	LAST_ERROR_NUMBER(14);
   return FALSE;
}

//------------------------------------------------------------------------------
// Функция выдачи информации о последней ошибке
//------------------------------------------------------------------------------
BOOL WINAPI TLUSBBASE::GetLastErrorInfo(LAST_ERROR_INFO_LUSBAPI * const LastErrorInfo)
{
	// проверим структуру последней ошибки
	if(!LastErrorInfo) { return FALSE; }
	// номер последней ошибки
	LastErrorInfo->ErrorNumber = LastErrorNumber;
	// попробуем зачитать строку с последней ошибкой
	if(!LoadString(hInstance, LastErrorNumber, (LPTSTR)LastErrorInfo->ErrorString, sizeof(LastErrorInfo->ErrorString)))
	{
		// специфический номер ошибки
		LastErrorInfo->ErrorNumber = LUSBBASE_BASE_ERROR_ID + 99;
		strcpy((char *)LastErrorInfo->ErrorString, "Ошибка TLUSBBASE::GetLastErrorInfo(). Невозможно получить строку с последней ошибкой поскольку не выполнилась WinAPI функция LoadString().");
		return FALSE;
	}
	// все хорошо :)))))
	else return TRUE;
}





//==============================================================================
// вспомогательные функции для работы с модулем
//==============================================================================
BOOL WINAPI TLUSBBASE::OpenLDeviceByID(WORD VirtualSlot, DWORD DeviceID)
{
	char DeviceName[18];
	WORD i;
//	DWORD WindowsVersion;

	// сбросим номер последней ошибки
	LAST_ERROR_NUMBER(0x0);

	// виртуальный слот доступен?
	if(hDevice != INVALID_HANDLE_VALUE) { CloseHandle(hDevice); hDevice = INVALID_HANDLE_VALUE; }

	// определимся с версией используемой Windows
/*	WindowsVersion = GetWindowsVersion();
	if((WindowsVersion == UNKNOWN_WINDOWS_VERSION) ||
		(WindowsVersion == WINDOWS_32S) || (WindowsVersion == WINDOWS_95) ||
		(WindowsVersion == WINDOWS_NT)) { LAST_ERROR_NUMBER(1); return FALSE; }*/

	// формируем название драйвера USB
	wsprintf(DeviceName, "\\\\.\\LDev%d", VirtualSlot);
	// попробуем открыть идентификатор для модуля
	hDevice = CreateFile(DeviceName, GENERIC_READ|GENERIC_WRITE, 0x0, NULL,
												OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	// проверим: получилось ли открыть устройство?
	if(hDevice == INVALID_HANDLE_VALUE) { LAST_ERROR_NUMBER(2); return FALSE; }
	// попробуем получить первичную информацию об открытом устройсве
	if(!GetDeviceInitialInfo()) { LAST_ERROR_NUMBER(11); CloseLDevice(); return FALSE; }
	// если требуется пройтись по всем ID, то выполним это
	else if(DeviceID == ENUM_ALL_USB_DEVICE_ID)
	{
		for(i = 0x0; i < SUPPORTED_USB_DEVICES_QUANTITY; i++)
			if(DeviceInitialInfo.DeviceID == DEVICES_ID_ARRAY[i]) break;
		if(i == SUPPORTED_USB_DEVICES_QUANTITY) { LAST_ERROR_NUMBER(12); return FALSE; }
	}
	// проверим полученное ID устройства с требуемым
	else if(DeviceInitialInfo.DeviceID != DeviceID)
	{
		if(DeviceID == E2010_ID)
		{
			if(DeviceInitialInfo.DeviceID != E2010B_ID) { LAST_ERROR_NUMBER(13); return FALSE; }
		}
		else { LAST_ERROR_NUMBER(13); return FALSE; }
	}

	// попробуем прочитать название модуля
	if(!GetModuleName(ModuleName)) { LAST_ERROR_NUMBER(3); CloseLDevice(); return FALSE; }
	// определим на какой скорости работает модуль
	else if(!GetUsbSpeed(&UsbSpeed)) { LAST_ERROR_NUMBER(4); CloseLDevice(); return FALSE; }
	// неправильно опредилась скорость работы модуля на шине USB
	else if(UsbSpeed >= INVALID_USB_SPEED_LUSBAPI) { LAST_ERROR_NUMBER(5); CloseLDevice(); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// Пробуем получить первичную информацию об открытом устройсве
//------------------------------------------------------------------------------
BOOL WINAPI TLUSBBASE::GetDeviceInitialInfo(void)
{
	// виртуальный слот доступен?
	if(hDevice == INVALID_HANDLE_VALUE) { LAST_ERROR_NUMBER(7); return FALSE; }
	else if(!LDeviceIoControl(DIOC_GET_PARAMS, NULL, 0x0,
							  &DeviceInitialInfo, sizeof(DEVICE_INITIAL_INFO), TimeOut)) { LAST_ERROR_NUMBER(9); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
//  Чтение название модуля с возможностью использования длины строки
//------------------------------------------------------------------------------
BOOL WINAPI TLUSBBASE::GetModuleNameByLength(char * const ModuleName, WORD ModuleNameLength)
{
	// формируем запрос на получение названия модуля
	WORD InBuf[4] = { 0x01, V_GET_MODULE_NAME, 0x00, 0x00 };

	// виртуальный слот доступен?
	if(hDevice == INVALID_HANDLE_VALUE) { LAST_ERROR_NUMBER(7); return FALSE; }
	// проверим строку
	else if(!ModuleName) { LAST_ERROR_NUMBER(8); return FALSE; }
	// проверим длину названия
	if(ModuleNameLength)
	{
		// обнулим строчку с названием модуля
		ZeroMemory(ModuleName, ModuleNameLength);
		// посылаем запрос в модуль
		if(!LDeviceIoControl(DIOC_SEND_COMMAND, InBuf, sizeof(InBuf), ModuleName, ModuleNameLength, TimeOut)) { LAST_ERROR_NUMBER(9); return FALSE; }
	}
	else
	{
		char LocModuleName[LONG_MODULE_NAME_STRING_LENGTH];

		// обнулим строчку с названием модуля
		ZeroMemory(LocModuleName, LONG_MODULE_NAME_STRING_LENGTH);
		// для начала попробуем заслать запрос большой длины
		if(!LDeviceIoControl(DIOC_SEND_COMMAND, InBuf, sizeof(InBuf), LocModuleName, LONG_MODULE_NAME_STRING_LENGTH, TimeOut))
		{
			// теперь можно попробовать заслать запрос меньшей длины
			if(!LDeviceIoControl(DIOC_SEND_COMMAND, InBuf, sizeof(InBuf), LocModuleName, SHORT_MODULE_NAME_STRING_LENGTH, TimeOut)) { LAST_ERROR_NUMBER(9); return FALSE; }
			// скопируем короткую строчку
			strncpy(ModuleName, LocModuleName, SHORT_MODULE_NAME_STRING_LENGTH);
			// обнуляем последний символ в строке
			ModuleName[SHORT_MODULE_NAME_STRING_LENGTH - 0x1] = 0x0;
		}
		else
		{
			// скопируем длинную строчку
			strncpy(ModuleName, LocModuleName, LONG_MODULE_NAME_STRING_LENGTH);
			// обнуляем последний символ в строке
			ModuleName[LONG_MODULE_NAME_STRING_LENGTH - 0x1] = 0x0;
		}
	}
	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// выполнение ассинхронного пользовательского запроса в модуль
//------------------------------------------------------------------------------
BOOL WINAPI TLUSBBASE::LDeviceIoControl(	DWORD dwIoControlCode,		// control code of operation to perform
														LPVOID lpInBuffer,			// pointer to buffer to supply input data
														DWORD nInBufferSize,			// size of input buffer in bytes
														LPVOID lpOutBuffer,			// pointer to buffer to receive output data
														DWORD nOutBufferSize,		// size of output buffer in bytes
														DWORD TimeOut)					// таймаут в мс
{
	DWORD RealBytesTransferred;
	DWORD BytesReturned;
	OVERLAPPED Ov;

	// виртуальный слот доступен?
	if(hDevice == INVALID_HANDLE_VALUE) { LAST_ERROR_NUMBER(7); return FALSE; }

	// инициализируем OVERLAPPED структуру
	memset(&Ov, 0x0, sizeof(OVERLAPPED));
	// создаём событие для асинхронного запроса
	Ov.hEvent = CreateEvent(NULL, FALSE , FALSE, NULL);
	if(!Ov.hEvent) { return FALSE; }

	// посылаем требуемый запрос в MCU
	if(!DeviceIoControl(	hDevice, dwIoControlCode,
								lpInBuffer, nInBufferSize,
								lpOutBuffer, nOutBufferSize,
								&BytesReturned, &Ov))
			{ if(GetLastError() != ERROR_IO_PENDING) { /*LastErrorNumber = 1001;*/ CloseHandle(Ov.hEvent); return FALSE; } }
	// ждём окончания выполнения запроса
	if(WaitForSingleObject(Ov.hEvent, TimeOut) == WAIT_TIMEOUT) { CancelIo(hDevice); CloseHandle(Ov.hEvent); /*LastErrorNumber = 1001;*/ return FALSE; }
	// попробуем получить кол-во реально переданных байт данных
	else if(!GetOverlappedResult(hDevice, &Ov, &RealBytesTransferred, TRUE)) { CancelIo(hDevice); CloseHandle(Ov.hEvent); /*LastErrorNumber = 1001;*/ return FALSE; }
	// проверим сколько реально было передано байт данных
	if(nOutBufferSize != RealBytesTransferred) { CancelIo(hDevice); CloseHandle(Ov.hEvent); /*LastErrorNumber = 1001;*/ return FALSE; }
	// закроем событие асинхронного запроса
	else if(!CloseHandle(Ov.hEvent)) { /*LastErrorNumber = 1001;*/ return FALSE; }
	// все хорошо :)))))
	return TRUE;
}

//----------------------------------------------------------------
// функция для вычисление CRC16
//----------------------------------------------------------------
WORD WINAPI TLUSBBASE::CalculateCrc16(BYTE *ptr, WORD NBytes)
{
	WORD w = 0x0;
	WORD i, j;

	for(i = 0x0; i < NBytes; i++)
	{
		w ^= (WORD)((WORD)(ptr[i]) << 0x8);
		for(j = 0x0; j < 8; j++)
		{
			if(w & (WORD)0x8000) w = (WORD)((w << 0x1) ^ 0x8005);
			else w <<= 0x1;
		}
	}
	return w;
}

//----------------------------------------------------------------
// функция сброса последней ошибки
//----------------------------------------------------------------
void WINAPI TLUSBBASE::ResetLastErrorInfo(void)
{
	// у нас всё хорошо - сбросываем последнюю ошибку
	LAST_ERROR_NUMBER(0x0);
}

//------------------------------------------------------------------------------
// попробуем получить версию используемой Windows
//------------------------------------------------------------------------------
/*DWORD TLUSBBASE::GetWindowsVersion(void)
{
	OSVERSIONINFO osvi;

	ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

	if(!GetVersionEx(&osvi))
	{
		osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		if(!GetVersionEx((OSVERSIONINFO *)&osvi)) return UNKNOWN_WINDOWS_VERSION;
	}

	switch (osvi.dwPlatformId)
	{
		case VER_PLATFORM_WIN32_NT:
			if(osvi.dwMajorVersion <= 4 ) return WINDOWS_NT; 			// WinNT
			else if((osvi.dwMajorVersion == 5) && (osvi.dwMinorVersion == 0)) return WINDOWS_2000; 			// Win2000
			else if((osvi.dwMajorVersion == 5) && (osvi.dwMinorVersion == 1)) return WINDOWS_XP; 				// WinXP
			else if((osvi.dwMajorVersion == 5) && (osvi.dwMinorVersion == 2)) return WINDOWS_2003_SERVER; 	// Win2003 Server
			else if((osvi.dwMajorVersion == 6) && (osvi.dwMinorVersion == 0)) return WINDOWS_VISTA; 	// WinVista или  WinServer 2008
			else if((osvi.dwMajorVersion == 6) && (osvi.dwMinorVersion == 1)) return WINDOWS_7; 		// Win7     или  WinServer 2008 R2
			else if(osvi.dwMajorVersion == 6) return WINDOWS_FUTURE;			// возможно для будущей Windows
			else return UNKNOWN_WINDOWS_VERSION;

		case VER_PLATFORM_WIN32_WINDOWS:
			if((osvi.dwMajorVersion > 0x4) || ((osvi.dwMajorVersion == 0x4) &&
				(osvi.dwMinorVersion > 0x0))) return WINDOWS_98_OR_LATER; // Win98 or later
			else return WINDOWS_95; 											// Win95

		case VER_PLATFORM_WIN32s: return WINDOWS_32S; 					// Win32s
	}

	return UNKNOWN_WINDOWS_VERSION;
} */
