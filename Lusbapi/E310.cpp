//------------------------------------------------------------------------------
#if defined(_MSC_VER)
	#define NOMINMAX	// https://demin.ws/blog/russian/2009/02/15/min-max-in-visual-studio/
#endif
#include <algorithm>
#include <stdio.h>
#include <math.h>
//
#include "e310.h"
#include "ErrorBaseIds.h"

#define 	LAST_ERROR_NUMBER(ErrorNumber)	LastErrorNumber = E310_BASE_ERROR_ID + ErrorNumber

// ----------------------------------------------------------------------------------
//  коструктор
// ----------------------------------------------------------------------------------
TLE310::TLE310(HINSTANCE hInst) : TLUSBBASE(hInst)
{
	// проинициализируем все необходимые переменные
	InitVars();
}

// ----------------------------------------------------------------------------------
//  деструктор
// ----------------------------------------------------------------------------------
TLE310::~TLE310() { }



// ==================================================================================
// Функции общего назначения для работы с модулем Е-310
// ==================================================================================
// ----------------------------------------------------------------------------------
//  Откроем виртуальный слот для доступа к USB модулю
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::OpenLDevice (WORD VirtualSlot)
{
	char ModuleName[LONG_MODULE_NAME_STRING_LENGTH];

	// проинициализируем все необходимые переменные
	InitVars();
	// сбросим последнюю ошибку
	TLUSBBASE::ResetLastErrorInfo();
	//  попробуем открыть виртуальный слот для доступа к USB модулю
	if(!TLUSBBASE::OpenLDeviceByID(VirtualSlot, E310_ID)) { return FALSE; }
	// попробуем прочитать название модуля
	else if(!TLUSBBASE::GetModuleName(ModuleName)) { TLUSBBASE::CloseLDevice(); return FALSE; }
	// убедимся, что это модуль E-310
	else if(strcmp(ModuleName, "E-310")) { TLUSBBASE::CloseLDevice(); LAST_ERROR_NUMBER(0); return FALSE; }
	// попробуем запустить микроконтроллер в режиме 'Загрузчика' (BootLoader)
	else if(!RunMcuApplication(BOOT_LOADER_START_ADDRESS)) { TLUSBBASE::CloseLDevice(); LAST_ERROR_NUMBER(1); return FALSE; }
	// а теперь запустим микроконтроллер в режиме 'Приложения' (Application)
	else if(!RunMcuApplication(FIRMWARE_START_ADDRESS, REINIT_SWITCH_PARAM)) { TLUSBBASE::CloseLDevice(); LAST_ERROR_NUMBER(2); return FALSE; }
	// теперь можно прочитать структуру с параметрами цифровых линий,
	// чтобы узнать разрешены ли дополнительные цифровые линии,
	// которые завязвны на генератор и частотомер
	else if(!GetArray((BYTE *)&TtlPars, sizeof(TTL_PARS), TTL_PARS_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//-----------------------------------------------------------------------------------
// освободим текущий виртуальный слот
//-----------------------------------------------------------------------------------
BOOL WINAPI TLE310::CloseLDevice(void)
{
	BOOL Status = TRUE;

	// останов работы генератора
	if(IsGeneratorBusy)
		{ if(!STOP_GENERATOR()) Status = FALSE; }
	// останов работы частотомера (FM)
	if(IsFmBusy)
		{ if(!STOP_FM()) Status = FALSE; }
	// закроем устройство
	if(!TLUSBBASE::CloseLDevice()) Status = FALSE;
	// возвращаем статус выполнения функции
	return Status;
}

//-----------------------------------------------------------------------------------
// освободим указатель на устройство
//-----------------------------------------------------------------------------------
BOOL WINAPI TLE310::ReleaseLInstance(void)
{
	BOOL Status = TRUE;

	// освободим идентификатор виртуального слота
	if(!CloseLDevice()) Status = FALSE;
	// освободим указатель на устройство
	delete this;
	// возвращаем статус выполнения функции
	return Status;
}

// ----------------------------------------------------------------------------------
// возвращается дескриптор устройства
// ----------------------------------------------------------------------------------
HANDLE WINAPI TLE310::GetModuleHandle(void) { return hDevice; }

// ----------------------------------------------------------------------------------
//  Чтение название модуля
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::GetModuleName(char * const ModuleName)
{
	// проверим буфер
	if(!ModuleName) { LAST_ERROR_NUMBER(3); return FALSE; }
	// теперь попробуем узнать название модуля
	else if(!TLUSBBASE::GetModuleNameByLength(ModuleName, LONG_MODULE_NAME_STRING_LENGTH)) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

// ----------------------------------------------------------------------------------
//  Получение текущей скорости работы шины USB
//-----------------------------------------------------------------------------------
BOOL WINAPI TLE310::GetUsbSpeed(BYTE * const UsbSpeed)
{
	// проверим указатель
	if(!UsbSpeed) { LAST_ERROR_NUMBER(1); return FALSE; }
	// теперь попробуем узнать скорость работы USB шины
	else if(!TLUSBBASE::GetUsbSpeed(UsbSpeed)) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

// ----------------------------------------------------------------------------------
// управления режимом низкого электропотребления модуля E-310
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::LowPowerMode(BOOL LowPowerFlag)
{
/*	BYTE Flag = (BYTE)(LowPowerFlag ? 0x1 : 0x0);

	// попробуем переслать в модуль команду управления низким электропотреблением
	if(!PutArray(&Flag, 0x1, SUSPEND_MODE_FLAG)) { return FALSE; }
	// всё хорошо
	else*/ return TRUE;
}

// ----------------------------------------------------------------------------------
// Функция выдачи строки с последней ошибкой
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::GetLastErrorInfo(LAST_ERROR_INFO_LUSBAPI * const LastErrorInfo)
{
	BOOL Status;

	// вычитываем название ошибки
	Status = TLUSBBASE::GetLastErrorInfo(LastErrorInfo);
	// если можно - сбросим последнюю ошибку
	if(Status) TLUSBBASE::ResetLastErrorInfo();
   // озвращаем статус выполнения функции
	return Status;
}



// ==================================================================================
// -------------------- ФУНКЦИИ ДЛЯ РАБОТЫ С ГЕНЕРАТОРОМ ----------------------------
// ==================================================================================
// ----------------------------------------------------------------------------------
// функция чтения текущих параметров работы генератора
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::GET_GENERATOR_PARS(GENERATOR_PARS_E310 * const GenPars)
{
	WORD i;
	DWORD StartFreqCode, DeltaFreqCode;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру параметров генератора
	else if(!GenPars) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }

	// проверим реальность Ревизии модуля
	for(i = 0x0; i< REVISIONS_QUANTITY_E310; i++)
		if(ModuleDescriptor.Revision == REVISIONS_E310[i]) break;
	if(i == REVISIONS_QUANTITY_E310) { LAST_ERROR_NUMBER(5); return FALSE; }

	// обнулим локальные переменные, относящиеся к генератору
	ZeroMemory(&ModulePars.Dds, sizeof(DDS_PARS));

	// теперь можно прочитать текущие параметры работы из модуля
	if(!GetArray((BYTE *)&ModulePars, sizeof(MODULE_PARS), MODULE_PARS_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }


	// *** определимся с параметрами работы генератора ***
	// == разрешение работы генератора
	GenPars->GeneratorEna = ModulePars.Dds.ControlReg.BitFields.DAC_ENA ? TRUE : FALSE;

	// == источник тактирующего сигнала для генератора: внутренний или внешний
	GenPars->MasterClockSource = ModulePars.ControlReg.BitFields.DDS_MCLK_SOURCE;
	// == величина тактирующего сигнала генератора в кГц
	if(GenPars->MasterClockSource == INTERNAL_MASTER_CLOCK_E310)
		GenPars->MasterClock = INTERNAL_DDS_MASTER_CLOCK;
	else if(GenPars->MasterClockSource == EXTERNAL_MASTER_CLOCK_E310)
		GenPars->MasterClock = ModulePars.Dds.MasterClock;
	else { LAST_ERROR_NUMBER(7); return FALSE; }

	// == стартовая частота сканирования в кГц
	StartFreqCode = (ModulePars.Dds.HigherStartFrequencyReg.BitFields.Value12bit << 12) |
						  ModulePars.Dds.LowerStartFrequencyReg.BitFields.Value12bit;
	GenPars->StartFrequency = StartFreqCode * GenPars->MasterClock / MAX_FREQ_DDS_CODE;
	// == частота приращения при сканировании в кГц
	DeltaFreqCode = (ModulePars.Dds.HigherDeltaFrequencyReg.BitFields.Value11bit << 12) |
						  ModulePars.Dds.LowerDeltaFrequencyReg.BitFields.Value12bit;
	GenPars->FrequencyIncrements = DeltaFreqCode * GenPars->MasterClock / MAX_FREQ_DDS_CODE;
	// == кол-во приращений частоты
	GenPars->NumberOfIncrements = ModulePars.Dds.NumberOfIncrementsReg.BitFields.Value12bit;
	// == конечная частота сканирования
	GenPars->FinalFrequency = GenPars->StartFrequency + (GenPars->FrequencyIncrements * GenPars->NumberOfIncrements);

	// == тип базового интервала приращения, который может быть
	//    кратен: периоду MCLK или периоду выходного сигнала
	GenPars->IncrementIntervalPars.BaseIntervalType = (BYTE)(ModulePars.Dds.IncrementIntervalReg.BitFields.ADDR & 0x1);
	// == индекс умножителя для базового интервала приращения
	GenPars->IncrementIntervalPars.MultiplierIndex = ModulePars.Dds.IncrementIntervalReg.BitFields.Multiplier;
	// == величина умножителя для базового интервала приращения
	GenPars->IncrementIntervalPars.MultiplierValue = DdsMultiplierArray[ModulePars.Dds.IncrementIntervalReg.BitFields.Multiplier];
	// == кол-во циклов (либо MCLK, либо периодов сигнала) в интервале приращений
	GenPars->IncrementIntervalPars.BaseIntervalsNumber = ModulePars.Dds.IncrementIntervalReg.BitFields.Value11bit;

	// == тип циклического автосканирования выходного сигнала:
	//    нет циклического сканирования, 'пила' или 'треугольник'
	GenPars->CyclicAutoScanType = ModulePars.Dds.AutoScanType;
	// == тип линии "INTERRUPT" для останова работы генератора: внутренняя (от MCU) или внешняя
	GenPars->InterrupLineType = ModulePars.ControlReg.BitFields.DDS_INTERRUPT_SOURCE;
	// == разрешение сигнала на выходе "Меандр"
	GenPars->SquareWaveOutputEna = ModulePars.Dds.ControlReg.BitFields.MSBOUTEN ? TRUE : FALSE;
	// тип CyclicAutoScanType определяет следующие параметры
	if(GenPars->CyclicAutoScanType == NO_CYCLIC_AUTOSCAN_E310)
	{
		// == тип инкрементации частоты генератора: внутренняя (автоматическая) или с помощью управляющей линии "CTRL"
		GenPars->IncrementType	= ModulePars.Dds.ControlReg.BitFields.INT_OR_EXT_INCR;
		// == тип линии "CTRL" для (управления инкрементацией частоты)/старта генератора: внутренняя (от MCU) или внешняя
		GenPars->CtrlLineType	= ModulePars.ControlReg.BitFields.DDS_CTRL_SOURCE;
	}
	else
	{
		// == тип инкрементации частоты генератора: внутренняя (автоматическая)
		GenPars->IncrementType											= AUTO_INCREMENT_E310;
		ModulePars.Dds.ControlReg.BitFields.INT_OR_EXT_INCR	= AUTO_INCREMENT_E310;
		// == тип линии "CTRL" для (управления инкрементацией частоты)/старта генератора: внутренняя (от MCU)
		GenPars->CtrlLineType = ModulePars.ControlReg.BitFields.DDS_CTRL_SOURCE = INTERNAL_CTRL_LINE_E310;
	}

	// == общая длительность приращения в мс (только для интервала приращений по MCLK), иначе 0.0
	if(GenPars->IncrementType == AUTO_INCREMENT_E310)
	{
		if(GenPars->IncrementIntervalPars.BaseIntervalType == CLOCK_PERIOD_INCREMENT_INTERVAL_E310)
			GenPars->IncrementIntervalPars.Duration = GenPars->IncrementIntervalPars.BaseIntervalsNumber * (GenPars->IncrementIntervalPars.MultiplierValue/GenPars->MasterClock);
		else
			GenPars->IncrementIntervalPars.Duration = 0.0;
	}
	else GenPars->IncrementIntervalPars.Duration = 0.0;

	// == формирование выходного синхросигнала на линии "SYNCOUT":
	// при каждом приращении частоты или только по окончании
	GenPars->SynchroOutEna = ModulePars.Dds.ControlReg.BitFields.SYNCOUTEN;
	GenPars->SynchroOutType = ModulePars.Dds.ControlReg.BitFields.SYNCSEL;

	// == тип аналогового сигнала на выходах 10 и 50 Ом: синусоидальный (TRUE) или треугольный (FALSE)
	GenPars->AnalogOutputsPars.SignalType = ModulePars.Dds.ControlReg.BitFields.SIN_OR_TRI;
	// == тип смещения на выходе 10 Ом: внутреннее или внешнее
	GenPars->AnalogOutputsPars.Output10OhmOffsetSource = ModulePars.ControlReg.BitFields.DDS_10OHM_SIGNAL_OFFSET_SOURCE;
	// == величина внутреннего смещение на выходе 10 Ом в В
	//   коду 000 --> -4B
	//   коду 255 --> +4B
	GenPars->AnalogOutputsPars.Output10OhmOffset = 8.0*ModulePars.DigitalPotentiometers.Output10OhmOffset.BitFields.Value8bit/255.0 - 4.0;
	// индекс усиления выходного тракта генератора
	GenPars->AnalogOutputsPars.GainIndex = GeneratorGainIndexes[ModulePars.ControlReg.BitFields.DDS_SIGNAL_GAIN1][ModulePars.ControlReg.BitFields.DDS_SIGNAL_GAIN2];
	// усиление выходного тракта генератора в дБ
	GenPars->AnalogOutputsPars.GaindB = GeneratorGaindBArray[GenPars->AnalogOutputsPars.GainIndex];
	// амплитуда сигнала на выходе 10 Ом в В
	GenPars->AnalogOutputsPars.Output10OhmInV = Output10OhmVArray[GenPars->AnalogOutputsPars.GainIndex];
	// амплитуда сигнала на выходе 10 Ом в дБ
	GenPars->AnalogOutputsPars.Output10OhmIndB = Output10OhmdBArray[GenPars->AnalogOutputsPars.GainIndex];
	// амплитуда сигнала на выходе 50 Ом в В
	GenPars->AnalogOutputsPars.Output50OhmInV = Output50OhmVArray[GenPars->AnalogOutputsPars.GainIndex];
	// амплитуда сигнала на выходе 50 Ом в дБ
	GenPars->AnalogOutputsPars.Output50OhmIndB = Output50OhmdBArray[GenPars->AnalogOutputsPars.GainIndex];

	// все хорошо :)))))
	return TRUE;
}

// ----------------------------------------------------------------------------------
// функция установки всевозможных параметров работы генератора
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::SET_GENERATOR_PARS(GENERATOR_PARS_E310 * const GenPars)
{
	WORD i;
	DWORD StartFreqCode, StopFreqCode;
	double StartFreqKHz, StopFreqKHz;
	DWORD FreqIncCode;
	double AbsFreqIncKHz, FreqIncSign;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру параметров генератора
	else if(!GenPars) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }
	// если разрешено использовать дополнительный цифровые линии, которые совместимы
	// с SPI MCU модуля, то полноценно работать с измерителем частоты невозможно
	else if(TtlPars.TtlConfig.BitFields.ADD_TTL_LINES_ENA) { LAST_ERROR_NUMBER(8); return FALSE; }
	// проверим флажок занятости генератора
	else if(IsGeneratorBusy) { LAST_ERROR_NUMBER(9); return FALSE; }

	// проверим реальность Ревизии модуля
	for(i = 0x0; i< REVISIONS_QUANTITY_E310; i++)
		if(ModuleDescriptor.Revision == REVISIONS_E310[i]) break;
	if(i == REVISIONS_QUANTITY_E310) { LAST_ERROR_NUMBER(5); return FALSE; }

	// обнулим локальные переменные, относящиеся к генератору
	ZeroMemory(&ModulePars.Dds, sizeof(DDS_PARS));
	ModulePars.ControlReg.Value = 0x0;
	ModulePars.DigitalPotentiometers.Output10OhmOffset.Value = 0x0;

	// установим зарезервированные поля Контрольного Регистра DDS
	ModulePars.Dds.ControlReg.BitFields.Reserved0 = 0x3;
	ModulePars.Dds.ControlReg.BitFields.Reserved1 = 0x1;
	ModulePars.Dds.ControlReg.BitFields.Reserved2 = 0x3;

	// адрес контрольного регистра DDS
	ModulePars.Dds.ControlReg.BitFields.ADDR = CONTROL_BITS_ADDR_REG;

	// *** определимся с параметрами работы генератора ***
	// == разрешение работы генератора
	GenPars->GeneratorEna = FALSE; 		// GenPars->GeneratorEna = TRUE только после выполнения ф. START_GENERATOR()
	ModulePars.Dds.ControlReg.BitFields.DAC_ENA = (BYTE)(GenPars->GeneratorEna ? 0x1 : 0x0);
	// всегда это поле устанавливаем в лог. '1'
	ModulePars.Dds.ControlReg.BitFields.B24 = 0x1;

	// == источник тактирующего сигнала для генератора: внутренний или внешний
	if(GenPars->MasterClockSource >= INVALID_MASTER_CLOCK_SOURCE_E310)
		GenPars->MasterClockSource = INTERNAL_MASTER_CLOCK_E310;
	ModulePars.ControlReg.BitFields.DDS_MCLK_SOURCE = GenPars->MasterClockSource;

	// == величина тактирующего сигнала генератора в кГц
	if(GenPars->MasterClockSource == INTERNAL_MASTER_CLOCK_E310)
		ModulePars.Dds.MasterClock = GenPars->MasterClock = INTERNAL_DDS_MASTER_CLOCK;
	else if(GenPars->MasterClockSource == EXTERNAL_MASTER_CLOCK_E310)
	{
		GenPars->MasterClock = fabs(GenPars->MasterClock);
		if((GenPars->MasterClock < 0.001) || (GenPars->MasterClock > INTERNAL_DDS_MASTER_CLOCK))
		{
			GenPars->MasterClock = INTERNAL_DDS_MASTER_CLOCK;
			ModulePars.ControlReg.BitFields.DDS_MCLK_SOURCE = GenPars->MasterClockSource = INTERNAL_MASTER_CLOCK_E310;;
		}
		ModulePars.Dds.MasterClock = GenPars->MasterClock;
	}

	// == кол-во приращений частоты сканирования
	if(GenPars->NumberOfIncrements > 4095) GenPars->NumberOfIncrements = 4095;
	ModulePars.Dds.NumberOfIncrementsReg.BitFields.ADDR = NUMBER_OF_INCREMENTS_ADDR_REG;
	ModulePars.Dds.NumberOfIncrementsReg.BitFields.Value12bit = GenPars->NumberOfIncrements;

	// == частота приращений в кГц
	FreqIncSign = (GenPars->FrequencyIncrements >= 0.0) ? 1.0 : (-1.0);
	AbsFreqIncKHz = fabs(GenPars->FrequencyIncrements);
	// узнаем код частоты приращений
	FreqIncCode = (DWORD)((AbsFreqIncKHz / GenPars->MasterClock) * MAX_FREQ_DDS_CODE + 0.5);
	// проверим на максимальное значение
	if(FreqIncCode > MAX_FREQ_DDS_CODE) FreqIncCode = MAX_FREQ_DDS_CODE;
	// вычислим актуальную частоту приращений
	AbsFreqIncKHz = GenPars->MasterClock * FreqIncCode / MAX_FREQ_DDS_CODE;
	GenPars->FrequencyIncrements = AbsFreqIncKHz * FreqIncSign;
	// младшие 12 бит кода частоты приращений
	ModulePars.Dds.LowerDeltaFrequencyReg.BitFields.ADDR = LOWER_DELTA_FREQUENCY_ADDR_REG;
	ModulePars.Dds.LowerDeltaFrequencyReg.BitFields.Value12bit = (WORD)(FreqIncCode & 0x0FFF);
	// старшие 12 бит кода частоты приращений
	ModulePars.Dds.HigherDeltaFrequencyReg.BitFields.ADDR = HIGHER_DELTA_FREQUENCY_ADDR_REG;
	ModulePars.Dds.HigherDeltaFrequencyReg.BitFields.Value11bit = (WORD)((FreqIncCode >> 12) & 0x07FF);
	ModulePars.Dds.HigherDeltaFrequencyReg.BitFields.Sign = (WORD)((FreqIncSign > 0.0) ? 0x0 : 0x1);

	// == стартовая частота сканирования в кГц
	StartFreqKHz = GenPars->StartFrequency;
	// узнаем код стартовой частоты
	StartFreqCode = (DWORD)((StartFreqKHz / GenPars->MasterClock) * MAX_FREQ_DDS_CODE + 0.5);
	// проверим на максимальное значение
	if(StartFreqCode > MAX_FREQ_DDS_CODE) StartFreqCode = MAX_FREQ_DDS_CODE;
	// вычислим актуальную стартовую частоту DDS
	GenPars->StartFrequency = StartFreqKHz = GenPars->MasterClock * StartFreqCode / MAX_FREQ_DDS_CODE;
	// младшие 12 бит кода стартовой частоты
	ModulePars.Dds.LowerStartFrequencyReg.BitFields.ADDR = LOWER_START_FREQUENCY_ADDR_REG;
	ModulePars.Dds.LowerStartFrequencyReg.BitFields.Value12bit = (WORD)(StartFreqCode & 0x0FFF);
	// старшие 12 бит кода стартовой частоты
	ModulePars.Dds.HigherStartFrequencyReg.BitFields.ADDR = HIGHER_START_FREQUENCY_ADDR_REG;
	ModulePars.Dds.HigherStartFrequencyReg.BitFields.Value12bit = (WORD)((StartFreqCode >> 12) & 0x0FFF);

	// == конечная частота сканирования в кГц
	// используется исключительно в циклическом автосканировании типа 'треугольник'
	// вычислим конечную частоту
	StopFreqKHz = StartFreqKHz + ModulePars.Dds.NumberOfIncrementsReg.BitFields.Value12bit * GenPars->FrequencyIncrements;
	// проверим на отрицительность - чего не должно быть!
	if(StopFreqKHz < 0.0) { LAST_ERROR_NUMBER(10); return FALSE; }
	// проверим на максимум
	else if(StopFreqKHz > (GenPars->MasterClock/2.0)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// вычислим код конечной частоты
	StopFreqCode = (DWORD)((StopFreqKHz / GenPars->MasterClock) * MAX_FREQ_DDS_CODE + 0.5);
	// проверим на максимально возможное значение - чего не должно быть!
	if(StopFreqCode > MAX_FREQ_DDS_CODE)  { LAST_ERROR_NUMBER(11); return FALSE; }
	// вычислим актуальную конечную частоту DDS
	GenPars->FinalFrequency = StopFreqKHz = GenPars->MasterClock * StopFreqCode / MAX_FREQ_DDS_CODE;
	// младшие 12 бит кода конечной частоты
	ModulePars.Dds.LowerStopFrequencyReg.BitFields.ADDR = LOWER_START_FREQUENCY_ADDR_REG;
	ModulePars.Dds.LowerStopFrequencyReg.BitFields.Value12bit = (WORD)(StopFreqCode & 0x0FFF);
	// старшие 12 бит кода конечной частоты
	ModulePars.Dds.HigherStopFrequencyReg.BitFields.ADDR = HIGHER_START_FREQUENCY_ADDR_REG;
	ModulePars.Dds.HigherStopFrequencyReg.BitFields.Value12bit = (WORD)((StopFreqCode >> 12) & 0x0FFF);

	// == тип базового интервала приращения, который может быть
	//    кратен: периоду MCLK или периоду выходного сигнала
	// проверим тип базового интервала приращения
	if(GenPars->IncrementIntervalPars.BaseIntervalType >= INVALID_INCREMENT_INTERVAL_TYPE_E310)
		GenPars->IncrementIntervalPars.BaseIntervalType = CLOCK_INCREMENT_INTERVAL_ADDR_REG;
	if(GenPars->IncrementIntervalPars.BaseIntervalType == WAVEFORM_CYCLE_INCREMENT_INTERVAL_E310)
		ModulePars.Dds.IncrementIntervalReg.BitFields.ADDR = CYCLE_INCREMENT_INTERVAL_ADDR_REG;
	else
		ModulePars.Dds.IncrementIntervalReg.BitFields.ADDR = CLOCK_INCREMENT_INTERVAL_ADDR_REG;

	// == умножитель интервала приращения
	// проверим индекс умножителя
	if(GenPars->IncrementIntervalPars.MultiplierIndex >= INVALID_INCREMENT_INTERVAL_MULTIPLIER_E310)
		GenPars->IncrementIntervalPars.MultiplierIndex = INCREMENT_INTERVAL_MULTIPLIERS_001_E310;
	ModulePars.Dds.IncrementIntervalReg.BitFields.Multiplier = GenPars->IncrementIntervalPars.MultiplierIndex;
	// величина умножителя
	GenPars->IncrementIntervalPars.MultiplierValue = DdsMultiplierArray[GenPars->IncrementIntervalPars.MultiplierIndex];

	// == кол-во базовых интервалов в интервале приращения
	if(GenPars->IncrementIntervalPars.BaseIntervalsNumber >= 2047)
		GenPars->IncrementIntervalPars.BaseIntervalsNumber = 2047;
	else if(GenPars->IncrementIntervalPars.BaseIntervalsNumber < 2)
		GenPars->IncrementIntervalPars.BaseIntervalsNumber = 2;
	ModulePars.Dds.IncrementIntervalReg.BitFields.Value11bit = GenPars->IncrementIntervalPars.BaseIntervalsNumber;

	// == тип циклического автосканирования выходного сигнала:
	//    нет циклического сканирования, 'пила' или 'треугольник'
	if(GenPars->CyclicAutoScanType >= INVALID_CYCLIC_AUTOSCAN_TYPE_E310)
		GenPars->CyclicAutoScanType = NO_CYCLIC_AUTOSCAN_E310;
	ModulePars.Dds.AutoScanType = GenPars->CyclicAutoScanType;

	// == формирование выходного синхросигнала на линии "SYNCOUT":
	// при каждом приращении частоты или только по окончании сканирования
	if(GenPars->SynchroOutType >= INVALID_SYNCOUT_TYPES_E310)
		GenPars->SynchroOutType = SYNCOUT_ON_EACH_INCREMENT_E310;
	ModulePars.Dds.ControlReg.BitFields.SYNCSEL = GenPars->SynchroOutType;
	ModulePars.Dds.ControlReg.BitFields.SYNCOUTEN = (BYTE)(GenPars->SynchroOutEna ? 0x1 : 0x0);

	// == тип линии "INTERRUPT" для останова работы генератора: внутренняя (от MCU) или внешняя
	if(GenPars->InterrupLineType >= INVALID_INTERRUPT_LINE_TYPE_E310)
		GenPars->InterrupLineType = INTERNAL_INTERRUPT_LINE_E310;
	ModulePars.ControlReg.BitFields.DDS_INTERRUPT_SOURCE = GenPars->InterrupLineType;
	// == разрешение сигнала на цифровом выходе "Меандр"
	ModulePars.Dds.ControlReg.BitFields.MSBOUTEN = (BYTE)(GenPars->SquareWaveOutputEna ? 0x1 : 0x0);

	// тип CyclicAutoScanType определяет следующие параметры
	if(GenPars->CyclicAutoScanType == NO_CYCLIC_AUTOSCAN_E310)
	{
		// == тип инкрементации частоты генератора: внутренний или по внешнему сигналу на линии "СТАРТ"
		if(GenPars->IncrementType >= INVALID_INCREMENT_TYPE_E310)
			GenPars->IncrementType = AUTO_INCREMENT_E310;
		ModulePars.Dds.ControlReg.BitFields.INT_OR_EXT_INCR = GenPars->IncrementType;

		// == тип линии "CTRL" для (управления инкрементацией частоты)/старта генератора: внутренняя (от MCU) или внешняя
		if(GenPars->CtrlLineType >= INVALID_CTRL_LINE_TYPE_E310)
			GenPars->CtrlLineType = INTERNAL_CTRL_LINE_E310;
		ModulePars.ControlReg.BitFields.DDS_CTRL_SOURCE = GenPars->CtrlLineType;
	}
	else
	{
		// == тип инкрементации частоты генератора: внутренняя (автоматическая)
		GenPars->IncrementType											= AUTO_INCREMENT_E310;
		ModulePars.Dds.ControlReg.BitFields.INT_OR_EXT_INCR	= AUTO_INCREMENT_E310;
		// == тип линии "CTRL" для (управления инкрементацией частоты)/старта генератора: внутренняя (от MCU)
		GenPars->CtrlLineType = ModulePars.ControlReg.BitFields.DDS_CTRL_SOURCE = INTERNAL_CTRL_LINE_E310;
		// == формирование выходного синхросигнала на линии "SYNCOUT"
		GenPars->SynchroOutEna = TRUE;
		ModulePars.Dds.ControlReg.BitFields.SYNCOUTEN	= (BYTE)(GenPars->SynchroOutEna ? 0x1 : 0x0);
		ModulePars.Dds.ControlReg.BitFields.SYNCSEL		= GenPars->SynchroOutType = SYNCOUT_AT_END_OF_SCAN_E310;
	}

	// == общая длительность приращения в мс (только для интервала приращений по MCLK, иначе 0
	if(GenPars->IncrementType == AUTO_INCREMENT_E310)
	{
		if(GenPars->IncrementIntervalPars.BaseIntervalType == CLOCK_PERIOD_INCREMENT_INTERVAL_E310)
			GenPars->IncrementIntervalPars.Duration = GenPars->IncrementIntervalPars.BaseIntervalType * (GenPars->IncrementIntervalPars.MultiplierValue/GenPars->MasterClock);
		else
			GenPars->IncrementIntervalPars.Duration = 0.0;
	}
	else GenPars->IncrementIntervalPars.Duration = 0.0;

	// == тип аналогового сигнала на выходах 10 и 50 Ом: синусоидальный (TRUE) или треугольный (FALSE)
	if(GenPars->AnalogOutputsPars.SignalType >= INVALID_ANALOG_OUTPUT_TYPE_E310)
		GenPars->AnalogOutputsPars.SignalType = SINUSOIDAL_ANALOG_OUTPUT_E310;
	ModulePars.Dds.ControlReg.BitFields.SIN_OR_TRI = (WORD)((GenPars->AnalogOutputsPars.SignalType == SINUSOIDAL_ANALOG_OUTPUT_E310) ? 0x1 : 0x0);

	// == тип смещения на выходе 10 Ом: внутреннее или внешнее
	if(GenPars->AnalogOutputsPars.Output10OhmOffsetSource >= INVALID_OUTPUT_10_OHM_OFFSET_SOURCE_E310)
		GenPars->AnalogOutputsPars.Output10OhmOffsetSource = INTERNAL_OUTPUT_10_OHM_OFFSET_E310;
	ModulePars.ControlReg.BitFields.DDS_10OHM_SIGNAL_OFFSET_SOURCE = GenPars->AnalogOutputsPars.Output10OhmOffsetSource;

	// == величина внутреннего смещение на выходе 10 Ом в В
	//     коду 000 --> -4B
	//     коду 255 --> +4B
	if((GenPars->AnalogOutputsPars.Output10OhmOffset < -4.0) || (GenPars->AnalogOutputsPars.Output10OhmOffset > 4.0))
		GenPars->AnalogOutputsPars.Output10OhmOffset = 0.0;
	ModulePars.DigitalPotentiometers.Output10OhmOffset.BitFields.Value8bit = (WORD)(255.0*(GenPars->AnalogOutputsPars.Output10OhmOffset + 4.0)/8.0 + 0.5);

	ModulePars.DigitalPotentiometers.Output10OhmOffset.BitFields.ADDR = 0x0;
	GenPars->AnalogOutputsPars.Output10OhmOffset = 8.0*ModulePars.DigitalPotentiometers.Output10OhmOffset.BitFields.Value8bit/255.0 - 4.0;

	// == индекс усиления выходного тракта генератора
	if(GenPars->AnalogOutputsPars.GainIndex >= INVALID_ANALOG_OUTPUT_GAINS_INDEX_E310)
		GenPars->AnalogOutputsPars.GainIndex = ANALOG_OUTPUT_GAIN_PLUS_10_DB_E310;
	ModulePars.ControlReg.BitFields.DDS_SIGNAL_GAIN1 = Gain1Index[GenPars->AnalogOutputsPars.GainIndex];
	ModulePars.ControlReg.BitFields.DDS_SIGNAL_GAIN2 = Gain2Index[GenPars->AnalogOutputsPars.GainIndex];
	// == усиление выходного тракта генератора в дБ
	GenPars->AnalogOutputsPars.GaindB = GeneratorGaindBArray[GenPars->AnalogOutputsPars.GainIndex];
	// == амплитуда сигнала на выходе 10 Ом в В
	GenPars->AnalogOutputsPars.Output10OhmInV = Output10OhmVArray[GenPars->AnalogOutputsPars.GainIndex];
	// == амплитуда сигнала на выходе 10 Ом в дБ
	GenPars->AnalogOutputsPars.Output10OhmIndB = Output10OhmdBArray[GenPars->AnalogOutputsPars.GainIndex];
	// == амплитуда сигнала на выходе 50 Ом в В
	GenPars->AnalogOutputsPars.Output50OhmInV = Output50OhmVArray[GenPars->AnalogOutputsPars.GainIndex];
	// == амплитуда сигнала на выходе 50 Ом в дБ
	GenPars->AnalogOutputsPars.Output50OhmIndB = Output50OhmdBArray[GenPars->AnalogOutputsPars.GainIndex];

	// теперь можно записать текущие параметры работы в модуль
	if(!PutArray((BYTE *)&ModulePars, sizeof(MODULE_PARS), MODULE_PARS_ADDRESS)) { LAST_ERROR_NUMBER(12); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

// ----------------------------------------------------------------------------------
// старт работы генератора
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::START_GENERATOR(void)
{
	WORD InBuf[4] = { 0x0, V_START_ADC, 0x1 << GENERATOR_START_INDEX, 0x0 };

	// если разрешено использовать дополнительный цифровые линии, которые совместимы
	// с SPI MCU модуля, то полноценно работать с измерителем частоты невозможно
	if(TtlPars.TtlConfig.BitFields.ADD_TTL_LINES_ENA) { LAST_ERROR_NUMBER(8); return FALSE; }
	// шлём USB-запрос в MCU на запуск генератора
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(13); return FALSE; }

	// установим флажок занятости генератора
	IsGeneratorBusy = TRUE;

	// всё хорошо
	return TRUE;
}

// ----------------------------------------------------------------------------------
// стоп работы генератора
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::STOP_GENERATOR(void)
{
	WORD InBuf[4] = { 0x0, V_STOP_ADC, 0x1 << GENERATOR_START_INDEX, 0x0 };

	// сбросим флажок занятости генератора
	IsGeneratorBusy = FALSE;

	// шлём USB-запрос в MCU на запуск генератора
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(13); return FALSE; }
	// всё хорошо
	else return TRUE;
}






// ==================================================================================
//------------------ функции для работы с частотометром (FM) ------------------------
// ==================================================================================
// ----------------------------------------------------------------------------------
// получение текущих параметров работы частотомера (FM)
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::GET_FM_PARS(FM_PARS_E310 * const FmPars)
{
	WORD i;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру параметров генератора
	else if(!FmPars) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }

	// проверим реальность Ревизии модуля
	for(i = 0x0; i< REVISIONS_QUANTITY_E310; i++)
		if(ModuleDescriptor.Revision == REVISIONS_E310[i]) break;
	if(i == REVISIONS_QUANTITY_E310) { LAST_ERROR_NUMBER(5); return FALSE; }

	// обнулим локальные переменные, относящиеся к генератору
	ZeroMemory(&ModulePars.Dds, sizeof(DDS_PARS));
	// теперь можно прочитать текущие параметры работы из модуля
	if(!GetArray((BYTE *)&ModulePars, sizeof(MODULE_PARS), MODULE_PARS_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }

	// *** определимся с параметрами работы FM ***
	// == рабочее состояние FM
	FmPars->FmEna = ModulePars.FrequencyMeasurement.FmEna ? TRUE : FALSE;
	// == режим работы измерителя частоты
	FmPars->Mode = ModulePars.FrequencyMeasurement.Mode;
	// == состояние входного делителя частоты: 1:1 или 1:8
	FmPars->InputDivider = ModulePars.FrequencyMeasurement.InputDivider;
	// == индекс делителя базовой тактовой частоты FM
	FmPars->BaseClockRateDivIndex = ModulePars.FrequencyMeasurement.BaseClockRateDivIndex;
	// == смещение порога измерителя частоты в В
	FmPars->Offset = 4.0 - 8.0*ModulePars.DigitalPotentiometers.FrequencyMeterPorog.BitFields.Value8bit/255.0;

	// == рабочая тактовая частота счётчика FM в Гц
	FmPars->ClockRate = ModulePars.FrequencyMeasurement.ClockRate;
	// == базовая тактовая частота счётчика FM в Гц
	FmPars->BaseClockRate = ModulePars.FrequencyMeasurement.BaseClockRate;

	// все хорошо :)))))
	return TRUE;
}

// ----------------------------------------------------------------------------------
// установка требуемых параметров работы частотомера (FM)
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::SET_FM_PARS(FM_PARS_E310 * const FmPars)
{
	WORD i;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру параметров генератора
	else if(!FmPars) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }
	// если разрешено использовать дополнительный цифровые линии, которые совместимы
	// с SPI MCU модуля, то полноценно работать с измерителем частоты невозможно
	else if(TtlPars.TtlConfig.BitFields.ADD_TTL_LINES_ENA) { LAST_ERROR_NUMBER(8); return FALSE; }
	// проверим флажок занятости измерителя частоты (FM)
	else if(IsFmBusy) { LAST_ERROR_NUMBER(14); return FALSE; }

	// проверим реальность Ревизии модуля
	for(i = 0x0; i< REVISIONS_QUANTITY_E310; i++)
		if(ModuleDescriptor.Revision == REVISIONS_E310[i]) break;
	if(i == REVISIONS_QUANTITY_E310) { LAST_ERROR_NUMBER(5); return FALSE; }

	// обнулим локальные переменные, относящиеся к FM
	ZeroMemory(&ModulePars.FrequencyMeasurement, sizeof(FM_PARS));
	ModulePars.DigitalPotentiometers.FrequencyMeterPorog.Value = 0x0;

	// == рабочее состояние FM
	FmPars->FmEna = FALSE;		// FmPars->FmEna = TRUE только после выполнении ф. START_FM()
	ModulePars.FrequencyMeasurement.FmEna = (BYTE)(FmPars->FmEna ? 0x1 : 0x0);
	// == режим работы измерителя частоты - пока только метод измерения периода :(((
	if(FmPars->Mode >= GATE_MODE_FM_E310) FmPars->Mode = PERIOD_MODE_FM_E310;
	ModulePars.FrequencyMeasurement.Mode = FmPars->Mode;
	// == состояние входного делителя частоты: 1:1 или 1:8
	if(FmPars->InputDivider >= INVALID_INPUT_DIVIDER_FM_E310) FmPars->InputDivider = INPUT_DIVIDER_1_FM_E310;
	ModulePars.FrequencyMeasurement.InputDivider = FmPars->InputDivider;
	// == индекс делителя базовой тактовой частоты FM
	if(FmPars->BaseClockRateDivIndex >= INVALID_BASE_CLOCK_DIV_INDEX_FM_E310)
		FmPars->BaseClockRateDivIndex = BASE_CLOCK_DIV_01_INDEX_FM_E310;
	ModulePars.FrequencyMeasurement.BaseClockRateDivIndex = FmPars->BaseClockRateDivIndex;
	// == смещение порога измерителя частоты в В
	//     коду 000 --> +4B
	//     коду 255 --> -4B
	if((FmPars->Offset < -4.0) || (FmPars->Offset > 4.0)) FmPars->Offset = 0.0;
	ModulePars.DigitalPotentiometers.FrequencyMeterPorog.BitFields.Value8bit = (WORD)(255.0*(4.0 - FmPars->Offset)/8.0 + 0.5);
	ModulePars.DigitalPotentiometers.FrequencyMeterPorog.BitFields.ADDR 		 = 0x1;
	FmPars->Offset = 4.0 - 8.0*ModulePars.DigitalPotentiometers.FrequencyMeterPorog.BitFields.Value8bit/255.0;

	// == базовая тактовая частота счётчика FM в Гц
	ModulePars.FrequencyMeasurement.BaseClockRate = FmPars->BaseClockRate = 25000000;
	// == рабочая тактовая частота счётчика FM в Гц
	ModulePars.FrequencyMeasurement.ClockRate = FmPars->ClockRate = FmPars->BaseClockRate >> FmPars->BaseClockRateDivIndex;

	// теперь можно записать текущие параметры работы в модуль
	if(!PutArray((BYTE *)&ModulePars, sizeof(MODULE_PARS), MODULE_PARS_ADDRESS)) { LAST_ERROR_NUMBER(12); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

// ----------------------------------------------------------------------------------
// старт работы частотомера (FM)
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::START_FM(void)
{
	WORD InBuf[4] = { 0x0, V_START_ADC, 0x1 << FM_START_INDEX, 0x0 };

	// если разрешено использовать дополнительный цифровые линии, которые совместимы
	// с SPI MCU модуля, то полноценно работать с измерителем частоты невозможно
	if(TtlPars.TtlConfig.BitFields.ADD_TTL_LINES_ENA) { LAST_ERROR_NUMBER(8); return FALSE; }
	// шлём USB-запрос в MCU на запуск частотомера (FM)
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(13); return FALSE; }

	// установим флажок занятости измерителя частоты (FM)
	IsFmBusy = TRUE;

	// всё хорошо
	return TRUE;
}

// ----------------------------------------------------------------------------------
// стоп работы частотомера (FM)
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::STOP_FM(void)
{
	WORD InBuf[4] = { 0x0, V_STOP_ADC, 0x1 << FM_START_INDEX, 0x0 };

	// сбросим флажок занятости измерителя частоты (FM)
	IsFmBusy = FALSE;

	// шлём USB-запрос в MCU на запуск частотомера (FM)
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0x0, TimeOut)) { LAST_ERROR_NUMBER(13); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// ----------------------------------------------------------------------------------
// считывание отсчета измерения частоты
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::FM_SAMPLE(FM_SAMPLE_E310 * const FmSample)
{
	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру параметров генератора
	else if(!FmSample) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }
	// если разрешено использовать дополнительный цифровые линии, которые совместимы
	// с SPI MCU модуля, то полноценно работать с измерителем частоты невозможно
	else if(TtlPars.TtlConfig.BitFields.ADD_TTL_LINES_ENA) { LAST_ERROR_NUMBER(8); return FALSE; }

	// теперь можно прочитать состояние отсчета с измерителья частоты
	if(!GetArray((BYTE *)&LocFmSample, sizeof(FM_DATA), FM_SAMPLE_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }
	// как там с отсчетом FM
	if(LocFmSample.IsActual)
	{
		// установим признак действительности отсчета измерителя
		FmSample->IsActual = TRUE;
		// метод измерения периода
		if(ModulePars.FrequencyMeasurement.Mode == PERIOD_MODE_FM_E310)
		{
			// частота сигнала
			FmSample->Frequency = ModulePars.FrequencyMeasurement.ClockRate/(1000.0 * LocFmSample.PeriodCode);
			// период сигнала
			FmSample->Period = 1000.0 * LocFmSample.PeriodCode / ModulePars.FrequencyMeasurement.ClockRate;
			// скважность сигнала
			FmSample->DutyCycle = 1000.0 * LocFmSample.DutyCycleCode / ModulePars.FrequencyMeasurement.ClockRate;
		}
		// для остальный методов пока только так :(((
		else
		{
			FmSample->Frequency = FmSample->Period = FmSample->DutyCycle = 0.0;
		}
	}
	else
	{
		// обнулим все параметры
		FmSample->IsActual = FALSE;
		FmSample->Frequency = FmSample->Period = FmSample->DutyCycle = 0.0;
	}
	// всё хорошо
	return TRUE;
}




// ==================================================================================
// ------------------------ функции для работы АЦП ----------------------------------
// ==================================================================================
// ----------------------------------------------------------------------------------
// получение текущих параметров работы АЦП
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::GET_ADC_PARS(ADC_PARS_E310 * const AdcPars)
{
	WORD i;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру параметров генератора
	else if(!AdcPars) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }

	// проверим реальность Ревизии модуля
	for(i = 0x0; i< REVISIONS_QUANTITY_E310; i++)
		if(ModuleDescriptor.Revision == REVISIONS_E310[i]) break;
	if(i == REVISIONS_QUANTITY_E310) { LAST_ERROR_NUMBER(5); return FALSE; }

	// обнулим локальные переменные, относящиеся к АЦП
	ZeroMemory(&LocAdcPars, sizeof(ADC_PARS));
	// теперь можно прочитать текущие параметры работы из модуля
	if(!GetArray((BYTE *)&LocAdcPars, sizeof(ADC_PARS), ADC_PARS_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }

	// == посмотим каков источник сигнала запуска АЦП: внутренний или внешний
	AdcPars->AdcStartSource = LocAdcPars.AdcStartSource;
	// == битовая маска активных каналов
	AdcPars->ChannelsMask = (BYTE)(LocAdcPars.ChannelsMask >> 0x4);
	// == вернем входной диапазон АЦП в В
	AdcPars->InputRange = ADC_INPUT_RANGE;

	// всё хорошо
	return TRUE;
}

// ----------------------------------------------------------------------------------
// установка требуемых параметров работы АЦП
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::SET_ADC_PARS(ADC_PARS_E310 * const AdcPars)
{
	WORD i;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру параметров генератора
	else if(!AdcPars) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }

	// проверим реальность Ревизии модуля
	for(i = 0x0; i< REVISIONS_QUANTITY_E310; i++)
		if(ModuleDescriptor.Revision == REVISIONS_E310[i]) break;
	if(i == REVISIONS_QUANTITY_E310) { LAST_ERROR_NUMBER(5); return FALSE; }

	// обнулим локальные переменные, относящиеся к АЦП
	ZeroMemory(&LocAdcPars, sizeof(ADC_PARS));

	// == посмотим каков источник сигнала запуска АЦП: внутренний или внешний
	if(AdcPars->AdcStartSource >= INVALID_ADC_START_SOURCES_E310)
		AdcPars->AdcStartSource = INTERNAL_ADC_START_E310;
	LocAdcPars.AdcStartSource = AdcPars->AdcStartSource;

	// == поколдуем с битовой маской активных каналов
	AdcPars->ChannelsMask &= 0x0F;
	// для использования в MCU сдвинем влево битовую маску
	LocAdcPars.ChannelsMask = (BYTE)(AdcPars->ChannelsMask << 0x4);

	// == вернем входной диапазон АЦП в В
	AdcPars->InputRange = ADC_INPUT_RANGE;

	// теперь можно записать текущие параметры работы в модуль
	if(!PutArray((BYTE *)&LocAdcPars, sizeof(ADC_PARS), ADC_PARS_ADDRESS)) { LAST_ERROR_NUMBER(12); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// ----------------------------------------------------------------------------------
// считывание массива отсчетов с АЦП
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::GET_ADC_DATA(ADC_DATA_E310 * const AdcData)
{
	WORD i;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру параметров генератора
	else if(!AdcData) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }

	// обнулим локальные переменные, относящиеся к АЦП
	ZeroMemory(&LocAdcData, sizeof(ADC_DATA));
	// теперь можно прочитать текущее значения с АЦП
	if(!GetArray((BYTE *)&LocAdcData, sizeof(ADC_DATA), ADC_DATA_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }

	// вычленим данные с АЦП
	for(i = 0x0; i < ADC_CHANNEL_QUANTITY_E310; i++)
	{
		AdcData->DataInCode[i]	= LocAdcData.DataInCode[i];		// в кодах
		if(AdcData->DataInCode[i] != (-1))
			AdcData->DataInV[i] = ADC_INPUT_RANGE * AdcData->DataInCode[i] / 1024.0;	// в В
		else
			AdcData->DataInV[i] = -1.0;	// в В
	}

	// всё хорошо
	return TRUE;
}




// ==================================================================================
// ---------------- функции для работы с цифровыми линиями --------------------------
// ==================================================================================
// ----------------------------------------------------------------------------------
// конфигурирование цифровых линий: вход или выход
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::CONFIG_TTL_LINES(WORD Pattern, BOOL AddTtlLinesEna)
{
	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }

	// Проверим разрешение использовать дополнительный цифровые линии, которые
	// совместимы с SPI MCU модуля. Их использование исключает возможность
	// полноценной работы с генератором и измерителем частоты
	TtlPars.TtlConfig.BitFields.ADD_TTL_LINES_ENA 	= (WORD)(AddTtlLinesEna ? 0x1 : 0x0);
	// маска конфигурирования цифровых линий
	if(AddTtlLinesEna)
		TtlPars.TtlConfig.BitFields.CONFIG_PATTERN	= (WORD)(Pattern & 0x07FF);
	else
		TtlPars.TtlConfig.BitFields.CONFIG_PATTERN	= (WORD)(Pattern & 0x00FF);

	// активным будет конфигурация цифровых линий
	TtlPars.Mode = TLL_LINES_CONFIG;

	// теперь можно записать текущую конфигурацию цифровых линий в модуль
	if(!PutArray((BYTE *)&TtlPars, sizeof(TTL_PARS), TTL_PARS_ADDRESS)) { LAST_ERROR_NUMBER(12); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// ----------------------------------------------------------------------------------
// чтение состояний цифровых входных линий
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::TTL_IN (WORD * const TtlIn)
{
	WORD TtlInPattern;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }

	// теперь можно прочитать структуру с параметрами цифровых линий
	if(!GetArray((BYTE *)&TtlPars, sizeof(TTL_PARS), TTL_PARS_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }
	// посмотрим сколько цифровых линий разрешено использовать
	if(TtlPars.TtlConfig.BitFields.ADD_TTL_LINES_ENA)
		TtlInPattern = (WORD)((~TtlPars.TtlConfig.BitFields.CONFIG_PATTERN) & 0x07FF);
	else
		TtlInPattern = (WORD)((~TtlPars.TtlConfig.BitFields.CONFIG_PATTERN) & 0x00FF);
	// вычленим состояния только цифровых входных линий
	*TtlIn = TtlPars.TtlIn & TtlInPattern;
	// всё хорошо
	return TRUE;
}

// ----------------------------------------------------------------------------------
// установка состояний цифровых выходных линий
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::TTL_OUT(WORD * const TtlOut)
{
	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E-310")) { LAST_ERROR_NUMBER(0); return FALSE; }
	// сформируем состояния цифровых выходных линий
	*TtlOut &= TtlPars.TtlConfig.BitFields.CONFIG_PATTERN;
	TtlPars.TtlOut = *TtlOut;
	// активным будет запись цифровых линий
	TtlPars.Mode = TTL_LINES_OUT;
	// теперь можно записать текущую конфигурацию цифровых линий в модуль
	if(!PutArray((BYTE *)&TtlPars, sizeof(TTL_PARS), TTL_PARS_ADDRESS)) { LAST_ERROR_NUMBER(12); return FALSE; }
	// всё хорошо
	else return TRUE;
}



// ==================================================================================
// ------------ функции для работы с пользовательской информацией ППЗУ  -------------
// ==================================================================================
// ----------------------------------------------------------------------------------
// разрешение/запрещение режима записи в пользовательскую область ППЗУ
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled)
{
	// запомним признак
	this->IsUserFlashWriteEnabled = IsUserFlashWriteEnabled;
	// все хорошо :)))))
	return TRUE;
}

// ----------------------------------------------------------------------------------
// чтенние пользовательской области ППЗУ
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::READ_FLASH_ARRAY(USER_FLASH_E310 * const UserFlash)
{
	// проверим структуру
	if(!UserFlash) { LAST_ERROR_NUMBER(3); return FALSE; }
	// тупо читаем массив
	else if(!GetArray((BYTE *)UserFlash, sizeof(USER_FLASH_E310), USER_FLASH_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

// ----------------------------------------------------------------------------------
// запись пользовательской области ППЗУ
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::WRITE_FLASH_ARRAY(USER_FLASH_E310 * const UserFlash)
{
	BYTE *ptr;
	DWORD Address, Size;
	DWORD i = 0x0;

	// для начала проверим разрешение на запись
	if(!IsUserFlashWriteEnabled) { LAST_ERROR_NUMBER(15); return FALSE; }
	// теперь проверим структуру
	else if(!UserFlash) { LAST_ERROR_NUMBER(3); return FALSE; }

	// захватим ресурс критической секции
	EnterCriticalSection(&cs);
	// если нужно попробуем перевести микроконтроллер в режим 'Загрузчика' (BootLoader)
	if(IsMcuApplicationActive)
		if(!RunMcuApplication(BOOT_LOADER_START_ADDRESS)) { LAST_ERROR_NUMBER(1); LeaveCriticalSection(&cs); return FALSE; }

	Address = USER_FLASH_ADDRESS;
	Size = sizeof(USER_FLASH_E310);
	ptr = (BYTE *)UserFlash;
	while(i < Size)
	{
		DWORD sz = std::min((DWORD)MAX_USB_CONTROL_PIPE_BLOCK, Size - i);
		WORD InBuf[4] = {0x0000, 13/*Special PutArray*/, (WORD)(Address & 0xFFFF), (WORD)(Address >> 0x10) };

		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), ptr + i, sz, TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(13); return FALSE; }
		//
		Address += sz;
		i += sz;
	 }

	// попробуем вернуть микроконтроллер в режим 'Приложения' (Application)
	if(!RunMcuApplication(FIRMWARE_START_ADDRESS)) { LAST_ERROR_NUMBER(2); LeaveCriticalSection(&cs); return FALSE; }
	// освободим ресурс критической секции
	LeaveCriticalSection(&cs);
	// все хорошо :)))))
	return TRUE;
}





// ==================================================================================
// ------------- функции для работы со служебной информацией из ППЗУ ----------------
// ==================================================================================
// ----------------------------------------------------------------------------------
// получим служебную информацию о модуле из ППЗУ
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E310 * const md)
{
	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру
	else if(!md) { LAST_ERROR_NUMBER(3); return FALSE; }

	// пробуем прочитать дескриптор модуля
	if(!GetModuleDescriptor()) return FALSE;

	// читаем остальные дескрипторы
	if(!GetArray((BYTE*)&FirmwareDescriptor, sizeof(FIRMWARE_DESCRIPTOR), FIRMWARE_DESCRIPTOR_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }
	else if(!GetArray((BYTE*)&BootLoaderDescriptor, sizeof(BOOT_LOADER_DESCRIPTOR), BOOT_LOADER_DESCRIPTOR_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }

	// обнулим всю структуру MODULE_DESCRIPTION_E310
	ZeroMemory(md, sizeof(MODULE_DESCRIPTION_E310));

	// Module Info
	strncpy((char *)md->Module.CompanyName,  "L-Card Ltd.",    sizeof(md->Module.CompanyName));
	strncpy((char *)md->Module.DeviceName,   (char *)ModuleDescriptor.ModuleName,    std::min(sizeof(md->Module.DeviceName), sizeof(ModuleDescriptor.ModuleName)));
	strncpy((char *)md->Module.SerialNumber, (char *)ModuleDescriptor.SerialNumber,  std::min(sizeof(md->Module.SerialNumber), sizeof(ModuleDescriptor.SerialNumber)));
	strcpy((char *)md->Module.Comment,      			"Внешний модуль генератора-частотомера на шину USB 1.1");
	md->Module.Revision = ModuleDescriptor.Revision;
	md->Module.Modification = NO_MODULE_MODIFICATION_LUSBAPI;

	// Interface Info
	md->Interface.Active = TRUE;
	strcpy((char *)md->Interface.Name, "USB 1.1");
	strcpy((char *)md->Interface.Comment, "Full-Speed Mode (12 Mbit/s)");

	// MCU Info
	md->Mcu.Active = TRUE;
	strncpy((char *)md->Mcu.Name,   									(char *)ModuleDescriptor.McuName,    			std::min(sizeof(md->Mcu.Name), sizeof(ModuleDescriptor.McuName)));
	strncpy((char *)md->Mcu.Version.FwVersion.Version,  		(char *)FirmwareDescriptor.Version, 			std::min(sizeof(md->Mcu.Version.FwVersion.Version), sizeof(FirmwareDescriptor.Version)));
	strncpy((char *)md->Mcu.Version.FwVersion.Date,  			(char *)FirmwareDescriptor.Created, 			std::min(sizeof(md->Mcu.Version.FwVersion.Date), sizeof(FirmwareDescriptor.Created)));
	strncpy((char *)md->Mcu.Version.FwVersion.Manufacturer,  (char *)FirmwareDescriptor.Manufacturer, 		std::min(sizeof(md->Mcu.Version.FwVersion.Manufacturer), sizeof(FirmwareDescriptor.Manufacturer)));
	strncpy((char *)md->Mcu.Version.FwVersion.Author,  		(char *)FirmwareDescriptor.Author, 				std::min(sizeof(md->Mcu.Version.FwVersion.Author), sizeof(FirmwareDescriptor.Author)));
	strncpy((char *)md->Mcu.Version.FwVersion.Comment,			(char *)FirmwareDescriptor.Comment, 			std::min(sizeof(md->Mcu.Version.FwVersion.Comment), sizeof(FirmwareDescriptor.Comment)));
	strncpy((char *)md->Mcu.Version.BlVersion.Version,  		(char *)BootLoaderDescriptor.Version, 			std::min(sizeof(md->Mcu.Version.BlVersion.Version), sizeof(BootLoaderDescriptor.Version)));
	strncpy((char *)md->Mcu.Version.BlVersion.Date,  			(char *)BootLoaderDescriptor.Created, 			std::min(sizeof(md->Mcu.Version.BlVersion.Date), sizeof(BootLoaderDescriptor.Created)));
	strncpy((char *)md->Mcu.Version.BlVersion.Manufacturer,  (char *)BootLoaderDescriptor.Manufacturer, 	std::min(sizeof(md->Mcu.Version.BlVersion.Manufacturer), sizeof(BootLoaderDescriptor.Manufacturer)));
	strncpy((char *)md->Mcu.Version.BlVersion.Author,  		(char *)BootLoaderDescriptor.Author, 			std::min(sizeof(md->Mcu.Version.BlVersion.Author), sizeof(BootLoaderDescriptor.Author)));
	strncpy((char *)md->Mcu.Version.BlVersion.Comment,			"Штатный Загрузчик для внешнего USB модуля E-310", sizeof(md->Mcu.Version.BlVersion.Comment));
	strncpy((char *)md->Mcu.Comment, 								"32-bit Microcontroller with 64K Bytes Internal High-speed Flash", sizeof(md->Mcu.Comment));
	md->Mcu.ClockRate = ModuleDescriptor.ClockRate/1000.0;	// в кГц
	// определимся с номером версии Firmware микроконтроллера
	FirmwareVersionNumber = atof((char *)md->Mcu.Version.FwVersion.Version);
	if((FirmwareVersionNumber < 0.75) || (FirmwareVersionNumber > 20.0)) { LAST_ERROR_NUMBER(16); return FALSE; }

	// ADC Info
	md->Adc.Active = TRUE;
	strcpy((char *)md->Adc.Name, "Built-in ARM");
	strcpy((char *)md->Adc.Comment, "4 Channels, 10-Bit ADC converter");

	// DAC Info
	md->Dac.Active = TRUE;
	strcpy((char *)md->Dac.Name, "AD5932");
	strcpy((char *)md->Dac.Comment, "Programmable Frequency Scan Waveform Generator (DDS)");

	// IO Info
	md->DigitalIo.Active = TRUE;
	strcpy((char *)md->DigitalIo.Name, "Built-in ARM");
	md->DigitalIo.InLinesQuantity = 11;
	md->DigitalIo.OutLinesQuantity = 11;
	strcpy((char *)md->DigitalIo.Comment, "8/11 TTL/CMOS compatible lines");

	// все хорошо :)))))
	return TRUE;
}

// ----------------------------------------------------------------------------------
// запишем служебную информацию о модуле в ППЗУ
// ----------------------------------------------------------------------------------
BOOL WINAPI TLE310::SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E310 * const md)
{
//	WORD i;

	// проверим указатель на структуру
	if(!md) { LAST_ERROR_NUMBER(3); return FALSE; }

	// обнулим всю структуру MODULE_DESCRIPTOR
	ZeroMemory(&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR));

	// Module Info
	strncpy((char *)ModuleDescriptor.ModuleName,		(char *)md->Module.DeviceName,	 	std::min(sizeof(md->Module.DeviceName), sizeof(ModuleDescriptor.ModuleName)));
	strncpy((char *)ModuleDescriptor.SerialNumber, 	(char *)md->Module.SerialNumber, 	std::min(sizeof(md->Module.SerialNumber), sizeof(ModuleDescriptor.SerialNumber)));
//	strncpy((char *)ModuleDescriptor.Reserved,     	(char *)md->Module.Comment,      	std::min(sizeof(md->Module.Comment), sizeof(ModuleDescriptor.Reserved)));

	// проверим требуемую ревизию модуля
	if((	md->Module.Revision > REVISIONS_E310[REVISIONS_QUANTITY_E310 - 0x1] ||
			md->Module.Revision < REVISIONS_E310[REVISION_A_E310])) { LAST_ERROR_NUMBER(17); return FALSE; }
	else ModuleDescriptor.Revision = md->Module.Revision;

	// MCU Info
	strncpy((char *)ModuleDescriptor.McuName, (char *)md->Mcu.Name, std::min(sizeof(md->Mcu.Name), sizeof(ModuleDescriptor.McuName)));
	ModuleDescriptor.ClockRate = 1000*MCU_MASTER_CLOCK;		// в Гц

	// теперь вычислим CRC16 получившейся структуры
	ModuleDescriptor.CRC16 = 0x0;
	ModuleDescriptor.CRC16 = CalculateCrc16((BYTE *)&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR) - sizeof(WORD));

	// попробуем записать сформированный дескриптор модуля
	if(!SaveModuleDescriptor()) { LAST_ERROR_NUMBER(18); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}






// ==================================================================================
//  внутренние функции для работы с модулем
// ==================================================================================
//------------------------------------------------------------------------------
// инициализация всех необходимых переменных
//------------------------------------------------------------------------------
BOOL WINAPI TLE310::InitVars(void)
{

	WORD i;
	double Ampl = sqrt(2.0) * 0.77459666924148;		// амплитуда равная 0 дБ на нагрузке 600 Ом

	// инициализация всех локальных структур класса
	ZeroMemory(&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR));
	ZeroMemory(&ModulePars, 		sizeof(MODULE_PARS));
	ZeroMemory(&TtlPars,				sizeof(TTL_PARS));
	ZeroMemory(&LocAdcPars, 		sizeof(ADC_PARS));
	// сбросим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	IsMcuApplicationActive = FALSE;
	// сбросим флажок разрешения записи в пользователькое ППЗУ
	IsUserFlashWriteEnabled = FALSE;
	// сбросим номер версии Firmware микроконтроллера
	FirmwareVersionNumber = 0.0;
	// величина внутреннего тактирующего сигнала генератора в кГц
	INTERNAL_DDS_MASTER_CLOCK = 50000.0;
	// входной диапазон АЦП в В
	ADC_INPUT_RANGE = 3.3;

	// сбросим флажки занятости генератора и измерителя частот
	IsGeneratorBusy = IsFmBusy = FALSE;

	// максимальный код частоты DDS
	MAX_FREQ_DDS_CODE = 0x1 << 24;

	// массив соотвествия битов DDS_SIGNAL_GAIN1 и DDS_SIGNAL_GAIN1 в CONTROL_REG
	// индексам усиления генератора
	GeneratorGainIndexes[0x3][0x0] = ANALOG_OUTPUT_GAIN_PLUS_10_DB_E310;
	GeneratorGainIndexes[0x2][0x0] = ANALOG_OUTPUT_GAIN_PLUS_06_DB_E310;
	GeneratorGainIndexes[0x1][0x0] = ANALOG_OUTPUT_GAIN_PLUS_03_DB_E310;
	GeneratorGainIndexes[0x0][0x0] = ANALOG_OUTPUT_GAIN_00_DB_E310;
	GeneratorGainIndexes[0x3][0x1] = ANALOG_OUTPUT_GAIN_PLUS_04_DB_E310;
	GeneratorGainIndexes[0x2][0x1] = INVALID_ANALOG_OUTPUT_GAINS_INDEX_E310;
	GeneratorGainIndexes[0x1][0x1] = ANALOG_OUTPUT_GAIN_MINUS_03_DB_E310;
	GeneratorGainIndexes[0x0][0x1] = INVALID_ANALOG_OUTPUT_GAINS_INDEX_E310;
	GeneratorGainIndexes[0x3][0x2] = ANALOG_OUTPUT_GAIN_MINUS_02_DB_E310;
	GeneratorGainIndexes[0x2][0x2] = ANALOG_OUTPUT_GAIN_MINUS_06_DB_E310;
	GeneratorGainIndexes[0x1][0x2] = ANALOG_OUTPUT_GAIN_MINUS_09_DB_E310;
	GeneratorGainIndexes[0x0][0x2] = ANALOG_OUTPUT_GAIN_MINUS_12_DB_E310;
	GeneratorGainIndexes[0x3][0x3] = ANALOG_OUTPUT_GAIN_MINUS_14_DB_E310;
	GeneratorGainIndexes[0x2][0x3] = ANALOG_OUTPUT_GAIN_MINUS_18_DB_E310;
	GeneratorGainIndexes[0x1][0x3] = ANALOG_OUTPUT_GAIN_MINUS_21_DB_E310;
	GeneratorGainIndexes[0x0][0x3] = ANALOG_OUTPUT_GAIN_MINUS_24_DB_E310;

	// массивы соотвествия индексов усиления битам
	// DDS_SIGNAL_GAIN1 и DDS_SIGNAL_GAIN1 в CONTROL_REG
	Gain1Index[ANALOG_OUTPUT_GAIN_PLUS_10_DB_E310]	= 0x3;	Gain2Index[ANALOG_OUTPUT_GAIN_PLUS_10_DB_E310]	= 0x0;
	Gain1Index[ANALOG_OUTPUT_GAIN_PLUS_06_DB_E310]	= 0x2;	Gain2Index[ANALOG_OUTPUT_GAIN_PLUS_06_DB_E310]	= 0x0;
	Gain1Index[ANALOG_OUTPUT_GAIN_PLUS_04_DB_E310]	= 0x3;	Gain2Index[ANALOG_OUTPUT_GAIN_PLUS_04_DB_E310]	= 0x1;
	Gain1Index[ANALOG_OUTPUT_GAIN_PLUS_03_DB_E310]	= 0x1;	Gain2Index[ANALOG_OUTPUT_GAIN_PLUS_03_DB_E310]	= 0x0;
	Gain1Index[ANALOG_OUTPUT_GAIN_00_DB_E310] 		= 0x0;	Gain2Index[ANALOG_OUTPUT_GAIN_00_DB_E310] 		= 0x0;
	Gain1Index[ANALOG_OUTPUT_GAIN_MINUS_02_DB_E310]	= 0x3;	Gain2Index[ANALOG_OUTPUT_GAIN_MINUS_02_DB_E310]	= 0x2;
	Gain1Index[ANALOG_OUTPUT_GAIN_MINUS_03_DB_E310]	= 0x1;	Gain2Index[ANALOG_OUTPUT_GAIN_MINUS_03_DB_E310]	= 0x1;
	Gain1Index[ANALOG_OUTPUT_GAIN_MINUS_06_DB_E310]	= 0x2;	Gain2Index[ANALOG_OUTPUT_GAIN_MINUS_06_DB_E310]	= 0x2;
	Gain1Index[ANALOG_OUTPUT_GAIN_MINUS_09_DB_E310]	= 0x1;	Gain2Index[ANALOG_OUTPUT_GAIN_MINUS_09_DB_E310]	= 0x2;
	Gain1Index[ANALOG_OUTPUT_GAIN_MINUS_12_DB_E310]	= 0x0;	Gain2Index[ANALOG_OUTPUT_GAIN_MINUS_12_DB_E310]	= 0x2;
	Gain1Index[ANALOG_OUTPUT_GAIN_MINUS_14_DB_E310]	= 0x3;	Gain2Index[ANALOG_OUTPUT_GAIN_MINUS_14_DB_E310]	= 0x3;
	Gain1Index[ANALOG_OUTPUT_GAIN_MINUS_18_DB_E310]	= 0x2;	Gain2Index[ANALOG_OUTPUT_GAIN_MINUS_18_DB_E310]	= 0x3;
	Gain1Index[ANALOG_OUTPUT_GAIN_MINUS_21_DB_E310]	= 0x1;	Gain2Index[ANALOG_OUTPUT_GAIN_MINUS_21_DB_E310]	= 0x3;
	Gain1Index[ANALOG_OUTPUT_GAIN_MINUS_24_DB_E310]	= 0x0;	Gain2Index[ANALOG_OUTPUT_GAIN_MINUS_24_DB_E310]	= 0x3;

	// заполним массив усилений выходного тракта генератора в дБ
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_PLUS_10_DB_E310]		= 10.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_PLUS_06_DB_E310]		= 6.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_PLUS_04_DB_E310]		= 4.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_PLUS_03_DB_E310]		= 3.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_00_DB_E310]			= 0.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_MINUS_02_DB_E310]	= -2.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_MINUS_03_DB_E310]	= -3.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_MINUS_06_DB_E310]	= -6.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_MINUS_09_DB_E310]	= -9.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_MINUS_12_DB_E310]	= -12.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_MINUS_14_DB_E310]	= -14.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_MINUS_18_DB_E310]	= -18.;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_MINUS_21_DB_E310]	= -21.0;
	GeneratorGaindBArray[ANALOG_OUTPUT_GAIN_MINUS_24_DB_E310]	= -24.0;

	// заполнение массива доступных амплитуд на выходе 10 и 50 Ом в дБ
	for(i = 0x0; i < ANALOG_OUTPUT_GAINS_QUANTITY_E310; i++)
	{
		Output10OhmdBArray[i] = GeneratorGaindBArray[i];
		Output50OhmdBArray[i] = GeneratorGaindBArray[i] - 6.0;
	}

	// заполнение массива доступных амплитуд на выходе 10 и 50 Ом в В
	for(i = 0x0; i < ANALOG_OUTPUT_GAINS_QUANTITY_E310; i++)
	{
		Output10OhmVArray[i] = Ampl * pow(10.0, Output10OhmdBArray[i]/20.0);
		Output50OhmVArray[i] = Ampl * pow(10.0, Output50OhmdBArray[i]/20.0);
	}

	// инициализация масcива умножителей интервалов приращения для микросхемы DDS
	DdsMultiplierArray[0x0] = 1.0;		DdsMultiplierArray[0x1] = 5.0;
	DdsMultiplierArray[0x2] = 100.0;		DdsMultiplierArray[0x3] = 500.0;

	// всё хорошо
	return TRUE;
}

// ----------------------------------------------------------------------------------
// состояние аналогового питания узлов модуля при переходе шины USB в режим suspend
// ----------------------------------------------------------------------------------
BOOL TLE310::SetSuspendModeFlag(BOOL SuspendModeFlag)
{
/*	BYTE Flag = (BYTE)(SuspendModeFlag ? 0x1 : 0x0);

	if(!PutArray(&Flag, 0x1, SUSPEND_MODE_FLAG)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else*/ return TRUE;
}

//------------------------------------------------------------------------------
// чтение дескриптора модуля
//------------------------------------------------------------------------------
BOOL TLE310::GetModuleDescriptor(void)
{
	// читаем из ППЗУ байтовый образ структуры типа MODULE_DESCRIPTOR
	if(!GetArray((BYTE *)&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR), MODULE_DESCRIPTOR_ADDRESS)) { LAST_ERROR_NUMBER(6); return FALSE; }
	// сверим с CRC считанного из ППЗУ байтового образа
	if(ModuleDescriptor.CRC16 != CalculateCrc16((BYTE *)&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR) - sizeof(WORD))) { LAST_ERROR_NUMBER(19); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// запись дескриптора модуля
//------------------------------------------------------------------------------
BOOL TLE310::SaveModuleDescriptor(void)
{
	BYTE *ptr;
	DWORD Address, MdSize;
	DWORD i = 0x0;

	// захватим ресурс критической секции
	EnterCriticalSection(&cs);
	// если нужно попробуем перевести микроконтроллер в режим 'Загрузчика' (BootLoader)
	if(IsMcuApplicationActive)
		if(!RunMcuApplication(BOOT_LOADER_START_ADDRESS)) { LAST_ERROR_NUMBER(1); LeaveCriticalSection(&cs); return FALSE; }

	// сохраняем структуру в ППЗУ
	Address = MODULE_DESCRIPTOR_ADDRESS;
	MdSize = sizeof(MODULE_DESCRIPTOR);
	ptr = (BYTE *)&ModuleDescriptor;
	while(i < MdSize)
	{
		DWORD sz = std::min((DWORD)MAX_USB_CONTROL_PIPE_BLOCK, MdSize - i);
		WORD InBuf[4] = {0x0000, 13/*Special PutArray*/, (WORD)(Address & 0xFFFF), (WORD)(Address >> 0x10) };

		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), ptr + i, sz, TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(13); return FALSE; }
		//
		Address += sz;
		i += sz;
	}

	// попробуем вернуть микроконтроллер в режим 'Приложения' (Application)
	if(!RunMcuApplication(FIRMWARE_START_ADDRESS)) { LAST_ERROR_NUMBER(2); LeaveCriticalSection(&cs); return FALSE; }
	// освободим ресурс критической секции
	LeaveCriticalSection(&cs);
	// все хорошо :)))))
	return TRUE;
}

// ----------------------------------------------------------------------------------
// чтение массива данных из памяти микроконтроллера
// ----------------------------------------------------------------------------------
BOOL TLE310::GetArray(BYTE * const Data, DWORD Size, DWORD Address)
{
	DWORD i = 0x0;

	// для начала проверим ресурсы массива данных
	if(!Data || !Size) { return FALSE; }

	// читаем массив данных из памяти микроконтроллера
	while(i < Size)
	{
		DWORD sz = std::min((DWORD)MAX_USB_CONTROL_PIPE_BLOCK, Size - i);
		WORD InBuf[4] = { 0x01, V_GET_ARRAY, (WORD)(Address & 0xFFFF), (WORD)(Address >> 0x10)};

		// захватим ресурс критической секции
		EnterCriticalSection(&cs);
		// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
		if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); LeaveCriticalSection(&cs); return FALSE; }

		// посылаем запрос в модуль
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + i, sz, TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(13); return FALSE; }

		// освободим ресурс критической секции
		LeaveCriticalSection(&cs);
		//
		Address += sz;
		i += sz;
	}
	// все хорошо :)))))
	return TRUE;
}

// ----------------------------------------------------------------------------------
// запись массива данных в память микроконтроллера
// ----------------------------------------------------------------------------------
BOOL TLE310::PutArray(BYTE * const Data, DWORD Size, DWORD Address)
{
	DWORD i = 0x0;

	// для начала проверим ресурсы массива данных
	if(!Data || !Size) { return FALSE; }

	// пишем массив данных в память микроконтроллера
	while(i < Size)
	{
		DWORD sz = std::min((DWORD)MAX_USB_CONTROL_PIPE_BLOCK, Size - i);
		WORD InBuf[4] = { 0x00, V_PUT_ARRAY, (WORD)(Address & 0xFFFF), (WORD)(Address >> 0x10) };

		// захватим ресурс критической секции
		EnterCriticalSection(&cs);

		// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
		if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); LeaveCriticalSection(&cs); return FALSE; }
		// посылаем запрос в модуль
		else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + i, sz, TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(13); return FALSE; }

		// освободим ресурс критической секции
		LeaveCriticalSection(&cs);
		//
		Address += sz;
		i += sz;
	}
	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// запуск микроконтроллера в режиме 'Приложение' (Application) или 'Загрузчика' (BootLoader)
//------------------------------------------------------------------------------
BOOL TLE310::RunMcuApplication(DWORD Address, WORD BitParam)
{
	// формируем вендор запуска приложения в микроконтроллере
	WORD InBuf[4] = { 0x00, V_CALL_APPLICATION, (WORD)(Address & 0xFFFF), (WORD)(Address >> 0x10) };

	// проверим адрес
	if(Address == (DWORD)FIRMWARE_START_ADDRESS)
	{
		InBuf[0x2] &= 0xFF00;
		InBuf[0x2] |= (WORD)(BitParam ?  0x1 : 0x0);
	}
	else if(Address == (DWORD)BOOT_LOADER_START_ADDRESS)
	{
		InBuf[0x3] = 0x0;
	}
	else { return FALSE; }

	// захватим ресурс критической секции
	EnterCriticalSection(&cs);
	// сбросим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	IsMcuApplicationActive = FALSE;
	// посылаем запрос в модуль
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, InBuf, sizeof(InBuf), NULL, 0x0, TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(13); return FALSE; }
	// мы запустили приложение в микроконтроллере?
	IsMcuApplicationActive = (Address == (DWORD)BOOT_LOADER_START_ADDRESS) ? FALSE : TRUE;
	// освободим ресурс критической секции
	LeaveCriticalSection(&cs);

	// все хорошо :)))))
	return TRUE;
}

