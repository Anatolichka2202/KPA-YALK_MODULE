//------------------------------------------------------------------------------
#if defined(_MSC_VER)
	#define NOMINMAX	// https://demin.ws/blog/russian/2009/02/15/min-max-in-visual-studio/
#endif
#include <algorithm>
#include <stdio.h>
#include <math.h>
//
#include "e140.h"
#include "ErrorBaseIds.h"
//------------------------------------------------------------------------------

#define 	LAST_ERROR_NUMBER(ErrorNumber)	LastErrorNumber = E140_BASE_ERROR_ID + ErrorNumber

// -----------------------------------------------------------------------------
//  коструктор
// ----------------------------------------------------------------------------
TLE140::TLE140(HINSTANCE hInst) : TLUSBBASE(hInst)
{
	// проинициализируем все необходимые переменные
	InitVars();
}

// -----------------------------------------------------------------------------
//  деструктор
// -----------------------------------------------------------------------------
TLE140::~TLE140() { }



// =============================================================================
// Функции общего назначения для работы с модулем Е14-140
// =============================================================================
// -----------------------------------------------------------------------------
//  Откроем виртуальный слот для доступа к USB модулю
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::OpenLDevice (WORD VirtualSlot)
{
	char ModuleName[LONG_MODULE_NAME_STRING_LENGTH];

	// проинициализируем все необходимые переменные
	InitVars();
	// сбросим последнюю ошибку
	TLUSBBASE::ResetLastErrorInfo();
	//  попробуем открыть виртуальный слот для доступа к USB модулю
	if(!TLUSBBASE::OpenLDeviceByID(VirtualSlot, E14_140_ID)) { return FALSE; }
	// попробуем прочитать название модуля
	else if(!TLUSBBASE::GetModuleName(ModuleName)) { TLUSBBASE::CloseLDevice(); return FALSE; }
	// убедимся, что это модуль E14-140
	else if(strcmp(ModuleName, "E140")) { TLUSBBASE::CloseLDevice(); LAST_ERROR_NUMBER(0); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------------
// освободим текущий виртуальный слот
//------------------------------------------------------------------------------------
BOOL WINAPI TLE140::CloseLDevice(void)
{
	// закроем устройство
	if(!TLUSBBASE::CloseLDevice()) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------------
// освободим указатель на устройство
//------------------------------------------------------------------------------------
BOOL WINAPI TLE140::ReleaseLInstance(void)
{
	BOOL Status = TRUE;

	// освободим идентификатор виртуального слота
	if(!CloseLDevice()) Status = FALSE;
	// освободим указатель на устройство
	delete this;
	// возвращаем статус выполнения функции
	return Status;
}

//------------------------------------------------------------------------------
// возвращается дескриптор устройства
//------------------------------------------------------------------------------
HANDLE WINAPI TLE140::GetModuleHandle(void) { return hDevice; }

//------------------------------------------------------------------------------
//  Чтение название модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE140::GetModuleName(char * const ModuleName)
{
	// проверим буфер
	if(!ModuleName) { LAST_ERROR_NUMBER(1); return FALSE; }
	// теперь попробуем узнать название модуля
	else if(!TLUSBBASE::GetModuleNameByLength(ModuleName, SHORT_MODULE_NAME_STRING_LENGTH)) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------------
//  Получение текущей скорости работы шины USB
//------------------------------------------------------------------------------------
BOOL WINAPI TLE140::GetUsbSpeed(BYTE * const UsbSpeed)
{
	// проверим указатель
	if(!UsbSpeed) { LAST_ERROR_NUMBER(1); return FALSE; }
	// теперь попробуем узнать скорость работы USB шины
	else if(!TLUSBBASE::GetUsbSpeed(UsbSpeed)) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// управления режимом низкого электропотребления модуля E14-140
//------------------------------------------------------------------------------
BOOL WINAPI TLE140::LowPowerMode(BOOL LowPowerFlag)
{
	BYTE Flag = (BYTE)(LowPowerFlag ? 0x1 : 0x0);

	// попробуем переслать в модуль команду управления низким электропотреблением
	if(!PutArray(&Flag, 0x1, SUSPEND_MODE_FLAG)) { return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------
// Функция выдачи строки с последней ошибкой
//------------------------------------------------------------------------------
BOOL WINAPI TLE140::GetLastErrorInfo(LAST_ERROR_INFO_LUSBAPI * const LastErrorInfo)
{
	BOOL Status;

	// вычитываем название ошибки
	Status = TLUSBBASE::GetLastErrorInfo(LastErrorInfo);
	// если можно - сбросим последнюю ошибку
	if(Status) TLUSBBASE::ResetLastErrorInfo();
   // озвращаем статус выполнения функции
	return Status;
}




// =============================================================================
// функции для работы с АЦП модуля
// =============================================================================
// -----------------------------------------------------------------------------
// получение текущих параметров работы АЦП
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::GET_ADC_PARS(ADC_PARS_E140 * const AdcPars)
{
	BYTE b, smode;
	BYTE buf[136];
	WORD i;
	DWORD ClockRate;

	// проверим структуру
	if(!AdcPars) { LAST_ERROR_NUMBER(1); return FALSE; }
	// зачитаем текущие параметры работы АЦП
	if(!GetArrayFromMcu(buf, sizeof(buf), ADC_PARS_BASE)) { LAST_ERROR_NUMBER(2); return FALSE; }
	// таблица логических каналов
	for(i = 0x0; i < MAX_CONTROL_TABLE_LENGTH_E140; i++)
	{
		b = buf[i];
		this->AdcPars.ControlTable[i] = (WORD)((b & 0xF0) | ((b << 2) & 0x0C) | ((b >> 2) & 0x03));
	}
	ClockRate = ModuleDescriptor.ClockRate;
	if((ClockRate > 24000000L) || (ClockRate < 1000000L)) ClockRate = 16000000L;
	// частота работы АЦП(кГц)
	this->AdcPars.AdcRate = (double)ClockRate/2000.0/(*(WORD*)(buf+128));
	// число активных логических каналов
	this->AdcPars.ChannelsQuantity = buf[130];
	// межкадровая задержка (мс)
	this->AdcPars.InterKadrDelay = (double)((WORD)buf[131] + 1.0)/this->AdcPars.AdcRate;
	// частота кадров (кГц)
	this->AdcPars.KadrRate = (double)this->AdcPars.AdcRate/((WORD)buf[130] + (WORD)buf[131]);
	// тип синхронизации
	smode = (BYTE)(buf[132] & 0x0F);
	if(smode == 0x1)      	this->AdcPars.InputMode = 2;
	else if(smode == 0x2) 	this->AdcPars.InputMode = 1;
	else if(smode > 0x2) 	this->AdcPars.InputMode = 3;
	else              		this->AdcPars.InputMode = 0;
	this->AdcPars.SynchroAdType = (BYTE)((smode == 0x5 || smode == 0x6) ? 0x1 : 0x0);
	this->AdcPars.SynchroAdMode = (BYTE)((smode == 0x4 || smode == 0x6) ? 0x1 : 0x0);
	// источник тактовых импульсов
	this->AdcPars.ClkSource = (BYTE)((buf[132] >> 0x6) & 0x1);
	// флаг разрешения трансляции собственных тактовых импульсов АЦП
	this->AdcPars.EnableClkOutput = (BYTE)((buf[132] >> 0x7) & 0x1);
	// номер логического канала при аналоговой синхронизации
	b = buf[133];
	this->AdcPars.SynchroAdChannel = (WORD)((b & 0xF0) | ((b << 0x2) & 0x0C) | ((b >> 0x2) & 0x03));
	// порог срабатывания при аналоговой синхронизации
	this->AdcPars.SynchroAdPorog = *(SHORT*)(buf+134);
	//
	*AdcPars = this->AdcPars;
	// все хорошо :)))))
	return TRUE;
}

// -----------------------------------------------------------------------------
//  заполнение требуемых параметров работы АЦП
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::SET_ADC_PARS(ADC_PARS_E140 * const AdcPars)
{
	BYTE b, smode;
	BYTE buf[136];
	DWORD i;
	DWORD ClockRate;

	// проверим структуру
	if(!AdcPars) { LAST_ERROR_NUMBER(1); return FALSE; }
	// проверим флажок сбора данных - во время сбора данных менять параметры АЦП нельзя
	// предварительно необходимо вызвать функцию STOP_ADC()
	else if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверяем название модуля
	else if(strcmp((char *)ModuleDescriptor.Name, "E140")) { LAST_ERROR_NUMBER(27); return FALSE; }

	// обнулим буфер
	ZeroMemory(buf, sizeof(buf));
	// таблица логических каналов
	for(i = 0x0; i < MAX_CONTROL_TABLE_LENGTH_E140; i++)
	{
		b = (BYTE)AdcPars->ControlTable[i];
		buf[i] = (BYTE)((b & 0xF0) | ((b << 0x2) & 0x0C) | ((b >> 0x2) & 0x03));
	}
	// определимся с тактовой частотой модуля
	ClockRate = ModuleDescriptor.ClockRate;		// в Гц
	if((ClockRate > 24000000L) || (ClockRate < 1000000L)) ClockRate = 16000000L;
	ClockRate = (DWORD)(ClockRate/2000.0 + 0.5);

	// проверим частоту АЦП на минимально возможное значение
	AdcPars->AdcRate = fabs(AdcPars->AdcRate);
	if(AdcPars->AdcRate < ((double)ClockRate/(double)0xFFFFL)) AdcPars->AdcRate = (double)ClockRate/(double)0xFFFFL;
	// вычисляем делитель частоты работы АЦП
	i = (DWORD)((double)ClockRate/AdcPars->AdcRate + 0.5);
	// наложим ограничения на минимальныый делитель частоты работы АЦП
	if(i > 0xFFFF) i = 0xFFFF;				// размер делителя - WORD
	else
	{
		// у модуля ревизии 'A' максимальная частота работы АЦП составляет 100 кГц
		if(ModuleDescriptor.Revision == 'A') { if(i < 80) i = 80; }
		// для остальных - 200 кГц
		else { if(i < 40) i = 40; }
	}
	// вернем реально установленную частоту работы АЦП в кГц
	AdcPars->AdcRate = (double)ClockRate/(double)i;
	//
	*(WORD*)(buf+128) = (WORD)i;
	// число активных логических каналов
	if(!AdcPars->ChannelsQuantity) { LAST_ERROR_NUMBER(26); return FALSE; }
	else if(AdcPars->ChannelsQuantity > MAX_CONTROL_TABLE_LENGTH_E140) AdcPars->ChannelsQuantity = MAX_CONTROL_TABLE_LENGTH_E140;
	buf[130] = (BYTE)AdcPars->ChannelsQuantity;
	// делитель межкадровой задержки АЦП
	i = (DWORD)(AdcPars->AdcRate*AdcPars->InterKadrDelay + 0.5);
	// наложим ограничения на делитель межкадровой задержки АЦП
	if(i == 0x0) i = 0x1;
	else if(i > 0x100) i = 0x100;
	// вернем реально установленную межкадровой задержки АЦП в мс
	AdcPars->InterKadrDelay = (double)i/AdcPars->AdcRate;
	//
	buf[131] = (BYTE)(i - 0x1);
	// частота кадров (кГц)
	AdcPars->KadrRate = (double)AdcPars->AdcRate/((WORD)buf[130] + (WORD)buf[131]);
	 // тип синхронизации
	if(AdcPars->InputMode > ANALOG_SYNC_E140) AdcPars->InputMode = ANALOG_SYNC_E140;
	smode = (BYTE)AdcPars->InputMode;
	if(smode == 1) smode=2;
	else if(smode == 2) smode = (BYTE)(0x1);
	else if(smode == 3)
	{
		if(AdcPars->SynchroAdType) smode += (BYTE)(0x2);
		if(AdcPars->SynchroAdMode) smode += (BYTE)(0x1);
	}
	buf[132] = smode;
	// источник тактовых импульсов
	if(AdcPars->ClkSource) buf[132] |= (0x1 << 0x6);
	// флаг разрешения трансляции собственных тактовых импульсов АЦП
	if(AdcPars->EnableClkOutput) buf[132] |= (0x1 << 0x7);
	// номер логического канала при аналоговой синхронизации
	b = (BYTE)AdcPars->SynchroAdChannel;
	buf[133] = (BYTE)((b & 0xF0) | ((b << 0x2) & 0x0C) | ((b >> 0x2) & 0x03));
	// порог срабатывания при аналоговой синхронизации
	*(SHORT*)(buf+134) = AdcPars->SynchroAdPorog;
	// запомним текущие параметры работы АЦП
	this->AdcPars=*AdcPars;
	// передаём параметры АЦП
	if(!PutArrayToMcu(buf, sizeof(buf), ADC_PARS_BASE)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

// -----------------------------------------------------------------------------
// запуск работы АЦП
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::START_ADC(void)
{
	WORD InBuf[4] = { 0x0000, V_START_ADC, 0x0, 0x0 };

	// отсылаем запрос на запуск АЦП
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(5); return FALSE; }
	// установми флажок сбора данных
	IsDataAcquisitionInProgress = TRUE;
	// всё хорошо
	return TRUE;
}

// -----------------------------------------------------------------------------
// останов АЦП
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::STOP_ADC(void)
{
	WORD InBuf[4] = {0x0000, V_STOP_ADC, 0x0, 0x0 };

	// отсылаем команду на останов АЦП
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(5); return FALSE; }
	// зачистим канал приёма данных
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE3/*reset Read Pipe*/, NULL, 0x0, NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(5); return FALSE; }
	// сбросим флажок сбора данных
	IsDataAcquisitionInProgress = FALSE;
	// всё хорошо
	return TRUE;
}

// -----------------------------------------------------------------------------
//  ввод кадра отсчетов с АЦП модуля
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::ADC_KADR(SHORT * const Data)
{
	// запрос на однократное считывание кадра отсчётов АЦП
	WORD InBuf[4] = { 0x0000, V_START_ADC_ONCE, 0x0, 0x0};
	// кол-во необходимых отсчётов
	DWORD PointsToRead;
	// структура дла запроса данных
	IO_REQUEST_LUSBAPI IoReq;

	// проверим буфер
	if(!Data) { LAST_ERROR_NUMBER(1); return FALSE; }
	// проверим кол-во опрашиваемых каналов
	else if(AdcPars.ChannelsQuantity > 32) { LAST_ERROR_NUMBER(6); return FALSE; }
	// проверим флажок сбора данных - во время сбора данных нельзя выполнять данную фукнцию
	// предварительно необходимо вызвать функцию STOP_ADC()
	else if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(3); return FALSE; }
	// небходимо выполнить функцию STOP_ADC()
	else if(!STOP_ADC()) { LAST_ERROR_NUMBER(25); return FALSE; }
	// сбрасываем принимающий канал
//	else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE3, NULL, NULL, NULL, NULL, TimeOut)) { LAST_ERROR_NUMBER(5); return FALSE; }
	// отсылаем запрос на однократный запуск АЦП
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(5); return FALSE; }

	// формируем структуру запроса типа IO_REQUEST_LUSBAPI на ввод данных
	if(!(AdcPars.ChannelsQuantity%32)) PointsToRead = AdcPars.ChannelsQuantity;
	else { PointsToRead = AdcPars.ChannelsQuantity - AdcPars.ChannelsQuantity%32; PointsToRead += 32; }
	// структура синхронного запроса на кадра данных с АЦП
	IoReq.Buffer 					= new SHORT[PointsToRead];	// буфер данных
	if(!IoReq.Buffer) { LAST_ERROR_NUMBER(7); return FALSE; }
	IoReq.NumberOfWordsToPass 	= PointsToRead;	 			// кол-во данных в SHORT
	IoReq.NumberOfWordsPassed 	= 0x0;
	IoReq.Overlapped			  	= NULL;				 			// у нас будет синхронный запрос
	IoReq.TimeOut					= (DWORD)(PointsToRead/AdcPars.AdcRate + 1000.0);	// таймаут выполнения запроса
	// далаем ввод кадра отсчётов
	if(!ReadData(&IoReq)) { LAST_ERROR_NUMBER(8); delete [] IoReq.Buffer; return FALSE; }
	// копируем полученные данные
	CopyMemory(Data, &IoReq.Buffer[(32 - AdcPars.ChannelsQuantity) & 0x1F], AdcPars.ChannelsQuantity*sizeof(SHORT));
	// освобождаем память
	if(IoReq.Buffer) delete [] IoReq.Buffer;
	// всё хорошо
	return TRUE;
}


// -----------------------------------------------------------------------------
//  однократный ввод с заданного логического канала АЦП модуля
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::ADC_SAMPLE(SHORT * const Data, WORD Channel)
{
	WORD b = (WORD)((Channel & 0xF0) | ((Channel << 0x2) & 0x0C) | ((Channel >> 0x2) & 0x03));

	// проверим флажок сбора данных - во время сбора данных нельзя выполнять данную фукнцию
	// предварительно необходимо вызвать функцию STOP_ADC()
	if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(3); return FALSE; }
	// физический номер канала
	else if(!PutArrayToMcu((BYTE*)&b, 0x2, ADC_CHANNEL_SELECT_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// задержечка
	Sleep(40);
	// 2х ступенчатый конвейер
	if(!PutArrayToMcu((BYTE*)&b, 0x2, ADC_CHANNEL_SELECT_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// читаем осчёт
	else if(!GetArrayFromMcu((BYTE*)Data, 0x2, ADC_DATA_REGISTER)) { LAST_ERROR_NUMBER(2); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
//  потоковое чтение данных с АЦП модуля
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::ReadData(IO_REQUEST_LUSBAPI * const ReadRequest)
{
	DWORD NumberOfBytesRead;

	// проверим буфер данных
	if(!ReadRequest->Buffer) { LAST_ERROR_NUMBER(1); return FALSE; }
	// число отсчетов должно находиться в диапазоне от 0x20(32)
	else if(ReadRequest->NumberOfWordsToPass < 0x20) ReadRequest->NumberOfWordsToPass = 0x20;
	// и вплоть до 0x100000(1024*1024)
	else if(ReadRequest->NumberOfWordsToPass > (1024*1024)) ReadRequest->NumberOfWordsToPass = 1024*1024;
	// также число отсчетов должно быть кратно 0x20(32)
	ReadRequest->NumberOfWordsToPass -= ReadRequest->NumberOfWordsToPass%32;
	//
	NumberOfBytesRead = 0x0;

	if(ReadRequest->Overlapped)
	{
		// посылаем асинхронный запрос на сбор необходимого кол-ва данных
		if(!ReadFile(hDevice, ReadRequest->Buffer, 2*ReadRequest->NumberOfWordsToPass, &NumberOfBytesRead, ReadRequest->Overlapped))
			{ if(GetLastError() != ERROR_IO_PENDING) { LAST_ERROR_NUMBER(9); CancelIo(hDevice); return FALSE; } }
		// кол-во полученных отсчётов (в силу асинхронности запроса эта величина вполне может быть равна 0)
		ReadRequest->NumberOfWordsPassed = NumberOfBytesRead/2;
	}
	else
	{
		OVERLAPPED Ov;

		// инициализируем OVERLAPPED структуру
		ZeroMemory(&Ov, sizeof(OVERLAPPED));
		// создаём событие
		Ov.hEvent = CreateEvent(NULL, FALSE , FALSE, NULL);
		if(!Ov.hEvent) { LAST_ERROR_NUMBER(10); return FALSE; }

		// посылаем асинхронный запрос на сбор необходимого кол-ва данных
		NumberOfBytesRead = 0x0;
		if(!ReadFile(hDevice, ReadRequest->Buffer, 2*ReadRequest->NumberOfWordsToPass, &NumberOfBytesRead, &Ov))
			{ if(GetLastError() != ERROR_IO_PENDING) { LAST_ERROR_NUMBER(9); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; } }
		// ждём окончания выполнения запроса
		if(WaitForSingleObject(Ov.hEvent, ReadRequest->TimeOut) == WAIT_TIMEOUT) { LAST_ERROR_NUMBER(11); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; }
		// попробуем получить кол-во реально переданных байт данных
		else if(!GetOverlappedResult(hDevice, &Ov, &NumberOfBytesRead, TRUE)) { LAST_ERROR_NUMBER(12); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; }
		// закроем событие
		else if(!CloseHandle(Ov.hEvent)) { LAST_ERROR_NUMBER(13); return FALSE; }
		// кол-во полученных отсчётов
		ReadRequest->NumberOfWordsPassed = NumberOfBytesRead/2;
		// сравним сколько было реально полученно данных (в словах) с запрашиваемым кол-вом отсчётов
		if(ReadRequest->NumberOfWordsToPass != ReadRequest->NumberOfWordsPassed) { LAST_ERROR_NUMBER(14); return FALSE; }
	}
	// все хорошо :)))))
	return TRUE;
}




// =============================================================================
//  функции для работы с ЦАП модуля
// =============================================================================
//------------------------------------------------------------------------------
// получение текущих параметров потоковой работы ЦАП
//------------------------------------------------------------------------------
BOOL WINAPI TLE140::GET_DAC_PARS(DAC_PARS_E140 * const DacPars)
{
	// проверим структуру параметров работы ЦАП
	if(!DacPars) { LAST_ERROR_NUMBER(1); return FALSE; }
	// проверяем название модуля
	else if(strcmp((char *)ModuleDescriptor.Name, "E140")) { LAST_ERROR_NUMBER(27); return FALSE; }
	// у модуля ревизии 'A' нет потокового вывода на ЦАП
	else if(ModuleDescriptor.Revision == 'A') { LAST_ERROR_NUMBER(28); return FALSE; }

	// считываем текущие параметры ЦАП
	if(!GetArrayFromMcu((BYTE *)&this->DacPars, sizeof(this->DacPars), DAC_PARS_BASE)) { LAST_ERROR_NUMBER(2); return FALSE; }

	// вычислим частоту работы ЦАП в кГц ( f = 200/(RateDiv+1) )
	DacPars->DacRate = 200.0/(this->DacPars.DacRateDiv + 1.0);
	// посмотрим нужна ли синхронная работа АЦП и ЦАП
	DacPars->SyncWithADC = this->DacPars.SyncWithADC;
	// посмотрим нужна ли синхронная работа АЦП и ЦАП
	DacPars->SetZeroOnStop = this->DacPars.SetZeroOnStop;

	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// заполнение требуемых параметров для потоковой работы ЦАП
//------------------------------------------------------------------------------
BOOL WINAPI TLE140::SET_DAC_PARS(DAC_PARS_E140 * const DacPars)
{
	DWORD DacDiv;
	DWORD ClockRate;

	// проверим структуру параметров работы ЦАП
	if(!DacPars) { LAST_ERROR_NUMBER(2); return FALSE; }
	// проверяем название модуля
	else if(strcmp((char *)ModuleDescriptor.Name, "E140")) { LAST_ERROR_NUMBER(27); return FALSE; }
	// у модуля ревизии 'A' нет потокового вывода на ЦАП
	else if(ModuleDescriptor.Revision == 'A') { LAST_ERROR_NUMBER(28); return FALSE; }

	// разберёмся с частотой потоковой работы ЦАП
	ClockRate = ModuleDescriptor.ClockRate;
	if((ClockRate > 24000000L) || (ClockRate < 1000000L)) ClockRate = 16000000L;
	ClockRate = (DWORD)(ClockRate/80000.0 + 0.5);
	DacPars->DacRate = fabs(DacPars->DacRate);
	DacDiv = (DWORD)(ClockRate/DacPars->DacRate - 0.5);
	if(DacDiv > 0x00000007L) DacDiv = 0x0007L;
//	if(DacDiv > 0x000000FFL) DacDiv = 0x00FFL;
//	if(DacDiv > 0x0000FFFFL) DacDiv = 0xFFFFL;
	this->DacPars.DacRateDiv = (WORD)DacDiv;
	// вернём реально установленную частоту ЦАП
	DacPars->DacRate = ClockRate/(this->DacPars.DacRateDiv + 1.0);

	// посмотрим нужна ли синхронная работа АЦП и ЦАП
	if(DacPars->SyncWithADC) this->DacPars.SyncWithADC = 0x1;
	else this->DacPars.SyncWithADC = 0x0;

	// посмотрим нужна ли синхронная работа АЦП и ЦАП
	if(DacPars->SetZeroOnStop) this->DacPars.SetZeroOnStop = 0x1;
	else this->DacPars.SetZeroOnStop = 0x0;

	// передаём параметры ЦАП
	if(!PutArrayToMcu((BYTE *)&this->DacPars, sizeof(this->DacPars), DAC_PARS_BASE)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}
//------------------------------------------------------------------------------
// запуск потоковый работы ЦАП
//------------------------------------------------------------------------------
BOOL WINAPI TLE140::START_DAC(void)
{
	// проверяем название модуля
	if(strcmp((char *)ModuleDescriptor.Name, "E140")) { LAST_ERROR_NUMBER(27); return FALSE; }
	// у модуля ревизии 'A' нет потокового вывода на ЦАП
	else if(ModuleDescriptor.Revision == 'A') { LAST_ERROR_NUMBER(28); return FALSE; }
	// а есть ли ЦАП на данном экземпляре модуля?
	else if(ModuleDescriptor.IsDacPresented == DAC_INACCESSIBLED_E140)  { LAST_ERROR_NUMBER(15); return FALSE; }
	// всё в порядке - запускаем потоковый вывод на ЦАП
	else
	{
		WORD InBuf[4] = { 0x0, V_START_DAC, 0x0, 0x0 };

		// делаем двойную зачистку Bulk-канала приёма данных по USB (на всякий случай)
//		if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE1/*reset Write Pipe*/, NULL, NULL, NULL, NULL, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
//		else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE1/*reset Write Pipe*/, NULL, NULL, NULL, NULL, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
		// шлём USB-запрос в MCU на запуск потоковой работы ЦАП
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	}
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// останов потоковый работы ЦАП
//------------------------------------------------------------------------------
BOOL WINAPI TLE140::STOP_DAC(void)
{
	// проверяем название модуля
	if(strcmp((char *)ModuleDescriptor.Name, "E140")) { LAST_ERROR_NUMBER(27); return FALSE; }
	// у модуля ревизии 'A' нет потокового вывода на ЦАП
	else if(ModuleDescriptor.Revision == 'A') { LAST_ERROR_NUMBER(28); return FALSE; }
	// а есть ли ЦАП на данном экземпляре модуля?
	else if(ModuleDescriptor.IsDacPresented == DAC_INACCESSIBLED_E140)  { LAST_ERROR_NUMBER(15); return FALSE; }
	else
	{
		WORD InBuf[4] = { 0x0, V_STOP_DAC, 0x0, 0x0 };

		// шлём USB-запрос в MCU на останов потоковой работы ЦАП
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
		// делаем двойную зачистку Bulk-канала приёма данных по USB (на всякий случай)
		else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE1/*reset Write Pipe*/, NULL, 0x0, NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
		else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE1/*reset Write Pipe*/, NULL, 0x0, NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	}
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// потоковая передача данных ЦАП в модуль
//------------------------------------------------------------------------------
BOOL WINAPI TLE140::WriteData(IO_REQUEST_LUSBAPI * const WriteRequest)
{
	DWORD NumberOfBytesWritten;

	// проверим буфер данных
	if(!WriteRequest->Buffer) { LAST_ERROR_NUMBER(2); return FALSE; }
	// число отсчетов должно находиться в диапазоне от 0x100(256)
	else if(WriteRequest->NumberOfWordsToPass < 0x100) WriteRequest->NumberOfWordsToPass = 0x100;
	// и вплоть до 0x100000(1024*1024)
	else if(WriteRequest->NumberOfWordsToPass > (1024*1024)) WriteRequest->NumberOfWordsToPass = 1024*1024;
	// также число отсчетов должно быть кратно 0x100(256)
	WriteRequest->NumberOfWordsToPass -= (WriteRequest->NumberOfWordsToPass)%256;

	if(WriteRequest->Overlapped)
	{
		// посылаем асинхронный запрос на вывод необходимого кол-ва данных
		NumberOfBytesWritten = 0x0;
		if(!WriteFile(hDevice, WriteRequest->Buffer, 2*WriteRequest->NumberOfWordsToPass, &NumberOfBytesWritten, WriteRequest->Overlapped))
			{ if(GetLastError() != ERROR_IO_PENDING) { LAST_ERROR_NUMBER(60); CancelIo(hDevice); return FALSE; } }
		// кол-во отосланных отсчётов (в силу асинхронности запроса эта величина вполне может быть равна 0)
		WriteRequest->NumberOfWordsPassed = NumberOfBytesWritten/2;
	}
	else
	{
		OVERLAPPED Ov;

		// инициализируем OVERLAPPED структуру
		ZeroMemory(&Ov, sizeof(OVERLAPPED));
		// создаём событие
		Ov.hEvent = CreateEvent(NULL, FALSE , FALSE, NULL);
		if(!Ov.hEvent) { LAST_ERROR_NUMBER(33); return FALSE; }

		// посылаем асинхронный запрос на сбор необходимого кол-ва данных
		NumberOfBytesWritten = 0x0;
		if(!WriteFile(hDevice, WriteRequest->Buffer, 2*WriteRequest->NumberOfWordsToPass, &NumberOfBytesWritten, &Ov))
			{ if(GetLastError() != ERROR_IO_PENDING) { LAST_ERROR_NUMBER(40); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; } }
		// ждём окончания выполнения запроса
		if(WaitForSingleObject(Ov.hEvent, WriteRequest->TimeOut) == WAIT_TIMEOUT) { LAST_ERROR_NUMBER(34); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; }
		// попробуем получить кол-во реально переданных байт данных
		else if(!GetOverlappedResult(hDevice, &Ov, &NumberOfBytesWritten, TRUE)) { LAST_ERROR_NUMBER(35); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; }
		// закроем событие
		else if(!CloseHandle(Ov.hEvent)) { LAST_ERROR_NUMBER(36); return FALSE; }
		// кол-во отосланных отсчётов
		WriteRequest->NumberOfWordsPassed = NumberOfBytesWritten/2;
		// сравним сколько было реально отослано данных (в словах) с требуемым кол-вом
		if(WriteRequest->NumberOfWordsToPass != WriteRequest->NumberOfWordsPassed) { LAST_ERROR_NUMBER(37); return FALSE; }
	}
	// все хорошо :)))))
	return TRUE;
}

// -----------------------------------------------------------------------------
// однократный вывод на заданный канал ЦАП
// используется 12ти битный код ЦАП - как для модуля E-14-140 Rev.'A'
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::DAC_SAMPLE(SHORT * const DacData, WORD DacChannel)
{
	SHORT DacParam;

	// проверяем название модуля
	if(strcmp((char *)ModuleDescriptor.Name, "E140")) { LAST_ERROR_NUMBER(27); return FALSE; }
	// а есть ли ЦАП на данном экземпляре модуля
	else if(ModuleDescriptor.IsDacPresented == DAC_INACCESSIBLED_E140) { LAST_ERROR_NUMBER(15); return FALSE; }
	// проверим номер канала ЦАП
	else if(DacChannel >= DAC_CHANNELS_QUANTITY_E140) { LAST_ERROR_NUMBER(16); return FALSE; }

	// подготовим значение для вывода на ЦАП
	if((*DacData) < -2048) *DacData = -2048;
	else if((*DacData) > 2047) *DacData = 2047;
	// формируем соотвестсвующее слово для передачи в MCU модуля
	DacParam = (SHORT)((*DacData) & 0xFFF);
	DacParam |= (SHORT)(DacChannel << 12);
	// передаём в MCU модуля
	if(!PutArrayToMcu((BYTE*)&DacParam, 0x2, DAC_SAMPLE_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
// однократный вывод сразу на два канала ЦАП
// используется 16ти битный код ЦАП - как для модуля E-14-140 Rev.'B' и выше
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::DAC_SAMPLES(SHORT * const DacData1, SHORT * const DacData2)
{
	SHORT DacSamples[0x2];

	// проверяем название модуля
	if(strcmp((char *)ModuleDescriptor.Name, "E140")) { LAST_ERROR_NUMBER(27); return FALSE; }
	// модуль ревизии 'A' не поддерживает однократный вывод на оба канала ЦАП
	else if(ModuleDescriptor.Revision == 'A') { LAST_ERROR_NUMBER(28); return FALSE; }
	// а есть ли ЦАП на данном экземпляре модуля
	else if(ModuleDescriptor.IsDacPresented == DAC_INACCESSIBLED_E140) { LAST_ERROR_NUMBER(15); return FALSE; }
	// заполним буфер
	DacSamples[0x0] = *DacData1; DacSamples[0x1] = *DacData2;
	// передаём в MCU модуля
	if(!PutArrayToMcu((BYTE *)DacSamples, 0x4, DAC_SAMPLES_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}




// =============================================================================
//  функции для работы с ТТЛ линиями на внешнем цифровом разъёме
// =============================================================================
// -----------------------------------------------------------------------------
//  функция разрешения выходных линий внешнего цифрового разъёма
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::ENABLE_TTL_OUT(BOOL EnableTtlOut)
{
	BYTE Flag = (BYTE)(EnableTtlOut ? 0x1 : 0x0);

	// пишем флаг доступности цифровых выходов
	if(!PutArrayToMcu(&Flag, 0x1, DOUT_ENABLE_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
//   функция чтения входных линии внешнего цифрового разъёма
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::TTL_IN(WORD * const TtlIn)
{
	// читаем состояние цифровых входов
	if(!GetArrayFromMcu((BYTE *)TtlIn, 0x2, DIN_REGISTER)) { LAST_ERROR_NUMBER(2); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
//  функция вывода на выходные линии внешнего цифрового разъёма
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::TTL_OUT(WORD TtlOut)
{
	// пишем состояние цифровых выходов
	if(!PutArrayToMcu((BYTE*)&TtlOut, 0x2, DOUT_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}




// =============================================================================
//  функции для работы с пользовательской информацией ППЗУ
// =============================================================================
// -----------------------------------------------------------------------------
// разрешение/запрещение режима записи в пользовательскую область ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled)
{
	BYTE Flag = (BYTE)(IsUserFlashWriteEnabled ? 0x1 : 0x0);

	// передадим признак разрешения/запрещения записи в ППЗУ
	if(!PutArrayToMcu(&Flag, 0x1, ENABLE_FLASH_WRITE_FLAG)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// запомним признак
	TLE140::IsUserFlashWriteEnabled = IsUserFlashWriteEnabled;
	// всё хорошо
	return TRUE;
}

// -----------------------------------------------------------------------------
// чтенние слова из пользовательской области ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::READ_FLASH_ARRAY(USER_FLASH_E140 * const UserFlash)
{
	// проверим структуру
	if(!UserFlash) { LAST_ERROR_NUMBER(1); return FALSE; }
	// тупо читаем массив
	else if(!GetArrayFromMcu((BYTE *)UserFlash, sizeof(USER_FLASH_E140), USER_FLASH_ADDRESS)) { LAST_ERROR_NUMBER(2); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

// -----------------------------------------------------------------------------
// запись слова в пользовательскую область ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::WRITE_FLASH_ARRAY(USER_FLASH_E140 * const UserFlash)
{
	// проверим флажок сбора данных - во время сбора данных нельзя ничего записывать в пользовательское ППЗУ
	// предварительно необходимо вызвать функцию STOP_ADC()
	if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(3); return FALSE; }
	// запись должна быть разрешена
	else if(!IsUserFlashWriteEnabled) { LAST_ERROR_NUMBER(18); return FALSE; }
	// проверим структуру
	else if(!UserFlash) { LAST_ERROR_NUMBER(1); return FALSE; }
	// тупо читаем массив
	else if(!PutArrayToMcu((BYTE *)UserFlash, sizeof(USER_FLASH_E140), USER_FLASH_ADDRESS)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}





// =============================================================================
//  функции для работы со служебной информацией ППЗУ
// =============================================================================
// -----------------------------------------------------------------------------
//  получим служебную информацию о модуле из ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E140 * const md)
{
	char *pTag;
	BYTE Buffer[128];
	BYTE Str[17];
	WORD i;

	// проверим структуру
	if(!md) { LAST_ERROR_NUMBER(1); return FALSE; }
	// считываем упакованный дескриптор модуля из ППЗУ
	else if(!GetArrayFromMcu(Buffer, sizeof(Buffer), DESCRIPTOR_BASE)) { LAST_ERROR_NUMBER(2); return FALSE; }
	// распаковываем дескриптор модуля
	else if(!UnpackModuleDescriptor(Buffer, &ModuleDescriptor)) { LAST_ERROR_NUMBER(19); return FALSE; }

	// Module Info
	strncpy((char *)md->Module.CompanyName,			"L-Card Ltd.",   								sizeof(md->Module.CompanyName));
	if(!strcmp((char *)ModuleDescriptor.Name,			"E140")) strcpy((char *)md->Module.DeviceName, "E14-140");
	else strncpy((char *)md->Module.DeviceName,		(char *)ModuleDescriptor.Name,	 		std::min(sizeof(md->Module.DeviceName), sizeof(ModuleDescriptor.Name)));
	strncpy((char *)md->Module.SerialNumber,			(char *)ModuleDescriptor.SerialNumber,	std::min(sizeof(md->Module.SerialNumber), sizeof(ModuleDescriptor.SerialNumber)));
	strcpy((char *)md->Module.Comment,					"Внешний модуль АЦП/ЦАП/ТТЛ общего назначения на шину USB 1.1");
	// получим ревизию модуля
	md->Module.Revision = ModuleDescriptor.Revision;
	// модуль E14-140 существует в единственном исполнении (по крайне мере пока)
	md->Module.Modification = NO_MODULE_MODIFICATION_LUSBAPI;

	// Interface Info
	md->Interface.Active = TRUE;
	strcpy((char *)md->Interface.Name, "USB 1.1");
	strcpy((char *)md->Interface.Comment, "Full-Speed Mode (12 Mbit/s)");

	// MCU Info
	if(!GetArrayFromMcu(Buffer, 0x10, FIRMWARE_VERSION)) { LAST_ERROR_NUMBER(2); return FALSE; }
	md->Mcu.Active = TRUE;
	sprintf((char *)Str, "%02u.%02u", Buffer[1], Buffer[0]);
	strncpy((char *)md->Mcu.Version.Version,			(char *)Str, 		std::min(sizeof(md->Mcu.Version.Version), sizeof(Str)));
	strncpy((char *)md->Mcu.Version.Date,				(char *)(Buffer+2), 14);
	md->Mcu.ClockRate = ModuleDescriptor.ClockRate/1000.0;		// в кГц
	if(ModuleDescriptor.Revision == 'A')
	{
		strcpy((char *)md->Mcu.Name, 							"AVR ATmega8515");
		strcpy((char *)md->Mcu.Version.Manufacturer,		"L-Card Ltd.");
		strcpy((char *)md->Mcu.Version.Author,				"Kodorkin A.V.");
		strncpy((char *)md->Mcu.Version.Comment,			"Штатная прошивка для модуля E14-140 Rev.'A'", sizeof(md->Mcu.Version.Comment));
		strncpy((char *)md->Mcu.Comment,						"8-bit Microcontroller with 8K Bytes In-System Programmable Flash", sizeof(md->Mcu.Comment));
	}
	else if(ModuleDescriptor.Revision == 'B')
	{
		strcpy((char *)md->Mcu.Name, 							"ARM AT91SAM7S256");
		strcpy((char *)md->Mcu.Version.Manufacturer,		"L-Card Ltd.");
		strcpy((char *)md->Mcu.Version.Author,				"Емельянов А.С.");
		strncpy((char *)md->Mcu.Version.Comment,			"Штатная прошивка для модуля E14-140 Rev.'B'", sizeof(md->Mcu.Version.Comment));
		strncpy((char *)md->Mcu.Comment, 					"32-bit ARM Thumb-based Microcontroller with 256K Bytes Programmable Flash", sizeof(md->Mcu.Comment));
	}
	else { LAST_ERROR_NUMBER(23); return FALSE; }
	// теперь определимся с номером версии Firmware микроконтроллера
	FirmwareVersionNumber = strtod((char *)md->Mcu.Version.Version, &pTag);

	// ADC Info
	md->Adc.Active = TRUE;
	strcpy((char *)md->Adc.Name, "LTC1416");
	// смещение при усилении i
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E140; i++)
	{
		md->Adc.OffsetCalibration[i] = ModuleDescriptor.AdcOffsetCoefs[i];
		md->Adc.ScaleCalibration[i] = ModuleDescriptor.AdcScaleCoefs[i];
	}
	strcpy((char *)md->Adc.Comment, "14-Bit 400Ksps ADC converter");

	// DAC Info
	if(ModuleDescriptor.IsDacPresented)
	{
		md->Dac.Active = TRUE;
		strcpy((char *)md->Dac.Name, "DAC8581");
		for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E140; i++)
		{
			md->Dac.OffsetCalibration[i] = ModuleDescriptor.DacOffsetCoefs[i];
			md->Dac.ScaleCalibration[i] = ModuleDescriptor.DacScaleCoefs[i];
		}
		strcpy((char *)md->Dac.Comment, "16-bit, High-Speed, Low-Noise DAC converter");
	}
	else md->Dac.Active = FALSE;

	// IO Info
	md->DigitalIo.Active = TRUE;
	strcpy((char *)md->DigitalIo.Name, "74HCT165/74HC595");
	md->DigitalIo.InLinesQuantity = 16;
	md->DigitalIo.OutLinesQuantity = 16;
	strcpy((char *)md->DigitalIo.Comment, "TTL/CMOS compatible");

	// всё хорошо
	return TRUE;
}

// -----------------------------------------------------------------------------
//  запишем служебную информацию о модуле в ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E140 * const md)
{
	BYTE Buffer[128];
	WORD i;

	strcpy((char *)ModuleDescriptor.Name, 				"E140");
//	strncpy((char *)ModuleDescriptor.Name, 			(char *)md->Module.DeviceName,	std::min(sizeof(md->Module.DeviceName), sizeof(ModuleDescriptor.Name)));
	strncpy((char *)ModuleDescriptor.SerialNumber,	(char *)md->Module.SerialNumber, std::min(sizeof(md->Module.SerialNumber), sizeof(ModuleDescriptor.SerialNumber)));

	// проверим требуемую ревизию модуля
	if((	md->Module.Revision > REVISIONS_E140[REVISIONS_QUANTITY_E140 - 0x1] &&
			md->Module.Revision <= 'Z')) { LAST_ERROR_NUMBER(23); return FALSE; }
	ModuleDescriptor.Revision = md->Module.Revision;

	ModuleDescriptor.IsDacPresented	= (BYTE)(md->Dac.Active ? 0x1 : 0x0);
	ModuleDescriptor.ClockRate		= (DWORD)(1000.0 * md->Mcu.ClockRate + 0.5);		// в Гц

	// корректировочные коэффициетны АЦП
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E140; i++)
	{
		// смещение АЦП при усилении i
		if((fabs(md->Adc.OffsetCalibration[i]) > 8191.0) ||
			(fabs(md->Adc.OffsetCalibration[i]) < FloatEps)) ModuleDescriptor.AdcOffsetCoefs[i] = 0.0;
		else ModuleDescriptor.AdcOffsetCoefs[i] = md->Adc.OffsetCalibration[i];
		// масштаб АЦП при усилении i
		if((fabs(md->Adc.ScaleCalibration[i]) > 2.0) ||
			(fabs(md->Adc.ScaleCalibration[i]) < FloatEps)) ModuleDescriptor.AdcScaleCoefs[i] = 1.0;
		else ModuleDescriptor.AdcScaleCoefs[i] = md->Adc.ScaleCalibration[i];
	}
	// корректировочные коэффициетны ЦАП
	for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E140; i++)
	{
		// проверим - есть ли ЦАП на борту
		if(md->Dac.Active)
		{
			// смещение ЦАП канала i
			if((fabs(md->Dac.OffsetCalibration[i]) > 2047.0) ||
				(fabs(md->Dac.OffsetCalibration[i]) < FloatEps)) ModuleDescriptor.DacOffsetCoefs[i] = 0.0;
			else ModuleDescriptor.DacOffsetCoefs[i] = md->Dac.OffsetCalibration[i];
			// масштаб ЦАП канала i
			if((fabs(md->Dac.ScaleCalibration[i]) > 2.0) ||
				(fabs(md->Dac.ScaleCalibration[i]) < FloatEps)) ModuleDescriptor.DacScaleCoefs[i] = 1.0;
			else ModuleDescriptor.DacScaleCoefs[i] = md->Dac.ScaleCalibration[i];
		}
		else
		{
			// смещение ЦАП канала i
			ModuleDescriptor.DacOffsetCoefs[i] = 0.0;
			// масштаб ЦАП канала i
			ModuleDescriptor.DacScaleCoefs[i] = 1.0;
		}
	}

	// проверим структуру
	if(!md) { LAST_ERROR_NUMBER(1); return FALSE; }
	// упаковываем дескриптор модуля
	else if(!PackModuleDescriptor(Buffer, &ModuleDescriptor)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// разрешим процедуру записи в ППЗУ
	if(!ENABLE_FLASH_WRITE(TRUE)) { LAST_ERROR_NUMBER(21); return FALSE; }
	// пробуем записать дескриптор в ППЗУ
	else if(!PutArrayToMcu(Buffer, sizeof(Buffer), DESCRIPTOR_BASE)) { LAST_ERROR_NUMBER(4); ENABLE_FLASH_WRITE(FALSE); return FALSE; }
	// запретим процедуру записи в ППЗУ
	else if(!ENABLE_FLASH_WRITE(FALSE)) { LAST_ERROR_NUMBER(21); return FALSE; }
	// обновляем поля сруктуры
	else if(!UnpackModuleDescriptor(Buffer, &ModuleDescriptor)) { LAST_ERROR_NUMBER(19); return FALSE; }
	// всё хорошо
	else return TRUE;
}





// =============================================================================
//  функции для работы со служебной информацией ППЗУ
// =============================================================================
// -----------------------------------------------------------------------------
// чтение массива данных из памяти микроконтроллера
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::GetArray(BYTE * const Buffer, WORD Size, WORD Address)
{
	// у модуля ревизии 'A' нет общего доступа к микроконтроллеру
	if(ModuleDescriptor.Revision != 'A')
	{
		// читаем массив данных из микроконтрллера модуля
		if(!GetArrayFromMcu(Buffer, Size, Address)) { return FALSE; }
		// все хорошо :)))))
		else return TRUE;
	}
	else { LAST_ERROR_NUMBER(28); return FALSE; }
}

// -----------------------------------------------------------------------------
// запись массива данных в память микроконтроллера
// -----------------------------------------------------------------------------
BOOL WINAPI TLE140::PutArray(BYTE * const Buffer, WORD Size, WORD Address)
{
	// у модуля ревизии 'A' нет общего доступа к микроконтроллеру
	if(ModuleDescriptor.Revision != 'A')
	{
		// читаем массив данных из микроконтрллера модуля
		if(!PutArrayToMcu(Buffer, Size, Address)) { return FALSE; }
		// все хорошо :)))))
		else return TRUE;
	}
	else { LAST_ERROR_NUMBER(28); return FALSE; }
}






// =============================================================================
//  внутренние функции для работы с модулем
// =============================================================================
//------------------------------------------------------------------------------
// инициализация всех необходимых переменных
//------------------------------------------------------------------------------
BOOL TLE140::InitVars(void)
{
	// инициализация всех локальных структур класса
	ZeroMemory(&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR));
	ZeroMemory(&DacPars, sizeof(DAC_PARS));
	// инициализация полей структуры параметров АЦП
	AdcPars.AdcRate = 100.0;
	AdcPars.ChannelsQuantity = 0x1;
	ZeroMemory(AdcPars.ControlTable, sizeof(AdcPars.ControlTable));
	// сбросим флажок сбора данных
	IsDataAcquisitionInProgress = FALSE;
	// сбросим номер версии Firmware микроконтроллера
	FirmwareVersionNumber = 0.0;

	// всё хорошо
	return TRUE;
}

// -----------------------------------------------------------------------------
// состояние аналогового питания узлов модуля при переходе шины USB в режим suspend
// -----------------------------------------------------------------------------
BOOL TLE140::SetSuspendModeFlag(BOOL SuspendModeFlag)
{
	BYTE Flag = (BYTE)(SuspendModeFlag ? 0x1 : 0x0);

	if(!PutArrayToMcu(&Flag, 0x1, SUSPEND_MODE_FLAG)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
// чтение массива данных из памяти микроконтроллера
// -----------------------------------------------------------------------------
BOOL TLE140::GetArrayFromMcu(BYTE * const Buffer, WORD Size, WORD Address)
{
	DWORD offs;

	// проверим буфер и размер
	if(!Buffer || !Size) { LAST_ERROR_NUMBER(1); return FALSE; }

	for(offs = 0x0; offs < (DWORD)Size; offs += MAX_USB_CONTROL_PIPE_BLOCK)
	{
		WORD InBuf[4] = {0x0001, V_GET_ARRAY, (WORD)(Address+offs), 0x0};

		if(!TLUSBBASE::LDeviceIoControl(
									DIOC_SEND_COMMAND,
									&InBuf, sizeof(InBuf),
									Buffer + offs, std::min(Size - offs, (DWORD)MAX_USB_CONTROL_PIPE_BLOCK),
									TimeOut)) { LAST_ERROR_NUMBER(5); return FALSE; }
	}
	// все хорошо :)))))
	return TRUE;
}

// -----------------------------------------------------------------------------
// запись массива данных в память микроконтроллера
// -----------------------------------------------------------------------------
BOOL TLE140::PutArrayToMcu(BYTE * const Buffer, WORD Size, WORD Address)
{
	DWORD offs;

	// проверим буфер и размер
	if(!Buffer || !Size) { LAST_ERROR_NUMBER(1); return FALSE; }

	for(offs = 0x0; offs < (DWORD)Size; offs += MAX_USB_CONTROL_PIPE_BLOCK)
	{
		WORD InBuf[4] = {0x0000, V_PUT_ARRAY, (WORD)(Address+offs), (WORD)0x0 };

		if(!TLUSBBASE::LDeviceIoControl(
									DIOC_SEND_COMMAND,
									&InBuf, sizeof(InBuf),
									Buffer + offs, std::min(Size - offs, (DWORD)MAX_USB_CONTROL_PIPE_BLOCK),
									TimeOut)) { LAST_ERROR_NUMBER(5); return FALSE; }
	}

	// все хорошо :)))))
	return TRUE;
}

// -----------------------------------------------------------------------------
//  подсчет кс8
// -----------------------------------------------------------------------------
#define CRC8INIT         0x55
BYTE TLE140::CRC8ADD(BYTE a, BYTE b)
{
	return (BYTE)(a + b);
}
//
BYTE TLE140::CRC8CALC(BYTE *Buffer, WORD Size)
{
	BYTE crc = CRC8INIT;
	while(Size--) crc = CRC8ADD(crc, *Buffer++);
	return crc;
}

// -----------------------------------------------------------------------------
//  упаковка дескриптора модуля
// -----------------------------------------------------------------------------
BOOL TLE140::PackModuleDescriptor(BYTE * const Buffer, MODULE_DESCRIPTOR * const ModuleDescriptor)
{
	WORD i;
	BYTE c;
	DWORD k, n;

	// отсканируем серийный номер модуля
	if(sscanf((char *)ModuleDescriptor->SerialNumber, "%lu%c%lu", &k, &c, &n) != 0x3) { LAST_ERROR_NUMBER(22); return FALSE; }
	// обнулим буфер
	ZeroMemory(Buffer, 128);
	// серийный номер изделия
	Buffer[0] = (BYTE)k;
	Buffer[1] = c;
	*(DWORD*)(Buffer+2) = n;
	// название изделия
	sprintf((char *)(Buffer+6), "%.10s", ModuleDescriptor->Name);
	// ревизия изделия
	Buffer[16] = (BYTE)(ModuleDescriptor->Revision + (((ModuleDescriptor->Revision >= 'a') && (ModuleDescriptor->Revision <= 'z')) ? ('A'-'a') : 0));
	// тип установленного процессора
	sprintf((char *)(Buffer+17), "%.10s", ModuleDescriptor->CpuType);
	// частота работы процессора
	*(DWORD*)(Buffer+27) = ModuleDescriptor->ClockRate;
	// флаг присутствия ЦАП
	Buffer[32] = (BYTE)(ModuleDescriptor->IsDacPresented ? 0x1 : 0x0);
	// упаковка коэффициентов коррекции АЦП
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E140; i++)
	{
		*(float*)(Buffer + 33 + (i + 0)*sizeof(float)) = (float)ModuleDescriptor->AdcOffsetCoefs[i];
		*(float*)(Buffer + 33 + (i + ADC_CALIBR_COEFS_QUANTITY_E140)*sizeof(float)) = (float)ModuleDescriptor->AdcScaleCoefs[i];
	}
	// упаковка коэффициентов коррекции ЦАП
	for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E140; i++)
	{
		*(float*)(Buffer + 65 + (i + 0)*sizeof(float)) = (float)ModuleDescriptor->DacOffsetCoefs[i];
		*(float*)(Buffer + 65 + (i + DAC_CALIBR_COEFS_QUANTITY_E140)*sizeof(float)) = (float)ModuleDescriptor->DacScaleCoefs[i];
	}
	// контрольная сумма первой системной области
	Buffer[31]  = CRC8CALC(Buffer+0, 31);
	// контрольная сумма второй системной области
	Buffer[127] = CRC8CALC(Buffer+32, 95);
	// всё хорошо
	return TRUE;
}

// -----------------------------------------------------------------------------
//  распаковка дескриптора модуля
// -----------------------------------------------------------------------------
BOOL TLE140::UnpackModuleDescriptor(BYTE * const Buffer, MODULE_DESCRIPTOR * const ModuleDescriptor)
{
	WORD i;

	// проверим контрольную сумму
	if(CRC8CALC(Buffer+0, 31)  != Buffer[31]) { LAST_ERROR_NUMBER(24); return FALSE; }
	else if(CRC8CALC(Buffer+32, 95) != Buffer[127]) { LAST_ERROR_NUMBER(24); return FALSE; }
	// серийный номер изделия
	sprintf((char *)(ModuleDescriptor->SerialNumber), "%lu%c%lu", (DWORD)Buffer[0], Buffer[1], (*(DWORD*)(Buffer+2)) & 0xFFFFFFL);
	// название изделия
	sprintf((char *)(ModuleDescriptor->Name), "%.10s", Buffer+6);
	// ревизия изделия
	ModuleDescriptor->Revision = Buffer[16];
	// тип установленного процессора
	sprintf((char *)(ModuleDescriptor->CpuType), "%.10s", Buffer+17);
	// частота работы процессора
	ModuleDescriptor->ClockRate = *(DWORD*)(Buffer+27);
	// флаг присутствия ЦАП на борту модуля
	ModuleDescriptor->IsDacPresented = Buffer[32] ? true : false;
	// распаковка коэффициентов коррекции АЦП
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E140; i++)
	{
		ModuleDescriptor->AdcOffsetCoefs[i]	= *(float*)(Buffer + 33 + (i + 0)*sizeof(float));
		ModuleDescriptor->AdcScaleCoefs[i]	= *(float*)(Buffer + 33 + (i + ADC_CALIBR_COEFS_QUANTITY_E140)*sizeof(float));
	}
	// распаковка коэффициентов коррекции ЦАП
	for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E140; i++)
	{
		ModuleDescriptor->DacOffsetCoefs[i]	= (*(float*)(Buffer + 65 + (i + 0)*sizeof(float)));
		ModuleDescriptor->DacScaleCoefs[i]	= (*(float*)(Buffer + 65 + (i + DAC_CALIBR_COEFS_QUANTITY_E140)*sizeof(float)));
	}
	// всё хорошо
	return TRUE;
}

