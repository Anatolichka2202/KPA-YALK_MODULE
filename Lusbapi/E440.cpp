//------------------------------------------------------------------------------
#if defined(_MSC_VER)
	#define NOMINMAX	// https://demin.ws/blog/russian/2009/02/15/min-max-in-visual-studio/
#endif
#include <math.h>
#include <io.h>
#include <stdio.h>
#include <fcntl.h>
#include <algorithm>
#include "e440.h"
#include "ErrorBaseIds.h"
//------------------------------------------------------------------------------

#define 	LAST_ERROR_NUMBER(ErrorNumber)	LastErrorNumber = E440_BASE_ERROR_ID + ErrorNumber

//------------------------------------------------------------------------------
// коструктор
//------------------------------------------------------------------------------
TLE440::TLE440(HINSTANCE hInst) : TLUSBBASE(hInst)
{
	// проинициализируем все необходимые переменные
	InitVars();
}

//------------------------------------------------------------------------------
// деструктор
//------------------------------------------------------------------------------
TLE440::~TLE440() { }




//==============================================================================
// Функции общего назначения для работы с модулем Е14-440
//==============================================================================
//------------------------------------------------------------------------------
// откроем виртуальный слот для доступа к USB модулю
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::OpenLDevice(WORD VirtualSlot)
{
	char ModuleName[LONG_MODULE_NAME_STRING_LENGTH];

	// проинициализируем все необходимые переменные
	InitVars();
	// сбросим последнюю ошибку
	TLUSBBASE::ResetLastErrorInfo();
	//  попробуем открыть виртуальный слот для доступа к USB модулю
	if(!TLUSBBASE::OpenLDeviceByID(VirtualSlot, E14_440_ID)) return FALSE;
	// попробуем прочитать название модуля
	else if(!TLUSBBASE::GetModuleName(ModuleName)) { TLUSBBASE::CloseLDevice(); return FALSE; }
	// убедимся, что это модуль E14-440
	else if(strcmp(ModuleName, "E440")) { TLUSBBASE::CloseLDevice(); LAST_ERROR_NUMBER(0); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------------
// освободим текущий виртуальный слот
//------------------------------------------------------------------------------------
BOOL WINAPI TLE440::CloseLDevice(void)
{
	// если нужно - освободим локальный буфер
//	if(IoReq.Buffer) { delete[] IoReq.Buffer; IoReq.Buffer = NULL; }
	// закроем устройство
	if(!TLUSBBASE::CloseLDevice()) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------------
// освободим указатель на устройство
//------------------------------------------------------------------------------------
BOOL WINAPI TLE440::ReleaseLInstance(void)
{
	BOOL Status = TRUE;

	// сбросим все дополнительные аттрибуты работы микроконтроллера
	if(!SetMcuAttributes(NO_MCU_ATTRIBUTES)) { LAST_ERROR_NUMBER(1); Status = FALSE; }
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
HANDLE WINAPI TLE440::GetModuleHandle(void) { return hDevice; }

//------------------------------------------------------------------------------
//  Чтение название модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::GetModuleName(char * const ModuleName)
{
	// проверим буфер
	if(!ModuleName) { LAST_ERROR_NUMBER(2); return FALSE; }
	// теперь попробуем узнать название модуля
	else if(!TLUSBBASE::GetModuleName(ModuleName)) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------------
//  Получение текущей скорости работы шины USB
//------------------------------------------------------------------------------------
BOOL WINAPI TLE440::GetUsbSpeed(BYTE * const UsbSpeed)
{
	// проверим указатель
	if(!UsbSpeed) { LAST_ERROR_NUMBER(2); return FALSE; }
	// теперь попробуем узнать скорость работы USB шины
	else if(!TLUSBBASE::GetUsbSpeed(UsbSpeed)) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// управления режимом низкого электропотребления модуля E14-440
// но т.к. данный режим не поддерживается модулем, то всегда возвращаем ошибку
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::LowPowerMode(BOOL LowPowerFlag)
{
	if(!TLUSBBASE::LowPowerMode(LowPowerFlag)) return FALSE;
   else return TRUE;
}

//------------------------------------------------------------------------------
// Функция выдачи строки с последней ошибкой
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::GetLastErrorInfo(LAST_ERROR_INFO_LUSBAPI * const LastErrorInfo)
{
	BOOL Status;

	// вычитываем название ошибки
	Status = TLUSBBASE::GetLastErrorInfo(LastErrorInfo);
	// если можно - сбросим последнюю ошибку
	if(Status) TLUSBBASE::ResetLastErrorInfo();
	// озвращаем статус выполнения функции
	return Status;
}



//==============================================================================
// Функции для работы с DSP модуля Е14-440
//==============================================================================
//------------------------------------------------------------------------------
// сброс модуля (DSP, ПЛИС и вторичное питание)
//  0x0 - режим переинициализации модуля, после чего модуль снова готов к загрузке DSP
//  0x1 - режим полного сброса модуля (т.е. DSP находится в состоянии
//		                                         сброса, вторичное питание отключено)
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::RESET_MODULE(BYTE ResetFlag)
{
	WORD InBuf[4] = { 0x0, V_RESET_MODULE, ResetFlag, 0x0 };

	// проверим допустимость аргумента функции
	if(ResetFlag >= INVALID_RESET_TYPE_E440) { LAST_ERROR_NUMBER(61); return FALSE; }
	// посылаем USB-запрос V_RESET_MODULE в MCU
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND,
													&InBuf, sizeof(InBuf),
													NULL, 0x0/*bytes*/,
													TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------------
// функция загрузки LBIOS в DSP модуля E14-440
//------------------------------------------------------------------------------------
BOOL WINAPI TLE440::LOAD_MODULE(const char * const FileName)
{
	// посмотрим откуда требуется загружать LBIOS
	if(FileName == NULL)
	{
		// пробуем загрузить DSP модуля из ресурса библиотеки Lusbapi
		if(!LoadModuleFromResource()) { return FALSE; }
	}
	else
	{
		// пробуем загрузить DSP модуля из загрузочного ППЗУ
		if(!strcmpi(FileName, "flash"))
		{
			if(!LoadModuleFromBootFlash()) { LAST_ERROR_NUMBER(65); return FALSE; }
		}
		// пробуем загрузить DSP модуля из файла
		else { if(!LoadModuleFromFile(FileName)) { return FALSE; } }
	}
	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// проверка работоспособности загруженного модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::TEST_MODULE(void)
{
	WORD TMode1; if(!GET_LBIOS_WORD(L_TMODE1_E440, (SHORT *)&TMode1)) { LAST_ERROR_NUMBER(16); return FALSE; }
	WORD TMode2; if(!GET_LBIOS_WORD(L_TMODE2_E440, (SHORT *)&TMode2)) { LAST_ERROR_NUMBER(16); return FALSE; }
	if((TMode1 != 0x5555) || (TMode2 != 0xAAAA)) { LAST_ERROR_NUMBER(17);  return FALSE; }
	else
	{
		WORD Ready;
		WORD LocTimeOut = 50;

		// сбросим в DSP модуля тестувую переменную
		if(!PUT_LBIOS_WORD(L_TEST_LOAD_E440, 0x77BB)) { LAST_ERROR_NUMBER(18); return FALSE; }

		// ждём пока DSP модуля не выставит признак готовности
		do
		{
			Sleep(20);
			if(!GET_LBIOS_WORD(L_READY_E440, (SHORT *)&Ready)) { LAST_ERROR_NUMBER(16); return FALSE; }
		} while((!Ready) && (LocTimeOut--));
		if(TimeOut == 0xFFFF)  { LAST_ERROR_NUMBER(19); return FALSE; }

		// шлём в DSP модуля тестовую команду
		if(!SEND_COMMAND(C_TEST)) { LAST_ERROR_NUMBER(20); return FALSE; }
		// после выполнения тестовой команды в L_TEST_LOAD_E440 должно быть 0xAA55
		WORD TestLoadVar; if(!GET_LBIOS_WORD(L_TEST_LOAD_E440, (SHORT *)&TestLoadVar)) { LAST_ERROR_NUMBER(16); return FALSE; }
		if(TestLoadVar != 0xAA55) { LAST_ERROR_NUMBER(21); return FALSE; }
	}
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// посылка в модуль номера команды
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::SEND_COMMAND(WORD Command)
{
	WORD InBuf[4] = { 0x0, V_COMMAND_IRQ, 0x0, 0x0 };
	WORD LocCommand, TimeOut = 100;

	// проверим требуемый номер команды
	if(Command >= C_LAST_COMMAND) { LAST_ERROR_NUMBER(22); return FALSE; }

	// захватим ресурс критической секции
	EnterCriticalSection(&cs);

	// зашлем номер команды в DSP модуля
	if(!PUT_LBIOS_WORD(L_COMMAND_E440, Command)) { LAST_ERROR_NUMBER(18); LeaveCriticalSection(&cs); return FALSE; }
	// сгенерим командное прерывание в DSP модуля
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0x0, this->TimeOut)) { LAST_ERROR_NUMBER(3); LeaveCriticalSection(&cs); return FALSE; }
	// проверим, что команда опознана
	do
	{
		if(!GET_LBIOS_WORD(L_COMMAND_E440, (SHORT *)&LocCommand)) { LAST_ERROR_NUMBER(16); LeaveCriticalSection(&cs); return FALSE; }
		else if(LocCommand == 0xFFFF) { LAST_ERROR_NUMBER(58); LeaveCriticalSection(&cs); return FALSE; }
	} while(LocCommand && (TimeOut--));

	// освободим ресурс критической секции
	LeaveCriticalSection(&cs);

	// проверим таймаут выполнения команды
	if(TimeOut == 0xFFFF) { LAST_ERROR_NUMBER(23); return FALSE; }
	// всё хорошо
	else return TRUE;
}




//==============================================================================
// функции для работы с АЦП модуля
//==============================================================================

//------------------------------------------------------------------------------
// получение текущих параметров работы АЦП
//------------------------------------------------------------------------------
BOOL WINAPI WINAPI TLE440::GET_ADC_PARS(ADC_PARS_E440 * const AdcPars)
{
	WORD i;
	WORD IsAdcEnabled, IsCorrectionEnabled;
	WORD SclockDiv, InterKadrDelay;
	WORD AdcOffsetCoef, AdcScaleCoef;
	double DspClockout;

	// проверим структуру параметров работы АЦП
	if(!AdcPars) { LAST_ERROR_NUMBER(2); return FALSE; }

	// проверяем название модуля
	if(strcmp((char *)ModuleDescriptor.Name, "E440")) { LAST_ERROR_NUMBER(24); return FALSE; }
	else if(!GET_LBIOS_WORD(L_ADC_ENABLED_E440, (SHORT *)&IsAdcEnabled)) { LAST_ERROR_NUMBER(16); return FALSE; }
	else if(!GET_LBIOS_WORD(L_CORRECTION_ENABLED_E440, (SHORT *)&IsCorrectionEnabled)) { LAST_ERROR_NUMBER(16); return FALSE; }
	AdcPars->IsAdcEnabled = IsAdcEnabled ? TRUE : FALSE;
	AdcPars->IsCorrectionEnabled = IsCorrectionEnabled ? TRUE : FALSE;

	// параметры синхронизации ввода данных с АЦП
	if(!GET_LBIOS_WORD(L_INPUT_MODE_E440, (SHORT *)&AdcPars->InputMode)) { LAST_ERROR_NUMBER(16); return FALSE; }
	else if(!GET_LBIOS_WORD(L_SYNCHRO_AD_TYPE_E440, (SHORT *)&AdcPars->SynchroAdType)) { LAST_ERROR_NUMBER(16); return FALSE; }
	else if(!GET_LBIOS_WORD(L_SYNCHRO_AD_MODE_E440, (SHORT *)&AdcPars->SynchroAdMode)) { LAST_ERROR_NUMBER(16); return FALSE; }
	else if(!GET_LBIOS_WORD(L_SYNCHRO_AD_CHANNEL_E440, (SHORT *)&AdcPars->SynchroAdChannel)) { LAST_ERROR_NUMBER(16); return FALSE; }
	else if(!GET_LBIOS_WORD(L_SYNCHRO_AD_POROG_E440, (SHORT *)&AdcPars->SynchroAdPorog)) { LAST_ERROR_NUMBER(16); return FALSE; }

	// управляющая таблица
	if(!GET_LBIOS_WORD(L_CONTROL_TABLE_LENGHT_E440, (SHORT *)&AdcPars->ChannelsQuantity)) { LAST_ERROR_NUMBER(16); return FALSE; }
	else if(AdcPars->ChannelsQuantity > MAX_CONTROL_TABLE_LENGTH_E440) { LAST_ERROR_NUMBER(25); return FALSE; }
	for(i = 0x0; i < AdcPars->ChannelsQuantity; i++) if(!GET_LBIOS_WORD((WORD)(L_CONTROL_TABLE_E440 + i), (SHORT *)&AdcPars->ControlTable[i])) { LAST_ERROR_NUMBER(16); return FALSE; }

	// источник тактовых испульсов запуска АЦП
	if(!GET_LBIOS_WORD(L_ADC_CLOCK_SOURCE_E440, (SHORT *)&AdcPars->AdcClockSource)) { LAST_ERROR_NUMBER(18); return FALSE; }

	// временные параметры работы АЦП
	DspClockout = 2.0*ModuleDescriptor.QuartzFrequency/1000.0; // тактовая частота DSP в кГц
	if(fabs(DspClockout - 48000.0) > 0.00001) { LAST_ERROR_NUMBER(26); return FALSE; }
	if(!GET_LBIOS_WORD(L_ADC_RATE_E440, (SHORT *)&SclockDiv)) { LAST_ERROR_NUMBER(16); return FALSE; }
	AdcPars->AdcRate = DspClockout/(2.0*(WORD)(SclockDiv + 1.0));
	if(!GET_LBIOS_WORD(L_INTER_KADR_DELAY_E440, (SHORT *)&InterKadrDelay)) { LAST_ERROR_NUMBER(16); return FALSE; }
	AdcPars->InterKadrDelay = (WORD)(InterKadrDelay + 1.0)/AdcPars->AdcRate; // межкадровая задержка в мс
	AdcPars->KadrRate = 1.0/((AdcPars->ChannelsQuantity - 1.0)/AdcPars->AdcRate + AdcPars->InterKadrDelay);

	// FIFO буфер АЦП
	if(!GET_LBIOS_WORD(L_ADC_FIFO_BASE_ADDRESS_E440, (SHORT *)&AdcPars->AdcFifoBaseAddress)) { LAST_ERROR_NUMBER(16); return FALSE; }
	if(!GET_LBIOS_WORD(L_CUR_ADC_FIFO_LENGTH_E440, (SHORT *)&AdcPars->AdcFifoLength)) { LAST_ERROR_NUMBER(16); return FALSE; }

	// считаем калибровочные коэффициенты для АЦП
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E440; i++)
	{
		// смещение АЦП для усиления i
		if(!GET_LBIOS_WORD((WORD)(L_ZERO_E440 + i), (SHORT *)&AdcOffsetCoef)) { LAST_ERROR_NUMBER(16); return FALSE; }
		AdcPars->AdcOffsetCoefs[i] = AdcOffsetCoef;
		// масштаб АЦП для усиления i
		if(!GET_LBIOS_WORD((WORD)(L_SCALE_E440 + i), (SHORT *)&AdcScaleCoef)) { LAST_ERROR_NUMBER(16); return FALSE; }
		AdcPars->AdcScaleCoefs[i] = (double)AdcScaleCoef/(double)0x8000;
	}
	// сохраним параметры работы АЦП
	this->AdcPars = *AdcPars;
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// заполнение требуемых параметров работы АЦП
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::SET_ADC_PARS(ADC_PARS_E440 * const AdcPars)
{
	WORD i;
	WORD AdcOffsetCoef, AdcScaleCoef;
	DWORD SclockDiv, KadrDelay;
	double DspClockOut;

	// проверим структуру параметров работы АЦП
	if(!AdcPars) { LAST_ERROR_NUMBER(2); return FALSE; }
	// проверяем название модуля
	else if(strcmp((char *)ModuleDescriptor.Name, "E440")) { LAST_ERROR_NUMBER(24); return FALSE; }

	// параметры синхронизации ввода данных с АЦП
	if(!PUT_LBIOS_WORD(L_CORRECTION_ENABLED_E440, (SHORT)(AdcPars->IsCorrectionEnabled))) { LAST_ERROR_NUMBER(18); return FALSE; }
	else if(!PUT_LBIOS_WORD(L_INPUT_MODE_E440, AdcPars->InputMode)) { LAST_ERROR_NUMBER(18); return FALSE; }
	else if(AdcPars->InputMode == ANALOG_SYNC_E440)
	{
		if(!PUT_LBIOS_WORD(L_SYNCHRO_AD_TYPE_E440, AdcPars->SynchroAdType)) { LAST_ERROR_NUMBER(18); return FALSE; }
		if(!PUT_LBIOS_WORD(L_SYNCHRO_AD_MODE_E440, AdcPars->SynchroAdMode)) { LAST_ERROR_NUMBER(18); return FALSE; }
		if(!PUT_LBIOS_WORD(L_SYNCHRO_AD_CHANNEL_E440, AdcPars->SynchroAdChannel)) { LAST_ERROR_NUMBER(18); return FALSE; }
		if(!PUT_LBIOS_WORD(L_SYNCHRO_AD_POROG_E440, AdcPars->SynchroAdPorog)) { LAST_ERROR_NUMBER(18); return FALSE; }
	}

	// управляющая таблица
	if(!AdcPars->ChannelsQuantity) { LAST_ERROR_NUMBER(62); return FALSE; }
	else if(AdcPars->ChannelsQuantity > MAX_CONTROL_TABLE_LENGTH_E440) AdcPars->ChannelsQuantity = MAX_CONTROL_TABLE_LENGTH_E440;
	else if(!PUT_LBIOS_WORD(L_CONTROL_TABLE_LENGHT_E440, AdcPars->ChannelsQuantity)) { LAST_ERROR_NUMBER(18); return FALSE; }
	for(i = 0x0; i < AdcPars->ChannelsQuantity; i++) if(!PUT_LBIOS_WORD((WORD)(L_CONTROL_TABLE_E440 + i), (SHORT)AdcPars->ControlTable[i])) { LAST_ERROR_NUMBER(18); return FALSE; }

	// выбор источника тактовых импульсов запуска АЦП возможен только у модулей
	// ревизии 'F' и выше
	if((REVISIONS_E440[REVISION_A_E440] < ModuleDescriptor.Revision) &&
		(ModuleDescriptor.Revision < REVISIONS_E440[REVISION_F_E440])) AdcPars->AdcClockSource = INT_ADC_CLOCK_E440;
	// проверим источник тактовых импульсов АЦП
	else if(AdcPars->AdcClockSource > EXT_ADC_CLOCK_E440) { LAST_ERROR_NUMBER(64); return FALSE; }

	// передаём в LBIOS источник тактовых испульсов запуска АЦП
	if(!PUT_LBIOS_WORD(L_ADC_CLOCK_SOURCE_E440, AdcPars->AdcClockSource)) { LAST_ERROR_NUMBER(18); return FALSE; }

	// временнЫе параметры работы АЦП
	DspClockOut = 2.0*ModuleDescriptor.QuartzFrequency/1000.0; // тактовая частота DSP в кГц
	if(fabs(DspClockOut - 48000.0) > 0.00001) { LAST_ERROR_NUMBER(26); return FALSE; }
	AdcPars->AdcRate = fabs(AdcPars->AdcRate);
	if(AdcPars->AdcRate > 400.0) AdcPars->AdcRate = 400.0;
	else if(AdcPars->AdcRate < 0.1) AdcPars->AdcRate = 0.1;
	SclockDiv = (DWORD)(DspClockOut/(2.0*AdcPars->AdcRate) - 0.5);
	if(SclockDiv > 65534) SclockDiv = 65534;
	AdcPars->AdcRate = DspClockOut/(2.0*(SclockDiv + 1.0));
	if(!PUT_LBIOS_WORD(L_FIRST_SAMPLE_DELAY_E440, (SHORT)(DspClockOut/AdcPars->AdcRate + 5.5))) { LAST_ERROR_NUMBER(18); return FALSE; }
	if(!PUT_LBIOS_WORD(L_ADC_RATE_E440, (SHORT)SclockDiv)) { LAST_ERROR_NUMBER(18); return FALSE; }
	AdcPars->InterKadrDelay = fabs(AdcPars->InterKadrDelay);
	if((1.0/(AdcPars->AdcRate)) > (AdcPars->InterKadrDelay)) AdcPars->InterKadrDelay = 1.0/AdcPars->AdcRate;
	KadrDelay = (DWORD)(AdcPars->InterKadrDelay*AdcPars->AdcRate - 0.5);
	if(KadrDelay > 65534) KadrDelay = 65534;
	AdcPars->InterKadrDelay = (KadrDelay + 1.0)/AdcPars->AdcRate;
	if(!PUT_LBIOS_WORD(L_INTER_KADR_DELAY_E440, (SHORT)KadrDelay)) { LAST_ERROR_NUMBER(18); return FALSE; }
	AdcPars->KadrRate = 1.0/((AdcPars->ChannelsQuantity - 1.0)/AdcPars->AdcRate + AdcPars->InterKadrDelay);

	//===== параметры FIFO буфера АЦП =====
	// базовый адрес FIFO буфера АЦП всегда равен 0x0
	AdcPars->AdcFifoBaseAddress = 0x0;
	//
	AdcPars->AdcFifoLength = AdcPars->AdcFifoLength;
	// длина FIFO буфера АЦП должна находиться в диапазоне от 0x40(64)
	if(AdcPars->AdcFifoLength < 0x40) AdcPars->AdcFifoLength = 0x40;
	// и вплоть до 0x3000 (12288)
	else if(AdcPars->AdcFifoLength > 0x3000) AdcPars->AdcFifoLength = 0x3000;
	// а также длина FIFO буфера АЦП должна быть кратна 0x40(64)
	AdcPars->AdcFifoLength -= (WORD)(AdcPars->AdcFifoLength%64);
	if(!PUT_LBIOS_WORD(L_ADC_FIFO_LENGTH_E440, AdcPars->AdcFifoLength)) { LAST_ERROR_NUMBER(18); return FALSE; }

	// загрузим калибровочные коэффициенты для АЦП
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E440; i++)
	{
		// смещение АЦП для усиления i
		if(AdcPars->AdcOffsetCoefs[i] >= 0.0) AdcOffsetCoef = (WORD)(AdcPars->AdcOffsetCoefs[i] + 0.5);
		else AdcOffsetCoef = (WORD)(AdcPars->AdcOffsetCoefs[i] - 0.5);
		if(!PUT_LBIOS_WORD((WORD)(L_ZERO_E440 + i), AdcOffsetCoef)) { LAST_ERROR_NUMBER(18); return FALSE; }

		// масштаб АЦП для усиления i
		AdcScaleCoef = (WORD)(AdcPars->AdcScaleCoefs[i]*(double)0x8000 + 0.5);
		if(!PUT_LBIOS_WORD((WORD)(L_SCALE_E440 + i), AdcScaleCoef)) { LAST_ERROR_NUMBER(18); return FALSE; }
	}
	// сохраним параметры работы АЦП
	this->AdcPars = *AdcPars;
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// запуск работы АЦП
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::START_ADC(void)
{
	WORD InBuf[4] = { 0x0, V_START_ADC, 0x0 | DSP_DM, (WORD)(AdcPars.AdcFifoLength/2.0 + 0.5)};

	// шлём USB-запрос в MCU на работу АЦП
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	// посылаем команду в DSP модуля на запуск АЦП
	else if(!SEND_COMMAND(C_START_ADC)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------
// останов АЦП
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::STOP_ADC(void)
{
	// посылаем команду в DSP модуля на останов АЦП
	if(!SEND_COMMAND(C_STOP_ADC)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// для старых ревизий модуля делаем спец.процедуру зачистки потоковых USB-запросов на чтение
	if(('A' <= ModuleDescriptor.Revision) && (ModuleDescriptor.Revision <= 'C'))
		{ if(!StopAdcForOldRevision()) { LAST_ERROR_NUMBER(27); return FALSE; } }

	// делаем двойную зачистку Bulk-канала приёма данных по USB (на всякий случай)
	if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE3/*reset Read Pipe*/, NULL, 0x0, NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE3/*reset Read Pipe*/, NULL, 0x0, NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------
// ввод кадра отсчетов с АЦП модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::ADC_KADR(SHORT * const Data)
{
	WORD ChannelsQuantity;

	// проверим буфер данных
	if(!Data) { LAST_ERROR_NUMBER(2); return FALSE; }
	// проверяем название модуля
	else if(strcmp((char *)ModuleDescriptor.Name, "E440")) { LAST_ERROR_NUMBER(24); return FALSE; }
	// остановим АЦП
	else if(!STOP_ADC()) { LAST_ERROR_NUMBER(28); return FALSE; }
	// проверим параметры работы АЦП
	else if(!GET_LBIOS_WORD(L_CONTROL_TABLE_LENGHT_E440, (SHORT *)&ChannelsQuantity)) { LAST_ERROR_NUMBER(16); return FALSE; }
	else if(ChannelsQuantity != AdcPars.ChannelsQuantity)  { LAST_ERROR_NUMBER(29); return FALSE; }
	else if(ChannelsQuantity > AdcPars.AdcFifoLength)  { LAST_ERROR_NUMBER(30); return FALSE; }
	// команда ввода кадра отсчетов с АЦП
	else if(!SEND_COMMAND(C_ADC_KADR)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// теперь можно прочитать полученные данные
	else if(!GET_DM_ARRAY(0x0, AdcPars.ChannelsQuantity, Data)) { LAST_ERROR_NUMBER(31); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------
// однократный ввод с заданного логического канала АЦП модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::ADC_SAMPLE(SHORT * const AdcData, WORD AdcChannel)
{
	// проверим указатель
	if(!AdcData) { LAST_ERROR_NUMBER(2); return FALSE; }
	// теперь можно остановить АЦП
	else if(!STOP_ADC()) { LAST_ERROR_NUMBER(28); return FALSE; }
	// логический номер канала для однократного ввода с АЦП
	else if(!PUT_LBIOS_WORD(L_ADC_CHANNEL_E440, AdcChannel)) { LAST_ERROR_NUMBER(18); return FALSE; }
	// команда однократного ввода с АЦП
	else if(!SEND_COMMAND(C_ADC_SAMPLE)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// теперь прочитаем полученное значение с АЦП
	else if(!GET_LBIOS_WORD(L_ADC_SAMPLE_E440, AdcData)) { LAST_ERROR_NUMBER(16); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------
// потоковое чтение данных с АЦП модуля:
//   если ReadRequest->Overlapped != NULL - асинхронный запрос на сбор необходимого кол-ва данных
//   если ReadRequest->Overlapped == NULL - синхронный сбор необходимого кол-ва данных
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::ReadData(IO_REQUEST_LUSBAPI * const ReadRequest)
{
	DWORD NumberOfBytesRead;

	// проверим буфер данных
	if(!ReadRequest->Buffer) { LAST_ERROR_NUMBER(2); return FALSE; }
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
			{ if(GetLastError() != ERROR_IO_PENDING) { LAST_ERROR_NUMBER(59); CancelIo(hDevice); return FALSE; } }
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
		if(!Ov.hEvent) { LAST_ERROR_NUMBER(33); return FALSE; }

		// посылаем асинхронный запрос на сбор необходимого кол-ва данных
		NumberOfBytesRead = 0x0;
		if(!ReadFile(hDevice, ReadRequest->Buffer, 2*ReadRequest->NumberOfWordsToPass, &NumberOfBytesRead, &Ov))
			{ if(GetLastError() != ERROR_IO_PENDING) { LAST_ERROR_NUMBER(32); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; } }
		// ждём окончания выполнения запроса
		if(WaitForSingleObject(Ov.hEvent, ReadRequest->TimeOut) == WAIT_TIMEOUT) { LAST_ERROR_NUMBER(34); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; }
		// попробуем получить кол-во реально переданных байт данных
		else if(!GetOverlappedResult(hDevice, &Ov, &NumberOfBytesRead, TRUE)) { LAST_ERROR_NUMBER(35); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; }
		// закроем событие
		else if(!CloseHandle(Ov.hEvent)) { LAST_ERROR_NUMBER(36); return FALSE; }
		// кол-во полученных отсчётов
		ReadRequest->NumberOfWordsPassed = NumberOfBytesRead/2;
		// сравним сколько было реально полученно данных (в словах) с запрашиваемым кол-вом отсчётов
		if(ReadRequest->NumberOfWordsToPass != ReadRequest->NumberOfWordsPassed) { LAST_ERROR_NUMBER(37); return FALSE; }
	}
	// все хорошо :)))))
	return TRUE;
}




//==============================================================================
// функции для работы с ЦАП модуля
//==============================================================================
//------------------------------------------------------------------------------
// получение текущих параметров работы ЦАП
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::GET_DAC_PARS(DAC_PARS_E440 * const DacPars)
{
	WORD DacEnabled;
	WORD SclkDiv, RfsDiv;
	double DspClockout, SCLK;

	// проверим структуру параметров работы ЦАП
	if(!DacPars) { LAST_ERROR_NUMBER(2); return FALSE; }
	// проверяем название модуля
	if(strcmp((char *)ModuleDescriptor.Name, "E440")) { LAST_ERROR_NUMBER(24); return FALSE; }
	else if(!GET_LBIOS_WORD(L_DAC_ENABLED_E440, (SHORT *)&DacEnabled)) { LAST_ERROR_NUMBER(16); return FALSE; }
	DacPars->DacEnabled = DacEnabled ? TRUE : FALSE;

	// временные параметры работы ЦАП
	DspClockout = 2.0*ModuleDescriptor.QuartzFrequency/1000.0; // тактовая частота DSP в кГц
	if(fabs(DspClockout - 48000.0) > 0.00001) { LAST_ERROR_NUMBER(26); return FALSE; }

	if(!GET_LBIOS_WORD(L_DAC_SCLK_DIV_E440, (SHORT *)&SclkDiv)) { LAST_ERROR_NUMBER(16); return FALSE; }
	SCLK = DspClockout/(2.0*(SclkDiv + 1.0));

	if(!GET_LBIOS_WORD(L_DAC_RATE_E440, (SHORT *)&RfsDiv)) { LAST_ERROR_NUMBER(16); return FALSE; }
	DacPars->DacRate = SCLK/(RfsDiv + 1.0);

	// параметры FIFO буфера АЦП
	if(!GET_LBIOS_WORD(L_DAC_FIFO_BASE_ADDRESS_E440, (SHORT *)&DacPars->DacFifoBaseAddress)) { LAST_ERROR_NUMBER(16); return FALSE; }
	if(!GET_LBIOS_WORD(L_CUR_DAC_FIFO_LENGTH_E440, (SHORT *)&DacPars->DacFifoLength)) { LAST_ERROR_NUMBER(16); return FALSE; }

	// сохраним параметры работы ЦАП
	this->DacPars = *DacPars;
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// заполнение требуемых параметров работы ЦАП
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::SET_DAC_PARS(DAC_PARS_E440 * const DacPars)
{
	WORD SclkDiv, RfsDiv;
	double DspClockout, SCLK;

	// проверим структуру параметров работы ЦАП
	if(!DacPars) { LAST_ERROR_NUMBER(2); return FALSE; }
	// проверяем название модуля
	if(strcmp((char *)ModuleDescriptor.Name, "E440")) { LAST_ERROR_NUMBER(24); return FALSE; }

	// временнЫе параметры работы ЦАП
	DspClockout = 2.0*ModuleDescriptor.QuartzFrequency/1000.0; // тактовая частота DSP в кГц
	if(fabs(DspClockout - 48000.0) > 0.00001) { LAST_ERROR_NUMBER(26); return FALSE; }

	if(!GET_LBIOS_WORD(L_DAC_SCLK_DIV_E440, (SHORT *)&SclkDiv)) { LAST_ERROR_NUMBER(16); return FALSE; }
	SCLK = DspClockout/(2.0*(SclkDiv + 1.0));

	DacPars->DacRate = fabs(DacPars->DacRate);
	if(DacPars->DacRate > 125.0)	DacPars->DacRate = 125.0;
	if(DacPars->DacRate < SCLK/65500.0) DacPars->DacRate = SCLK/65500.0;

	RfsDiv = (SHORT)(SCLK/(DacPars->DacRate) - 0.5);
	DacPars->DacRate = SCLK/(RfsDiv + 1.0);
	if(!PUT_LBIOS_WORD(L_DAC_RATE_E440, RfsDiv)) { LAST_ERROR_NUMBER(18); return FALSE; }

	//===== параметры FIFO буфера ЦАП =====
	// базовый адрес FIFO буфера ЦАП всегда равен 0x3000
	DacPars->DacFifoBaseAddress = 0x3000;
	//
	DacPars->DacFifoLength = DacPars->DacFifoLength;
	// длина FIFO буфера ЦАП должна находиться в диапазоне от 0x40(64)
	if(DacPars->DacFifoLength < 0x40) DacPars->DacFifoLength = 0x40;
	// и вплоть до 0xFC0(4032)
	else if(DacPars->DacFifoLength > 0xFC0) DacPars->DacFifoLength = 0xFC0;
	// а также длина FIFO буфера ЦАП должна быть кратна 0x40(64)
	DacPars->DacFifoLength -= (WORD)(DacPars->DacFifoLength%64);
	if(!PUT_LBIOS_WORD(L_DAC_FIFO_LENGTH_E440, DacPars->DacFifoLength)) { LAST_ERROR_NUMBER(18); return FALSE; }

	// сохраним параметры работы ЦАП
	this->DacPars = *DacPars;
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// запуск работы ЦАП
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::START_DAC(void)
{
	// а есть ли ЦАП на данном экземпляре модуля
	if(ModuleDescriptor.IsDacPresented == DAC_INACCESSIBLED_E440)  { LAST_ERROR_NUMBER(38); return FALSE; }
	// для старых ревизий модуля дополнительная процедурка
	else if(('A' <= ModuleDescriptor.Revision) && (ModuleDescriptor.Revision <= 'C'))
	{
		WORD InBuf[4] = { 0x0, V_START_DAC, 0x3000 | DSP_DM, (WORD)(DacPars.DacFifoLength/2.0 + 0.5) };

		// делаем двойную зачистку Bulk-канала приёма данных по USB (на всякий случай)
		if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE1/*reset Write Pipe*/, NULL, 0x0, NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
		else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE1/*reset Write Pipe*/, NULL, 0x0, NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
		// шлём USB-запрос в MCU на работу с ЦАП
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	}
	// посылаем команду в DSP модуля на запуск ЦАП
	if(!SEND_COMMAND(C_START_DAC)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------
// останов работы ЦАП
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::STOP_DAC(void)
{
	WORD InBuf[4] = { 0x0, V_START_DAC, 0x3000 | DSP_DM, (WORD)(DacPars.DacFifoLength/2.0 + 0.5) };

	// а есть ли ЦАП на данном экземпляре модуля
	if(ModuleDescriptor.IsDacPresented == DAC_INACCESSIBLED_E440)  { LAST_ERROR_NUMBER(38); return FALSE; }
	// посылаем команду в DSP модуля на останов ЦАП
	else if(!SEND_COMMAND(C_STOP_DAC)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// для старых ревизий модуля делаем спец.процедуру зачистки запросов
	else if(('A' <= ModuleDescriptor.Revision) && (ModuleDescriptor.Revision <= 'C'))
		{ if(!StopDacForOldRevision()) { LAST_ERROR_NUMBER(39); return FALSE; } }
	else
	{
		// делаем двойную зачистку Bulk-канала приёма данных по USB (на всякий случай)
		if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE1/*reset Write Pipe*/, NULL, 0x0, NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
		else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE1/*reset Write Pipe*/, NULL, 0x0, NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
		// шлём USB-запрос в MCU на работу с ЦАП
		else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	}
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// потоковая передача данных ЦАП в модуль
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::WriteData(IO_REQUEST_LUSBAPI * const WriteRequest)
{
	DWORD NumberOfBytesWritten;

	// проверим буфер данных
	if(!WriteRequest->Buffer) { LAST_ERROR_NUMBER(2); return FALSE; }
	// число отсчетов должно находиться в диапазоне от 0x20(32)
	else if(WriteRequest->NumberOfWordsToPass < 0x20) WriteRequest->NumberOfWordsToPass = 0x20;
	// и вплоть до 0x100000(1024*1024)
	else if(WriteRequest->NumberOfWordsToPass > (1024*1024)) WriteRequest->NumberOfWordsToPass = 1024*1024;
	// также число отсчетов должно быть кратно 0x20(32)
	WriteRequest->NumberOfWordsToPass -= (WriteRequest->NumberOfWordsToPass)%32;

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

//------------------------------------------------------------------------------
// однократный вывод на заданный канал ЦАП
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::DAC_SAMPLE(SHORT * const DacData, WORD DacChannel)
{
	SHORT DacParam;

	// а есть ли ЦАП на данном экземпляре модуля
	if(ModuleDescriptor.IsDacPresented == DAC_INACCESSIBLED_E440)  { LAST_ERROR_NUMBER(38); return FALSE; }
	// проверим указатель
	else if(!DacData) { LAST_ERROR_NUMBER(2); return FALSE; }
	// проверим номер канала ЦАП
	else if(DacChannel >= DAC_CHANNELS_QUANTITY_E440) { LAST_ERROR_NUMBER(41); return FALSE; }

	// подготовим значение для вывода на ЦАП
	if((*DacData) < -2048) *DacData = -2048;
	else if((*DacData) > 2047) *DacData = 2047;
	// формируем соотвестсвующее слово для передачи в DSP
	DacParam = (SHORT)((*DacData) & 0xFFF);
	DacParam |= (SHORT)(DacChannel << 12);
	if(!PUT_LBIOS_WORD(L_DAC_SAMPLE_E440, DacParam)) { LAST_ERROR_NUMBER(18); return FALSE; }

	// команда однократного вывода на ЦАП
	if(!SEND_COMMAND(C_DAC_SAMPLE)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// всё хорошо
	else return TRUE;
}



//==============================================================================
// функции для работы с цифровыми линиями на внешнем цифровом разъёме
//==============================================================================
//------------------------------------------------------------------------------
// функция разрешения выходных линий внешнего цифрового разъёма
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::ENABLE_TTL_OUT(BOOL EnableTtlOut)
{
	// передадим признак разрешения/запрещения цифровых линий
	if(!PUT_LBIOS_WORD(L_ENABLE_TTL_OUT_E440, (SHORT)(EnableTtlOut ? 0x1 : 0x0))) { LAST_ERROR_NUMBER(18); return FALSE; }
	// пошлем команду в DSP
	else if(!SEND_COMMAND(C_ENABLE_TTL_OUT)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// зафиксируем флажок статуса выходных цифровых линий модуля
	this->EnableTtlOut = EnableTtlOut;
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// функция чтения входных линии внешнего цифрового разъёма
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::TTL_IN(WORD * const TtlIn)
{
	// проверим указатель
	if(!TtlIn) { LAST_ERROR_NUMBER(2); return FALSE; }
	// пошлем команду в DSP
	else if(!SEND_COMMAND(C_TTL_IN)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// получим значения с цифровых входных линиий
	else if(!GET_LBIOS_WORD(L_TTL_IN_E440, (SHORT *)TtlIn)) { LAST_ERROR_NUMBER(16); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------
// функция вывода на выходные линии внешнего цифрового разъёма
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::TTL_OUT(WORD TtlOut)
{
	// проверим флажок статуса выходных цифровых линий модуля
	if(!EnableTtlOut) { LAST_ERROR_NUMBER(42); return FALSE; }
	// передадим значения, которые должны быть выставлены на цифровых линиях
	else if(!PUT_LBIOS_WORD(L_TTL_OUT_E440, (SHORT)TtlOut)) { LAST_ERROR_NUMBER(18); return FALSE; }
	// пошлем команду в DSP
	else if(!SEND_COMMAND(C_TTL_OUT)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// всё хорошо
	else return TRUE;
}




//==============================================================================
// функции для работы с пользовательским ППЗУ модуля
//==============================================================================

//------------------------------------------------------------------------------
// разрешение/запрещение режима записи в ППЗУ модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::ENABLE_FLASH_WRITE(BOOL EnableFlashWrite)
{
	// запомним этот флажок
	this->EnableFlashWrite = EnableFlashWrite;
	// передадим признак разрешения/запрещения записи в ППЗУ
	if(!PUT_LBIOS_WORD(L_FLASH_ENABLED_E440, (SHORT)((EnableFlashWrite) ? 0x9800 : 0x8000))) { LAST_ERROR_NUMBER(18); return FALSE; }
	// выполним команду
	else if(!SEND_COMMAND(C_ENABLE_FLASH_WRITE)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------
// чтенние слова из ППЗУ
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::READ_FLASH_WORD(WORD FlashAddress, SHORT * const FlashWord)
{
	// проверим указатель
	if(!FlashWord) { LAST_ERROR_NUMBER(2); return FALSE; }
	// проверим адрес ячейки ППЗУ (должен быть не больше 63)
	else if(FlashAddress > 63) { LAST_ERROR_NUMBER(43); return FALSE; }
	// передадим адрес ячейки ППЗУ
	else if(!PUT_LBIOS_WORD(L_FLASH_ADDRESS_E440, (SHORT)((FlashAddress << 7) | 0xC000))) { LAST_ERROR_NUMBER(18); return FALSE; }
	// выполним команду
	else if(!SEND_COMMAND(C_READ_FLASH_WORD)) { LAST_ERROR_NUMBER(20);LAST_ERROR_NUMBER(20); return FALSE; }	// считаем полученное из ППЗУ значение
	else if(!GET_LBIOS_WORD(L_FLASH_DATA_E440, FlashWord)) { LAST_ERROR_NUMBER(16); return FALSE; }

	Sleep(1);

	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// запись слова в ППЗУ
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::WRITE_FLASH_WORD(WORD FlashAddress, SHORT FlashWord)
{
	// проверим можно ли писать в ППЗУ
	if(!EnableFlashWrite) { LAST_ERROR_NUMBER(44); return FALSE; }
	else
	{
		if(!IsServiceFlashWriting)
		{
			// проверим адрес ячейки ППЗУ (должен быть не менее 32)
			if(FlashAddress < 32) { LAST_ERROR_NUMBER(45); return FALSE; }
			// проверим адрес ячейки ППЗУ (должен быть не больше 63)
			else if(FlashAddress > 63) { LAST_ERROR_NUMBER(43); return FALSE; }
		}
	}

	// передадим адрес ячейки ППЗУ
	if(!PUT_LBIOS_WORD(L_FLASH_ADDRESS_E440, (SHORT)((FlashAddress << 7) | 0xA000))) { LAST_ERROR_NUMBER(18); return FALSE; }
	// передадим данные
	else if(!PUT_LBIOS_WORD(L_FLASH_DATA_E440, FlashWord)) { LAST_ERROR_NUMBER(18); return FALSE; }
	// выполним команду
	else if(!SEND_COMMAND(C_WRITE_FLASH_WORD)) { LAST_ERROR_NUMBER(20); return FALSE; }

	Sleep(10);

	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// получим служебную информацию о модуле из ППЗУ
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E440 * const md)
{
	BOOL Status;
	WORD i;
	WORD last_err;
	char McuName[NAME_LINE_LENGTH_LUSBAPI];

	// проверим входной параметр
	if(!md) { LAST_ERROR_NUMBER(2); return FALSE; }

	// полностью сбросим входную структуру
	ZeroMemory(md, sizeof(MODULE_DESCRIPTION_E440));
	// установим статус выполнения функции
	Status = TRUE;

	// -=== информация о модуле ===-
	// тепереь можно прочитать системную чать ППЗУ модуля
	if(GetModuleDescriptor())
	{
		// Module Info
		strncpy((char *)md->Module.CompanyName,			"L-Card Ltd.",									sizeof(md->Module.CompanyName));
		if(!strcmp((char *)ModuleDescriptor.Name, "E440")) strcpy((char *)md->Module.DeviceName, "E14-440");
		else strncpy((char *)md->Module.DeviceName,		(char *)ModuleDescriptor.Name,			std::min(sizeof(md->Module.DeviceName), sizeof(ModuleDescriptor.Name)));
		strncpy((char *)md->Module.SerialNumber,			(char *)ModuleDescriptor.SerialNumber,	std::min(sizeof(md->Module.SerialNumber), sizeof(ModuleDescriptor.SerialNumber)));
		strcpy((char *)md->Module.Comment,					"Внешний модуль АЦП/ЦАП/ТТЛ общего назначения на шину USB 1.1 (Full Speed 12Mbit/s)");
		// получим ревизию модуля
		md->Module.Revision = ModuleDescriptor.Revision;
		// модуль E14-440 существует в единственном исполнении (по крайне мере пока)
		md->Module.Modification = NO_MODULE_MODIFICATION_LUSBAPI;

		// Interface Info
		md->Interface.Active = TRUE;
		strcpy((char *)md->Interface.Name, "USB 1.1");
		strcpy((char *)md->Interface.Comment, "Full-Speed Mode (12 Mbit/s)");

		// ADC Info
		md->Adc.Active = TRUE;
		strcpy((char *)md->Adc.Name, "LTC1416");
		// корректировочные коэффициенты АЦП
		for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E440; i++)
		{
			// смещение при усилении i
			md->Adc.OffsetCalibration[i] = (double)ModuleDescriptor.AdcOffsetCoefs[i];
			// масштаб при усилении i
			md->Adc.ScaleCalibration[i] = (double)ModuleDescriptor.AdcScaleCoefs[i]/(double)0x8000;
		}
		strcpy((char *)md->Adc.Comment, "14-Bit 400Ksps ADC converter");

		// DAC Info
		if(ModuleDescriptor.IsDacPresented)
		{
			md->Dac.Active = TRUE;
			strcpy((char *)md->Dac.Name, "AD7249");
			// корректировочные коэффициенты ЦАП
			for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E440; i++)
			{
				md->Dac.OffsetCalibration[i] = ModuleDescriptor.DacOffsetCoefs[i]/10000.0;
				md->Dac.ScaleCalibration[i] = ModuleDescriptor.DacScaleCoefs[i]/10000.0;
			}
			strcpy((char *)md->Dac.Comment, "12-Bit 125Ksps DAC converter");
		}
		else md->Dac.Active = FALSE;

		// IO Info
		md->DigitalIo.Active = TRUE;
		if((ModuleDescriptor.Revision >= 'A') && (ModuleDescriptor.Revision < 'E'))  strcpy((char *)md->DigitalIo.Name, "74LVX573/74HTC574");
		else strcpy((char *)md->DigitalIo.Name, "74LVX574MTC/74LCX574MTC");
		md->DigitalIo.InLinesQuantity = 16;
		md->DigitalIo.OutLinesQuantity = 16;
		strcpy((char *)md->DigitalIo.Comment, "TTL/CMOS compatible");
	}
	else
	{
		ZeroMemory(&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR));
		LAST_ERROR_NUMBER(46);
		last_err = LastErrorNumber;
		Status = FALSE;
	}
	// -===========================-

	// лёгкая проверка считанного ModuleDescriptor на правильность названия модуля
	if(strcmp((char *)ModuleDescriptor.Name, "E440"))
	{
		if(Status)
		{
			LAST_ERROR_NUMBER(24);
			last_err = LastErrorNumber;
			Status = FALSE;
		}
		else
		{
			LastErrorNumber = last_err;
		}
	}

	// -=== информация о LBIOS ===-
	// прочитаем информацию о LBIOS
	if(GetDspDescriptor())
	{
		// DSP Info
		md->Dsp.Active = TRUE;
		strncpy((char *)md->Dsp.Name,							(char *)DspDescriptor.DspName,		std::min(sizeof(md->Dsp.Name), sizeof(DspDescriptor.DspName)));
		strncpy((char *)md->Dsp.Version.Version,			(char *)DspDescriptor.Version,		std::min(sizeof(md->Dsp.Version.Version), sizeof(DspDescriptor.Version)));
		strncpy((char *)md->Dsp.Version.Date,				(char *)DspDescriptor.Created,		std::min(sizeof(md->Dsp.Version.Date), sizeof(DspDescriptor.Created)));
		strncpy((char *)md->Dsp.Version.Manufacturer,	(char *)DspDescriptor.Manufacturer, std::min(sizeof(md->Dsp.Version.Manufacturer), sizeof(DspDescriptor.Manufacturer)));
		strncpy((char *)md->Dsp.Version.Author,			(char *)DspDescriptor.Author,			std::min(sizeof(md->Dsp.Version.Author), sizeof(DspDescriptor.Author)));
		strncpy((char *)md->Dsp.Version.Comment,			(char *)DspDescriptor.Comment,		std::min(sizeof(md->Dsp.Version.Comment), sizeof(DspDescriptor.Comment)));
		strncpy((char *)md->Dsp.Comment,"The ADSP-2185 is a single-chip microcomputer optimized for digital signal processing (DSP) with 16K words (24-bit) of program RAM and 16K words (16-bit) of data RAM.", sizeof(md->Dsp.Comment));
		if(Status)
			md->Dsp.ClockRate = 2.0*ModuleDescriptor.QuartzFrequency/1000.0;		// в кГц
		else
			md->Dsp.ClockRate = 0.0;															// в кГц
	}
	else
	{
		if(Status)
		{
			LAST_ERROR_NUMBER(47);
			last_err = LastErrorNumber;
			Status = FALSE;
		}
		else
		{
			LastErrorNumber = last_err;
		}
	}
	// -==========================-

	// -=== информация о FIRMWARE ===-
	// прочитаем информацию о драйвере микроконтроллера
	if(GetMcuDescriptor())
	{
		// инициализируем OVERLAPPED структуру
		ZeroMemory(McuName, sizeof(McuName));
		// запомним название микроконтроллера
		strncpy(McuName, (char *)FirmwareDescriptor.McuName, sizeof(McuName));
		McuName[NAME_LINE_LENGTH_LUSBAPI - 0x1] = 0x0;
		// преобразуем в верхний регистр
		char *ptr;
		ptr = McuName;
		while(*ptr) { *ptr = toupper(*ptr); ptr++; }

		// MCU Info
		md->Mcu.Active = TRUE;
		strncpy((char *)md->Mcu.Name, 						(char *)FirmwareDescriptor.McuName, 		std::min(sizeof(md->Mcu.Name), sizeof(FirmwareDescriptor.McuName)));
		strncpy((char *)md->Mcu.Version.Version,  		(char *)FirmwareDescriptor.Version, 		std::min(sizeof(md->Mcu.Version.Version), sizeof(FirmwareDescriptor.Version)));
		strncpy((char *)md->Mcu.Version.Date,  			(char *)FirmwareDescriptor.Created, 		std::min(sizeof(md->Mcu.Version.Date), sizeof(FirmwareDescriptor.Created)));
		strncpy((char *)md->Mcu.Version.Manufacturer,  	(char *)FirmwareDescriptor.Manufacturer,	std::min(sizeof(md->Mcu.Version.Manufacturer), sizeof(FirmwareDescriptor.Manufacturer)));
		strncpy((char *)md->Mcu.Version.Author,  			(char *)FirmwareDescriptor.Author, 			std::min(sizeof(md->Mcu.Version.Author), sizeof(FirmwareDescriptor.Author)));
		strncpy((char *)md->Mcu.Version.Comment,			(char *)FirmwareDescriptor.Comment, 		std::min(sizeof(md->Mcu.Version.Comment), sizeof(FirmwareDescriptor.Comment)));
		if(strstr(McuName,		"AT90S8515"))
			strncpy((char *)md->Mcu.Comment,					"AVR AT90S8515 is 8-bit Microcontroller with 8K Bytes In-System Programmable Flash", sizeof(md->Mcu.Comment));
		else if(strstr(McuName,	"ATMEGA8515"))
			strncpy((char *)md->Mcu.Comment,					"AVR ATmega8515(L) is 8-bit Microcontroller with 8K Bytes In-System Programmable Flash", sizeof(md->Mcu.Comment));
		else if(strstr(McuName,	"ATMEGA162"))
			strncpy((char *)md->Mcu.Comment,					"AVR ATmega162 is 8-bit Microcontroller with 16K Bytes In-System Programmable Flash", sizeof(md->Mcu.Comment));
		else if(strstr(McuName,	"LPC1768"))
			strncpy((char *)md->Mcu.Comment,					"NXP LPC1768 ARM is Cortex-M3 32-bit Microcontroller with 512K Bytes In-System Programmable Flash", sizeof(md->Mcu.Comment));
		else if(strstr(McuName,	"UNKNOWN"))
			strncpy((char *)md->Mcu.Comment,					"AVR 8-bit Microcontroller with 8K Bytes In-System Programmable Flash", sizeof(md->Mcu.Comment));
		else
			strcpy((char *)md->Mcu.Comment, 					"??????");
		if(md->Dsp.ClockRate)
			md->Mcu.ClockRate = ModuleDescriptor.QuartzFrequency/3000.0;		// в кГц
		else
			md->Mcu.ClockRate = 0.0;														// в кГц
	}
	else
	{
		if(Status)
		{
			LAST_ERROR_NUMBER(48);
			Status = FALSE;
		}
		else
		{
			LastErrorNumber = last_err;
		}
	}
	// -=============================-

	// для модулей ревизии 'D' и выше вносим изменения в штатную работу MCU,
	// установив аттрибут возможности реагирования на сброс Bulk каналов USB
	McuAttributes |= ENABLE_RESET_BULK_PIPES;
	if(!SetMcuAttributes(McuAttributes)) { LAST_ERROR_NUMBER(49); return FALSE; }
	// возвращаем статус выполнения функции
	else return Status;
}

//------------------------------------------------------------------------------
// запишем служебную информацию о модуле в ППЗУ
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E440 * const md)
{
	WORD i;

	// проверим структуру
	if(!md) { LAST_ERROR_NUMBER(2); return FALSE; }

	// теперь можно заполнить дескриптор модуля
	strcpy((char *)ModuleDescriptor.Name, 				"E440");
//	strncpy((char *)ModuleDescriptor.Name, 			(char *)md->Module.DeviceName,	std::min(sizeof(md->Module.DeviceName), sizeof(ModuleDescriptor.Name)));
	strncpy((char *)ModuleDescriptor.SerialNumber,	(char *)md->Module.SerialNumber, std::min(sizeof(md->Module.SerialNumber), sizeof(ModuleDescriptor.SerialNumber)));
	// проверим требуемую ревизию модуля
	if((	md->Module.Revision < REVISIONS_E440[REVISION_A_E440] ||
			md->Module.Revision > REVISIONS_E440[REVISIONS_QUANTITY_E440 - 0x1])) { LAST_ERROR_NUMBER(63); return FALSE; }
	ModuleDescriptor.Revision = md->Module.Revision;

	if(!strcmp((char *)md->Dsp.Name, "ADSP-2185M")) strcpy((char *)ModuleDescriptor.DspType, "2185");
	else { LAST_ERROR_NUMBER(50); return FALSE; }
	ModuleDescriptor.IsDacPresented = (BYTE)(md->Dac.Active ? 0x1 : 0x0);
	ModuleDescriptor.QuartzFrequency = (ULONG)(1000.0 * md->Dsp.ClockRate / 2.0 + 0.5);
	// корректировочные коэффициенты АЦП
	for(i = 0x0; i < 0x4; i++)
	{
		// смещение АЦП при усилении i
		if(md->Adc.OffsetCalibration[i + 0x0] >= 0.0) ModuleDescriptor.AdcOffsetCoefs[i] = (SHORT)(md->Adc.OffsetCalibration[i] + 0.5);
		else ModuleDescriptor.AdcOffsetCoefs[i] = (SHORT)(md->Adc.OffsetCalibration[i] - 0.5);
		// масштаб АЦП при усилении i
		ModuleDescriptor.AdcScaleCoefs[i] = (WORD)(md->Adc.ScaleCalibration[i]*(double)0x8000 + 0.5);
	}
	// корректировочные коэффициенты ЦАП
	for(i = 0x0; i < 0x2; i++)
	{
		// проверим - есть ли ЦАП на борту
		if(md->Dac.Active)
		{
			// смещение ЦАП канала i
			if(md->Dac.OffsetCalibration[i] >= 0.0) ModuleDescriptor.DacOffsetCoefs[i] = (SHORT)(10000.0*md->Dac.OffsetCalibration[i] + 0.5);
			else ModuleDescriptor.DacOffsetCoefs[i] = (SHORT)(10000.0*md->Dac.OffsetCalibration[i] - 0.5);
			// масштаб ЦАП канала i
			ModuleDescriptor.DacScaleCoefs[i] = (WORD)(10000.0*md->Dac.ScaleCalibration[i] + 0.5);
		}
		else
		{
			// смещение ЦАП канала i
			ModuleDescriptor.DacOffsetCoefs[i] = 0;
			// масштаб ЦАП канала i
			ModuleDescriptor.DacScaleCoefs[i] = 1;
		}
	}
	for(i = 0x0; i < sizeof(ModuleDescriptor.ReservedWord); i++) ModuleDescriptor.ReservedWord[i] = 0x0;

	// попробуем сохранить дескриптор модуля
	if(!PutModuleDescriptor()) { LAST_ERROR_NUMBER(51); return FALSE; }
	// всё хорошо
	else return TRUE;
}




//==============================================================================
// функции для работы памятью DSP модуля
//==============================================================================
//------------------------------------------------------------------------------
// запись слова в область памяти программ DSP с переменными штатного LBIOS
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::PUT_LBIOS_WORD(WORD Address, SHORT Data)
{
	// проверим требуемый аддрес
	if((Address < DataBaseAddress) || (Address > ProgramBaseAddress)) { LAST_ERROR_NUMBER(52); return FALSE; }
	// пишем переменную в DSP
	else if(!PUT_PM_WORD(Address, (long)Data << 0x8)) { LAST_ERROR_NUMBER(53); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------
// чтение слова из область памяти программ DSP с переменными штатного LBIOS
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::GET_LBIOS_WORD(WORD Address, SHORT * const Data)
{
	long PmData;

	// для начала проверим указатель
	if(!Data) { LAST_ERROR_NUMBER(2); return FALSE; }
	// теперь проверим требуемый аддрес в ППЗУ
	else if((Address < DataBaseAddress) || (Address > ProgramBaseAddress)) { LAST_ERROR_NUMBER(52); return FALSE; }
	// читаем переменную из DSP
	else if(!GET_PM_WORD(Address, &PmData)) { LAST_ERROR_NUMBER(15); return FALSE; }
	// формируем значение
	*Data = (SHORT)(PmData >> 0x8);
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// запись слова в ячейку памяти данных DSP
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::PUT_DM_WORD(WORD Address, SHORT Data)
{
	WORD InBuf[4] = { 0x0, V_PUT_ARRAY, (WORD)(Address | DSP_DM), 0x0 };

	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND,
													&InBuf, sizeof(InBuf),
													&Data, 2/*bytes*/,
													TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// чтение слова из ячейки памяти данных DSP
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::GET_DM_WORD(WORD Address, SHORT * const Data)
{
	WORD InBuf[4] = { 0x1, V_GET_ARRAY, (WORD)(Address | DSP_DM), 0x0 };

	// для начала проверим указатель
	if(!Data) { LAST_ERROR_NUMBER(2); return FALSE; }
	// посылаем ползовательский USB-запрос в MCU
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND,
													&InBuf,sizeof(InBuf),
													Data, 2/*bytes*/,
													TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// запись слова в ячейку памяти программ DSP
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::PUT_PM_WORD(WORD Address, long Data)
{
	WORD InBuf[4] = { 0x0, V_PUT_ARRAY, (WORD)(Address | DSP_PM), 0x0 };

	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND,
													&InBuf, sizeof(InBuf),
													&Data, 4/*bytes*/,
													TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// чтение слова из ячейки памяти программ DSP
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::GET_PM_WORD(WORD Address, long * const Data)
{
	WORD InBuf[4] = { 0x1, V_GET_ARRAY, (WORD)(Address | DSP_PM), 0x0 };

	// для начала проверим указатель
	if(!Data) { LAST_ERROR_NUMBER(2); return FALSE; }
	// посылаем ползовательский USB-запрос в MCU
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND,
													&InBuf,sizeof(InBuf),
													Data, 4/*bytes*/,
													TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// запись массива слов в область памяти данных DSP
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::PUT_DM_ARRAY(WORD BaseAddress, WORD NPoints, SHORT * const Data)
{
	BOOL Error = FALSE;
	WORD i;
	WORD InBuf[4];
	WORD MaxByteBlock = MAX_USB_CONTROL_PIPE_BLOCK, BlockQuantity, RestDataPoints;

	// для начала проверим буфер
	if(!Data) { LAST_ERROR_NUMBER(2); return FALSE; }

	BlockQuantity = (WORD)((sizeof(SHORT)*NPoints)/MaxByteBlock);
	RestDataPoints = (WORD)((sizeof(SHORT)*NPoints)%MaxByteBlock); // в байтах

	InBuf[0] = 0x0; InBuf[1] = V_PUT_ARRAY; InBuf[3] = 0x0;
	for(i = 0x0; i < BlockQuantity; i++)
	{
		InBuf[2] = (WORD)((BaseAddress + i*MaxByteBlock/sizeof(SHORT)) | DSP_DM);
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + i*MaxByteBlock/sizeof(SHORT), MaxByteBlock/*bytes*/, TimeOut)) { LAST_ERROR_NUMBER(3); Error = TRUE; break; }
	}

	if(!Error && RestDataPoints)
	{
		InBuf[2] = (WORD)((BaseAddress + BlockQuantity*MaxByteBlock/sizeof(SHORT)) | DSP_DM);
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + BlockQuantity*MaxByteBlock/sizeof(SHORT), RestDataPoints/*bytes*/, TimeOut)) { LAST_ERROR_NUMBER(3); Error = TRUE; }
	}

	return !Error;
}

//------------------------------------------------------------------------------
// чтение массива слов из области памяти данных DSP
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::GET_DM_ARRAY(WORD BaseAddress, WORD NPoints, SHORT * const Data)
{
	BOOL Error = FALSE;
	WORD i;
	WORD InBuf[4];
	WORD MaxByteBlock = MAX_USB_CONTROL_PIPE_BLOCK, BlockQuantity, RestDataPoints;

	// для начала проверим буфер
	if(!Data) { LAST_ERROR_NUMBER(2); return FALSE; }

	BlockQuantity = (WORD)((sizeof(SHORT)*NPoints)/MaxByteBlock);
	RestDataPoints = (WORD)((sizeof(SHORT)*NPoints)%MaxByteBlock); // в байтах

	InBuf[0] = 1; InBuf[1] = V_GET_ARRAY; InBuf[3] = 0x0;
	for(i = 0x0; i < BlockQuantity; i++)
	{
		InBuf[2] = (WORD)((BaseAddress + i*MaxByteBlock/sizeof(SHORT)) | DSP_DM);
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf,sizeof(InBuf), Data + i*MaxByteBlock/sizeof(SHORT), MaxByteBlock/*bytes*/, TimeOut)) { LAST_ERROR_NUMBER(3); Error = TRUE; break; }
	}

	if(!Error && RestDataPoints)
	{
		InBuf[2] = (WORD)((BaseAddress + BlockQuantity*MaxByteBlock/sizeof(SHORT)) | DSP_DM);
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + BlockQuantity*MaxByteBlock/sizeof(SHORT), RestDataPoints/*bytes*/, TimeOut)) { LAST_ERROR_NUMBER(3); Error = TRUE; }
	}

	return !Error;
}

//------------------------------------------------------------------------------
// запись массива слов в область память данных DSP
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::PUT_PM_ARRAY(WORD BaseAddress, WORD NPoints, long * const Data)
{
	BOOL Error = FALSE;
	WORD i;
	WORD InBuf[4];
	WORD MaxByteBlock = MAX_USB_CONTROL_PIPE_BLOCK, BlockQuantity, RestDataPoints;

	// для начала проверим буфер
	if(!Data) { LAST_ERROR_NUMBER(2); return FALSE; }

	BlockQuantity = (WORD)((sizeof(long)*NPoints)/MaxByteBlock);
	RestDataPoints = (WORD)((sizeof(long)*NPoints)%MaxByteBlock); // в байтах

	InBuf[0] = 0; InBuf[1] = V_PUT_ARRAY; InBuf[3] = 0x0;
	for(i = 0x0; i < BlockQuantity; i++)
	{
		InBuf[2] = (WORD)((BaseAddress + i*MaxByteBlock/sizeof(long)) | DSP_PM);
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + i*MaxByteBlock/sizeof(long), MaxByteBlock/*bytes*/, TimeOut)) { LAST_ERROR_NUMBER(3); Error = TRUE; break; }
	}

	if(!Error && RestDataPoints)
	{
		InBuf[2] = (WORD)((BaseAddress + BlockQuantity*MaxByteBlock/sizeof(long)) | DSP_PM);
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + BlockQuantity*MaxByteBlock/sizeof(long), RestDataPoints/*bytes*/, TimeOut)) { LAST_ERROR_NUMBER(3); Error = TRUE; }
	}

	return !Error;
}

//------------------------------------------------------------------------------
// чтение массива слов из области памяти данных DSP
//------------------------------------------------------------------------------
BOOL WINAPI TLE440::GET_PM_ARRAY(WORD BaseAddress, WORD NPoints, long * const Data)
{
	BOOL Error = FALSE;
	WORD i;
	WORD InBuf[4];
	WORD MaxByteBlock = MAX_USB_CONTROL_PIPE_BLOCK, BlockQuantity, RestDataPoints;

	// для начала проверим буфер
	if(!Data) { LAST_ERROR_NUMBER(2); return FALSE; }

	BlockQuantity = (WORD)((sizeof(long)*NPoints)/MaxByteBlock);
	RestDataPoints = (WORD)((sizeof(long)*NPoints)%MaxByteBlock); // в байтах

	InBuf[0] = 1; InBuf[1] = V_GET_ARRAY; InBuf[3] = 0x0;
	for(i = 0x0; i < BlockQuantity; i++)
	{
		InBuf[2] = (WORD)((BaseAddress + i*MaxByteBlock/sizeof(long)) | DSP_PM);
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + i*MaxByteBlock/sizeof(long), MaxByteBlock/*bytes*/, TimeOut))  { LAST_ERROR_NUMBER(3); Error = TRUE; break; }
	}

	if(!Error && RestDataPoints)
	{
		InBuf[2] = (WORD)((BaseAddress + BlockQuantity*MaxByteBlock/sizeof(long)) | DSP_PM);
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + BlockQuantity*MaxByteBlock/sizeof(long), RestDataPoints/*bytes*/, TimeOut)) { LAST_ERROR_NUMBER(3); Error = TRUE; }
	}

	return !Error;
}



//************************************************************************
// функции для работы с загрузочной FLASH
//************************************************************************
//------------------------------------------------------------------------
// зачистка загрузочного FLASH
//------------------------------------------------------------------------
BOOL WINAPI TLE440::ERASE_BOOT_FLASH(void)
{
	BOOL Status;
	WORD InBuf[4];

	// посылаем запрос в модуль
	InBuf[0] = 0; InBuf[1] = V_ERASE_BOOT_FLASH; InBuf[2] = 0x0; InBuf[3] = 0x0;
	Status = LDeviceIoControl( DIOC_SEND_COMMAND,
										&InBuf, sizeof(InBuf),
										NULL, 0/*bytes*/, 15000);
	// всё хорошо :)))))
	if(Status) return TRUE;
	// ошибка выполнения LDeviceIoControl()
	else { LastErrorNumber = 2; return FALSE; }
}

//------------------------------------------------------------------------
// запись массива байт в загрузочный FLASH
//------------------------------------------------------------------------
BOOL WINAPI TLE440::PUT_ARRAY_BOOT_FLASH(DWORD BaseAddress, DWORD NBytes, BYTE * const Data)
{
	BOOL Error = FALSE;
	WORD i, InBuf[4];
	WORD BlockQuantity, RestDataPoints;

	BlockQuantity = (WORD)(NBytes/MAX_USB_CONTROL_PIPE_BLOCK);
	RestDataPoints = (WORD)(NBytes%MAX_USB_CONTROL_PIPE_BLOCK); // в байтах

	InBuf[0] = 0; InBuf[1] = V_PUT_ARRAY_BOOT_FLASH;
	for(i = 0x0; i < BlockQuantity; i++)
	{
		InBuf[2] = (WORD)((BaseAddress + i*MAX_USB_CONTROL_PIPE_BLOCK) & 0x0000FFFFL);
		InBuf[3] = (WORD)(((BaseAddress + i*MAX_USB_CONTROL_PIPE_BLOCK) & 0xFFFF0000L) >> 0x10);
		if(!LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + i*MAX_USB_CONTROL_PIPE_BLOCK, MAX_USB_CONTROL_PIPE_BLOCK/*bytes*/, TimeOut)) { Error = TRUE; break; }
	}

	if(!Error && RestDataPoints)
	{
		InBuf[2] = (WORD)((BaseAddress + BlockQuantity*MAX_USB_CONTROL_PIPE_BLOCK) & 0x0000FFFFL);
		InBuf[3] = (WORD)(((BaseAddress + BlockQuantity*MAX_USB_CONTROL_PIPE_BLOCK) & 0xFFFF0000L) >> 0x10);
		if(!LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + BlockQuantity*MAX_USB_CONTROL_PIPE_BLOCK, RestDataPoints/*bytes*/, TimeOut)) Error = TRUE;
	}

	return !Error;
}

//------------------------------------------------------------------------
// чтение массива байт в загрузочный FLASH
//------------------------------------------------------------------------
BOOL WINAPI TLE440::GET_ARRAY_BOOT_FLASH(DWORD BaseAddress, DWORD NBytes, BYTE * const Data)
{
	BOOL Error = FALSE;
	WORD i, InBuf[4];
	WORD BlockQuantity, RestDataPoints;

	BlockQuantity = (WORD)(NBytes/MAX_USB_CONTROL_PIPE_BLOCK);
	RestDataPoints = (WORD)(NBytes%MAX_USB_CONTROL_PIPE_BLOCK); // в байтах

	InBuf[0] = 1; InBuf[1] = V_GET_ARRAY_BOOT_FLASH;
	for(i = 0x0; i < BlockQuantity; i++)
	{
		InBuf[2] = (WORD)((BaseAddress + i*MAX_USB_CONTROL_PIPE_BLOCK) & 0x0000FFFFL);
		InBuf[3] = (WORD)(((BaseAddress + i*MAX_USB_CONTROL_PIPE_BLOCK) & 0xFFFF0000L) >> 0x10);
		if(!LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + i*MAX_USB_CONTROL_PIPE_BLOCK, MAX_USB_CONTROL_PIPE_BLOCK/*bytes*/, TimeOut))  { Error = TRUE; break; }
	}

	if(!Error && RestDataPoints)
	{
		InBuf[2] = (WORD)((BaseAddress + BlockQuantity*MAX_USB_CONTROL_PIPE_BLOCK) & 0x0000FFFFL);
		InBuf[3] = (WORD)(((BaseAddress + BlockQuantity*MAX_USB_CONTROL_PIPE_BLOCK) & 0xFFFF0000L) >> 0x10);
		if(!LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + BlockQuantity*MAX_USB_CONTROL_PIPE_BLOCK, RestDataPoints/*bytes*/, TimeOut)) Error = TRUE;
	}

	return !Error;
}



//==============================================================================
// внутренние локальные функции для работы с модулем
//==============================================================================
//------------------------------------------------------------------------------
// инициализация всех необходимых переменных
//------------------------------------------------------------------------------
BOOL TLE440::InitVars(void)
{
	// вообще запись в ППЗУ модуля запрещена
	EnableFlashWrite = FALSE;
	// запись в служебную область ППЗУ модуля запрещена
	IsServiceFlashWriting = FALSE;
	// адрес начала сегмента кода с LBIOS в памяти программ
	ProgramBaseAddress = 0x1000;
	// адрес начала сегмента блока данных в памяти программ
	DataBaseAddress = DataBaseAddress_E440;
	// сбросим флажок статуса выходных цифровых линий модуля
	EnableTtlOut = FALSE;
	// сбросим все дополнительные аттрибуты работы MCU
	McuAttributes = NO_MCU_ATTRIBUTES;

	// инициализация всех локальных структур класса
	ZeroMemory(&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR));
	ZeroMemory(&DspDescriptor, sizeof(DSP_DESCRIPTOR));
	ZeroMemory(&OldFirmwareDescriptor, sizeof(OLD_FIRMWARE_DESCRIPTOR));
	ZeroMemory(&FirmwareDescriptor, sizeof(FIRMWARE_DESCRIPTOR));
	ZeroMemory(&AdcPars, sizeof(ADC_PARS_E440));
	ZeroMemory(&DacPars, sizeof(DAC_PARS_E440));

	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// пробуем загрузить DSP модуля из загрузочного ППЗУ
//------------------------------------------------------------------------------
BOOL TLE440::LoadModuleFromBootFlash(void)
{
	WORD InBuf[4] = { 0x0, V_LOAD_LBIOS_FROM_FLASH, 0x0, 0x0 };

	// посылаем USB-запрос V_RELOAD_LBIOS_FROM_FLASH
	if(!TLUSBBASE::LDeviceIoControl( DIOC_SEND_COMMAND,
												&InBuf, sizeof(InBuf),
												NULL, 0x0/*bytes*/,
												TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// пробуем загрузить DSP модуля из ресурса библиотеки Lusbapi
//------------------------------------------------------------------------------
BOOL TLE440::LoadModuleFromResource(void)
{
	DWORD	i, NBytes;
	char 	*BiosCodeByte;
	WORD	*BiosCodeWord;
	WORD 	PmWords, DmWords;

	// осуществим инициализацию модуля E14-440
	if(!RESET_MODULE(INIT_E440)) { LAST_ERROR_NUMBER(4); return FALSE; }

	// найдем ресурс с кодом LBIOS
	HRSRC RsRes = FindResource(hInstance, "LBIOSDATA", RT_RCDATA);
	if(!RsRes) { LAST_ERROR_NUMBER(5); return FALSE; }
	// загрузим его
	HGLOBAL RcResHandle = LoadResource(hInstance, RsRes);
	if(!RcResHandle) { LAST_ERROR_NUMBER(6); return FALSE; }
	// зафиксируем
	char  *pRcData = (char *)LockResource(RcResHandle);
	if(!pRcData) { LAST_ERROR_NUMBER(7); return FALSE; }
	// определим размер ресурса
	NBytes = SizeofResource(hInstance, RsRes);
	if(!NBytes) { LAST_ERROR_NUMBER(8); return FALSE; }

	// выделим массив в памяти компьютера под LBIOS
	BiosCodeByte = new char[NBytes+2];
	if(!BiosCodeByte)  { LAST_ERROR_NUMBER(9); return FALSE; }

	// скопируем данные ресурса
	for(i = 0x0; i < NBytes; i++) BiosCodeByte[i] = pRcData[i];

	// указатель к WORD
	BiosCodeWord = (WORD *)BiosCodeByte;

	// кол-во слов для памяти программ
	PmWords = BiosCodeWord[0];
	// кол-во слов для памяти данных
	DmWords = BiosCodeWord[PmWords+1];

	// загрузим память данных DSP
	if(!PUT_DM_ARRAY((WORD)0x0000, DmWords, (SHORT *)(BiosCodeWord + PmWords + 2))) { delete[] BiosCodeByte;  LAST_ERROR_NUMBER(13); return FALSE; }
	// загрузим память программ DSP, начиная с адреса 0х1
	else if(!PUT_PM_ARRAY((WORD)0x1, MAKE_PM_BUFFER_E440((PmWords-2)/2, (BiosCodeWord + 3)))) { delete[] BiosCodeByte;  LAST_ERROR_NUMBER(14); return FALSE; }
	// загрузим нулевую ячейку памяти программ DSP, т.е. стартанем LBIOS
	else if(!PUT_PM_ARRAY((WORD)0x0, MAKE_PM_BUFFER_E440(1, BiosCodeWord + 1))) { delete[] BiosCodeByte;  LAST_ERROR_NUMBER(14); return FALSE; }

	// освободим память
	if(BiosCodeByte) delete[] BiosCodeByte;

	// теперь найдем базовый адрес сегмента переменных LBIOS, расположенного в памяти программ DSP
	long PBA; if(!GET_PM_WORD(L_PROGRAM_BASE_ADDRESS_E440, &PBA)) { LAST_ERROR_NUMBER(15); return FALSE; }
	ProgramBaseAddress = (WORD)(PBA >> 0x8);
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// пробуем загрузить DSP модуля из файла
//------------------------------------------------------------------------------
BOOL TLE440::LoadModuleFromFile(const char * const FileName)
{
	int	BiosFile;
	DWORD	NBytes;
	char 	*BiosCodeByte;
	WORD	*BiosCodeWord;
	WORD 	PmWords, DmWords;

	// осуществим инициализацию модуля E14-440
	if(!RESET_MODULE(INIT_E440)) { LAST_ERROR_NUMBER(4); return FALSE; }

	// откроем файл с LBIOS
	BiosFile = open(FileName, O_BINARY | O_RDONLY);
	if(BiosFile == -1) { LAST_ERROR_NUMBER(10); return FALSE; }

	// определим кол-во байт для LBIOS
	NBytes = filelength(BiosFile);
	if((long)NBytes == -1) { LAST_ERROR_NUMBER(11); return FALSE; }

	// выделим память под массив LBIOS
	BiosCodeByte = new char[NBytes+2];
	if(!BiosCodeByte)  { LAST_ERROR_NUMBER(9); return FALSE; }

	if(read(BiosFile, BiosCodeByte, NBytes) == -1) { LAST_ERROR_NUMBER(12); return FALSE; }

	// закроем файл
	close(BiosFile);

	// указатель к WORD
	BiosCodeWord = (WORD *)BiosCodeByte;

	// кол-во слов для памяти программ
	PmWords = BiosCodeWord[0];
	// кол-во слов для памяти данных
	DmWords = BiosCodeWord[PmWords+1];

	// загрузим память данных DSP
	if(!PUT_DM_ARRAY((WORD)0x0000, DmWords, (SHORT *)(BiosCodeWord + PmWords + 2))) { delete[] BiosCodeByte;  LAST_ERROR_NUMBER(13); return FALSE; }
	// загрузим память программ DSP, начиная с адреса 0х1
	else if(!PUT_PM_ARRAY((WORD)0x1, MAKE_PM_BUFFER_E440((PmWords-2)/2, (BiosCodeWord + 3)))) { delete[] BiosCodeByte;  LAST_ERROR_NUMBER(14); return FALSE; }
	// загрузим нулевую ячейку памяти программ DSP, т.е. стартанем LBIOS
	else if(!PUT_PM_ARRAY((WORD)0x0, MAKE_PM_BUFFER_E440(1, BiosCodeWord + 1))) { delete[] BiosCodeByte;  LAST_ERROR_NUMBER(14); return FALSE; }

	// освободим память
	if(BiosCodeByte) delete[] BiosCodeByte;

	// теперь найдем базовый адрес сегмента переменных LBIOS, расположенного в памяти программ DSP
	long PBA; if(!GET_PM_WORD(L_PROGRAM_BASE_ADDRESS_E440, &PBA)) { LAST_ERROR_NUMBER(15); return FALSE; }
	ProgramBaseAddress = (WORD)(PBA >> 0x8);
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
long *TLE440::Transform_Buffer_E440(WORD PmPoints, long * const Buffer)
{
	WORD i;

	for(i = 0x0; i < PmPoints; i++)
		Buffer[i] = ((Buffer[i] & 0x0000FFFF) << 0x8) | ((Buffer[i] & 0x00FF0000) >> 0x10);
	return Buffer;
}

//------------------------------------------------------------------------------------
// чтение дескриптора модуля (системная часть ППЗУ модуля)
//------------------------------------------------------------------------------------
BOOL TLE440::GetModuleDescriptor(void)
{
	WORD i;
	SHORT *ptr;

	// приведём указатель на структуру к типу (SHORT *)
	ptr = (SHORT *)&ModuleDescriptor;
	// попробуем зачитать системную облать ППЗУ модуля
	for(i = 0x0; i < (sizeof(MODULE_DESCRIPTOR)/2); i++) if(!READ_FLASH_WORD(i, &ptr[i])) { LAST_ERROR_NUMBER(54); return FALSE; }
	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------------
// сохранение дескриптора модуля (системная часть ППЗУ модуля)
//------------------------------------------------------------------------------------
BOOL TLE440::PutModuleDescriptor(void)
{
	WORD i;
	SHORT *ptr;

	// приведём указатель на структуру к типу (SHORT *)
	ptr=(SHORT *)&ModuleDescriptor;
	// разрешим процедуру записи в ППЗУ
	if(!ENABLE_FLASH_WRITE(TRUE)) { LAST_ERROR_NUMBER(55); return FALSE; }
	// разрешим запись в системную облать ППЗУ
	IsServiceFlashWriting = TRUE;
	for(i = 0x0; i < (sizeof(MODULE_DESCRIPTOR)/2); i++) if(!WRITE_FLASH_WORD(i, ptr[i])) { IsServiceFlashWriting = FALSE; LAST_ERROR_NUMBER(56); return FALSE; }
	// запретим запись в системную облать ППЗУ
	IsServiceFlashWriting = FALSE;
	// запретим процедуру записи в ППЗУ
	if(!ENABLE_FLASH_WRITE(FALSE)) { LAST_ERROR_NUMBER(57); return FALSE; }
	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------------
// получение информации о LBIOS
//------------------------------------------------------------------------------------
BOOL TLE440:: GetDspDescriptor(void)
{
	BYTE *bPtr;
	WORD i;
	SHORT *sPtr;

	// приведём указатели
	bPtr = (BYTE  *)&DspDescriptor;
	sPtr = (SHORT *)&DspDescriptor;
	// вычитываем дескриптор DSP c кратностью SHORT
	for(i = 0x0; i < sizeof(DSP_DESCRIPTOR)/2; i++) if(!GET_LBIOS_WORD((WORD)(L_DSP_INFO_STUCTURE_E440 + i), &sPtr[i])) { LAST_ERROR_NUMBER(16); return FALSE; }
	// если нужно - дочитываем последний байт
	if(sizeof(DSP_DESCRIPTOR)%2)
	{
		if(!GET_LBIOS_WORD((WORD)(L_DSP_INFO_STUCTURE_E440 + sizeof(DSP_DESCRIPTOR)/2), (SHORT *)&i)) { LastErrorNumber = 13; return FALSE; }
		bPtr[sizeof(DSP_DESCRIPTOR)-0x1] = (BYTE)(i & 0xFF);
	}

	// проверим полученную структуру: возмодно у нас старый LBIOS версии ниже 3.0
	if(strcmp((char *)DspDescriptor.Manufacturer, "L-Card Ltd."))
	{
		WORD lbv;

		ZeroMemory(&DspDescriptor, sizeof(DSP_DESCRIPTOR));
		strcpy((char *)DspDescriptor.DspName, "ADSP-2185M");
		// прочитаем версию LBIOS
		if(!GET_LBIOS_WORD(L_LBIOS_VERSION_E440, (SHORT *)&lbv)) { LAST_ERROR_NUMBER(16); return FALSE; }
		sprintf((char *)DspDescriptor.Version, "%2u.%2u", lbv >> 0x8, lbv & 0xFF);
		strcpy((char *)DspDescriptor.Created, "Unknown");
		strcpy((char *)DspDescriptor.Manufacturer, "L-Card Ltd.");
		strcpy((char *)DspDescriptor.Author, "Unknown");
	}
	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------------
// структура с информацией о драйвере микроконтроллера
//------------------------------------------------------------------------------------
BOOL TLE440::GetMcuDescriptor(void)
{
	WORD InBuf[4] = { 0x1, V_GET_MCU_DESCRIPTOR, 0x0, 0x0 };

	// сначала попробуем зачитать новый формат дескриптора MCU (для ревизий модуля 'E' и выше)
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), &FirmwareDescriptor, sizeof(FIRMWARE_DESCRIPTOR), TimeOut))
	{
		// сбросим структуру дескриптора MCU
		ZeroMemory(&FirmwareDescriptor, sizeof(FIRMWARE_DESCRIPTOR));

		// теперь попробуем зачитать старый формат дескриптора MCU (для ревизий модуля 'С' и 'D' ??? )
		if(TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), &OldFirmwareDescriptor, sizeof(OLD_FIRMWARE_DESCRIPTOR), TimeOut))
		{
			strcpy((char *)FirmwareDescriptor.McuName,		"AVR ATmega8515");
			strcpy((char *)FirmwareDescriptor.Version,		(char *)OldFirmwareDescriptor.Version);
			strcpy((char *)FirmwareDescriptor.Created,		(char*)OldFirmwareDescriptor.Created);
			strcpy((char *)FirmwareDescriptor.Manufacturer, (char*)OldFirmwareDescriptor.Manufacturer);
			strcpy((char *)FirmwareDescriptor.Author,		(char*)OldFirmwareDescriptor.Author);
		}
		else
		{
			// для ревизий модуля 'B' и ниже
			strcpy((char *)FirmwareDescriptor.McuName,		"AVR AT90S8515");
			strcpy((char *)FirmwareDescriptor.Version,		"Unknown");
			strcpy((char *)FirmwareDescriptor.Created,		"Unknown");
			strcpy((char *)FirmwareDescriptor.Manufacturer,	"L-Card");
			strcpy((char *)FirmwareDescriptor.Author,			"Unknown");
		}
	}

	return TRUE;
}

//------------------------------------------------------------------------------
// выставление аттрибутов работы микроконтроллера MCU модуля
//------------------------------------------------------------------------------
BOOL TLE440::SetMcuAttributes(WORD McuAttributes)
{
	WORD InBuf[4] = { 0x0, V_SET_MCU_ATTRIBUTES, McuAttributes, 0x0 };

	// для ревизий модуля 'D' и выше вносим изменения в штатную работу MCU модуля
	if((this->ModuleDescriptor.Revision >= 'D') && (this->ModuleDescriptor.Revision <= 'Z'))
	{
		// посылаем USB-запрос в MCU
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND,
												&InBuf, sizeof(InBuf),
												NULL, 0/*bytes*/,
												TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	}
	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// функция корректного оснатова АЦП для модуля с ревизия младше 'C'
//------------------------------------------------------------------------------
BOOL TLE440::StopAdcForOldRevision(void)
{
	WORD InBuf[4] = { 0x0, V_START_ADC, 0x0 | DSP_DM, (WORD)(AdcPars.AdcFifoLength/2.0)};

	// шлём USB-запрос в MCU на запуск АЦП, одновременно зачищая
	// соответствующие переменные в драйвере микроконтроллера
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(3); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// функция корректного остонова ЦАП для модуля с ревизия младше 'C'
//------------------------------------------------------------------------------
BOOL TLE440::StopDacForOldRevision(void)
{
	// всё хорошо :)))))
	return TRUE;
}

