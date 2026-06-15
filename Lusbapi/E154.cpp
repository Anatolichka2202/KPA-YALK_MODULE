//------------------------------------------------------------------------------
#if defined(_MSC_VER)
	#define NOMINMAX	// https://demin.ws/blog/russian/2009/02/15/min-max-in-visual-studio/
#endif
#include <algorithm>
#include <stdio.h>
#include <math.h>
//
#include "e154.h"
#include "ErrorBaseIds.h"
//------------------------------------------------------------------------------

#define 	LAST_ERROR_NUMBER(ErrorNumber)	LastErrorNumber = E154_BASE_ERROR_ID + ErrorNumber

// -----------------------------------------------------------------------------
//  коструктор
// ----------------------------------------------------------------------------
TLE154::TLE154(HINSTANCE hInst) : TLUSBBASE(hInst)
{
	// проинициализируем все необходимые переменные
	InitVars();
}

// -----------------------------------------------------------------------------
//  деструктор
// -----------------------------------------------------------------------------
TLE154::~TLE154() { }



// =============================================================================
// Функции общего назначения для работы с модулем Е14-154
// =============================================================================
// -----------------------------------------------------------------------------
//  Откроем виртуальный слот для доступа к USB модулю
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::OpenLDevice (WORD VirtualSlot)
{
	char ModuleName[LONG_MODULE_NAME_STRING_LENGTH];

	// проинициализируем все необходимые переменные
	InitVars();
	// сбросим последнюю ошибку
	TLUSBBASE::ResetLastErrorInfo();
	//  попробуем открыть виртуальный слот для доступа к USB модулю
	if(!TLUSBBASE::OpenLDeviceByID(VirtualSlot, E154_ID)) { return FALSE; }
	// попробуем прочитать название модуля
	else if(!TLUSBBASE::GetModuleName(ModuleName)) { TLUSBBASE::CloseLDevice(); return FALSE; }
	// убедимся, что это модуль E-154
	else if(strcmp(ModuleName, "E154")) { TLUSBBASE::CloseLDevice(); LAST_ERROR_NUMBER(0); return FALSE; }
	// всё хорошо
	else return TRUE;
}

//------------------------------------------------------------------------------------
// освободим текущий виртуальный слот
//------------------------------------------------------------------------------------
BOOL WINAPI TLE154::CloseLDevice(void)
{
	// закроем устройство
	if(!TLUSBBASE::CloseLDevice()) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------------
// освободим указатель на устройство
//------------------------------------------------------------------------------------
BOOL WINAPI TLE154::ReleaseLInstance(void)
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
HANDLE WINAPI TLE154::GetModuleHandle(void) { return hDevice; }

//------------------------------------------------------------------------------
//  Чтение название модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE154::GetModuleName(char * const ModuleName)
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
BOOL WINAPI TLE154::GetUsbSpeed(BYTE * const UsbSpeed)
{
	// проверим указатель
	if(!UsbSpeed) { LAST_ERROR_NUMBER(1); return FALSE; }
	// теперь попробуем узнать скорость работы USB шины
	else if(!TLUSBBASE::GetUsbSpeed(UsbSpeed)) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// управления режимом низкого электропотребления модуля E-154
// но т.к. данный режим не поддерживается модулем, то всегда возвращаем ошибку
//------------------------------------------------------------------------------
BOOL WINAPI TLE154::LowPowerMode(BOOL LowPowerFlag)
{
	if(!TLUSBBASE::LowPowerMode(LowPowerFlag)) return FALSE;
	else return TRUE;
}

//------------------------------------------------------------------------------
// Функция выдачи строки с последней ошибкой
//------------------------------------------------------------------------------
BOOL WINAPI TLE154::GetLastErrorInfo(LAST_ERROR_INFO_LUSBAPI * const LastErrorInfo)
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
BOOL WINAPI TLE154::GET_ADC_PARS(ADC_PARS_E154 * const AdcPars)
{
	BYTE b, smode;
	BYTE buf[26];
	WORD i;
	DWORD FREQ;
	DWORD ScaleDivs[5]={1, 4, 16, 64, 512};

	// проверим структуру
	if(!AdcPars) { LAST_ERROR_NUMBER(1); return FALSE; }
	// зачитаем текущие параметры работы АЦП
	if(!GetArray(buf, sizeof(buf), ADC_PARS_BASE)) { LAST_ERROR_NUMBER(2); return FALSE; }
	// таблица логических каналов
	for(i = 0x0; i < MAX_CONTROL_TABLE_LENGTH_E154; i++)
	{
		b = buf[i];
		this->AdcPars.ControlTable[i] = b;
	}
	RateScale=buf[24];
	FREQ = 24000000L/ScaleDivs[RateScale];
	// частота работы АЦП(кГц)
	this->AdcPars.AdcRate = (double)FREQ/1000.0/(*(WORD*)(buf+16));
	// число активных логических каналов
	this->AdcPars.ChannelsQuantity = buf[18];
	// межкадровая задержка (мс)
	this->AdcPars.InterKadrDelay = (double)((WORD)(buf[19]|(buf[25] << 8)) + 1.0)/this->AdcPars.AdcRate;
	// частота кадров (кГц)
	this->AdcPars.KadrRate = (double)this->AdcPars.AdcRate/((WORD)buf[18] + (WORD)(buf[19]|(buf[25] << 8)));
	// тип синхронизации
	smode = (BYTE)(buf[20] & 0x0F);
	if(smode == 0x1)      	this->AdcPars.InputMode = 2;
	else if(smode == 0x2) 	this->AdcPars.InputMode = 1;
	else if(smode > 0x2) 	this->AdcPars.InputMode = 3;
	else              		this->AdcPars.InputMode = 0;
	this->AdcPars.SynchroAdType = (BYTE)((smode == 0x5 || smode == 0x6) ? 0x1 : 0x0);
	this->AdcPars.SynchroAdMode = (BYTE)((smode == 0x4 || smode == 0x6) ? 0x1 : 0x0);
	// источник тактовых импульсов
	this->AdcPars.ClkSource = (BYTE)((buf[20] >> 0x6) & 0x1);
	// флаг разрешения трансляции собственных тактовых импульсов АЦП
	this->AdcPars.EnableClkOutput = (BYTE)((buf[20] >> 0x7) & 0x1);
	// номер логического канала при аналоговой синхронизации
	b = buf[21];
	this->AdcPars.SynchroAdChannel = b;
	// порог срабатывания при аналоговой синхронизации
	this->AdcPars.SynchroAdPorog = *(SHORT*)(buf+22);
	//
	*AdcPars = this->AdcPars;
	// все хорошо :)))))
	AdcConfigStruct=*AdcPars;
	return TRUE;
}

// -----------------------------------------------------------------------------
//  заполнение требуемых параметров работы АЦП
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::SET_ADC_PARS(ADC_PARS_E154 * const AdcPars)
{
	BYTE b, smode;
	BYTE buf[26];
	DWORD i;
	DWORD FREQ;
	DWORD ScaleDivs[5]={1, 4, 16, 64, 512};
	double rate, kadr;
	unsigned int n, rate0, arm_rate, arm_kadr, cnt;


	// проверим структуру
	if(!AdcPars) { LAST_ERROR_NUMBER(1); return FALSE; }
	// проверим флажок сбора данных - во время сбора данных менять параметры АЦП нельзя
	// предварительно необходимо вызвать функцию STOP_ADC()
	else if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(3); return FALSE; }

	// обнулим буфер
	ZeroMemory(buf, sizeof(buf));
	// таблица логических каналов
	for(i = 0x0; i < MAX_CONTROL_TABLE_LENGTH_E154; i++)
	{
		b = (BYTE)AdcPars->ControlTable[i];
		buf[i] = b;
	}

	// число активных логических каналов
	if(!AdcPars->ChannelsQuantity) { LAST_ERROR_NUMBER(26); return FALSE; }
	else if(AdcPars->ChannelsQuantity > MAX_CONTROL_TABLE_LENGTH_E154) AdcPars->ChannelsQuantity = MAX_CONTROL_TABLE_LENGTH_E154;
	buf[18] = (BYTE)AdcPars->ChannelsQuantity;

	AdcPars->AdcRate = fabs(AdcPars->AdcRate);
	if(AdcPars->AdcRate > 120) AdcPars->AdcRate=120;
	else if(AdcPars->AdcRate < 0.005) AdcPars->AdcRate=0.005;
	for(RateScale=0; RateScale < 5; RateScale++)
	{
		// рзберемся с частотой АЦП
		FREQ = 24000000L/ScaleDivs[RateScale];

		// частота работы АЦП в кГц
		i = (DWORD)((double)FREQ/1000/AdcPars->AdcRate + 0.5);
		if(i > 65530) i = 65530;
			else if(i < 10) i = 10;
		rate = (double)FREQ/1000/i;
		*(WORD*)(buf+16) = (WORD)i;
		arm_rate=i;

		// межкадровая задержка (мс)
		i = (DWORD)(rate*AdcPars->InterKadrDelay + 0.5);
		if(i == 0) i = 1;
		else if(i > 65530) i = 65530;
		buf[19] = (BYTE)(i - 0x1);
		buf[25] = (BYTE)((i - 0x1)>> 8);
		kadr=i/rate;
		arm_kadr=buf[19] | (buf[25] << 8);

		rate0=arm_rate*AdcPars->ChannelsQuantity;;
		n=arm_kadr*arm_rate;
		cnt=rate0+2+n;
		if(cnt <= 65534) break;
	}

	if(RateScale >= 5)
	{
		RateScale=4;
		// рзберемся с частотой АЦП
		FREQ = 24000000L/ScaleDivs[RateScale];

		// частота работы АЦП в кГц
		i = (DWORD)((double)FREQ/1000/AdcPars->AdcRate + 0.5);
		if(i > 65530) i = 65530;
		else if(i < 10) i = 10;
		rate = (double)FREQ/1000/i;
		*(WORD*)(buf+16) = (WORD)i;
		arm_rate=i;

		// межкадровая задержка (мс)
		arm_kadr = (65534-arm_rate*AdcPars->ChannelsQuantity-2)/arm_rate;
		if(arm_kadr > 65535) arm_kadr = 0x1;

		buf[19] = (BYTE)arm_kadr;
		buf[25] = (BYTE)(arm_kadr >> 8);
		kadr = (arm_kadr+1)/AdcPars->AdcRate;
	}

	AdcPars->AdcRate = rate;
	AdcPars->InterKadrDelay = kadr;

	// частота кадров (кГц)
	AdcPars->KadrRate = (double)AdcPars->AdcRate/((WORD)buf[18] + (WORD)(buf[19] | (buf[25] << 8)));

	 // тип синхронизации
	if(AdcPars->InputMode > ANALOG_SYNC_E154) AdcPars->InputMode = ANALOG_SYNC_E154;
	smode = (BYTE)AdcPars->InputMode;
	if(smode == 1) smode=2;
	else if(smode == 2) smode = (BYTE)(0x1);
	else if(smode == 3)
	{
		if(AdcPars->SynchroAdType) smode += (BYTE)(0x2);
		if(AdcPars->SynchroAdMode) smode += (BYTE)(0x1);
	}
	buf[20] = smode;
	// источник тактовых импульсов
	if(AdcPars->ClkSource) buf[20] |= (0x1 << 0x6);
	// флаг разрешения трансляции собственных тактовых импульсов АЦП
	if(AdcPars->EnableClkOutput) buf[20] |= (0x1 << 0x7);
	// номер логического канала при аналоговой синхронизации
	b = (BYTE)AdcPars->SynchroAdChannel;
	buf[21] = b;
	// порог срабатывания при аналоговой синхронизации
	*(SHORT*)(buf+22) = AdcPars->SynchroAdPorog;
	buf[24] = (BYTE)RateScale;
	// запомним текущие параметры работы АЦП
	this->AdcPars=*AdcPars;
	// передаём параметры АЦП
	if(!PutArray(buf, sizeof(buf), ADC_PARS_BASE)) { LAST_ERROR_NUMBER(4); return FALSE; }
	else AdcConfigStruct = *AdcPars;
	// все хорошо :)))))
	return TRUE;
}

// -----------------------------------------------------------------------------
// Преобразование кодов АЦП в вольты с учетом калибровки
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::ProcessArray(SHORT *src, double *dest, DWORD size, BOOL calibr, BOOL volt)
{
	double K_offset[MAX_CONTROL_TABLE_LENGTH_E154], K_scale[MAX_CONTROL_TABLE_LENGTH_E154];;
	DWORD i, j, nch, gain_index;
	MODULE_DESCRIPTION_E154 md;
	double ScaleGain[4]={5.0/2000., 1.6/2000., 0.5/2000., 0.16/2000.};

	nch=AdcConfigStruct.ChannelsQuantity;
	if(!nch) { LAST_ERROR_NUMBER(5); return FALSE; }

	if(calibr == TRUE && !DescriptorReadFlag)
		{
		// если не были считаны калибровочные коэффициенты, то считаем их
		if(TLE154::GET_MODULE_DESCRIPTION(&md) == FALSE) { LAST_ERROR_NUMBER(5); return FALSE; }
		}

	// подготовим коэффициенты
	for(i=0; i < nch; i++)
		{
		gain_index=(AdcConfigStruct.ControlTable[i] >> 6) & 3;
		if(calibr == FALSE)
			{
			// без калибровки
			if(volt == FALSE)
				{
				// коды АЦП, без калибровки
				K_offset[i]=0;
				K_scale[i]=1.0;
				}
			  else
				{
				// вольты, без калибровки
				K_offset[i]=0;
				K_scale[i]=ScaleGain[gain_index];
				}
			}
		  else
			{
			// с калибровкой
			if(volt == FALSE)
				{
				// коды АЦП, с калибровкой
				K_offset[i]=E154DescriptionStruct.Adc.OffsetCalibration[gain_index];
				K_scale[i]=E154DescriptionStruct.Adc.ScaleCalibration[gain_index];
				}
			  else
				{
				// вольты, с калибровкой
				K_offset[i]=E154DescriptionStruct.Adc.OffsetCalibration[gain_index];
				K_scale[i]=E154DescriptionStruct.Adc.ScaleCalibration[gain_index]*ScaleGain[gain_index];
				}
			}
		}

	for(i=j=0; i < size; i++)
		{
		*dest++=((*src++)+K_offset[j])*K_scale[j];
		j++;
		if(j >= nch) j=0;
		}

	// все хорошо :)))))
	return TRUE;
}

// -----------------------------------------------------------------------------
// Преобразование одного кода АЦП в вольты с учетом калибровки
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::ProcessOnePoint(SHORT source, double *dest, DWORD channel, BOOL calibr, BOOL volt)
{
	double K_offset, K_scale;
	int gain_index;
	MODULE_DESCRIPTION_E154 md;
	double ScaleGain[4]={5.0/2000., 1.6/2000., 0.5/2000., 0.16/2000.};

	if(calibr == TRUE && !DescriptorReadFlag)
	{
		// если не были считаны калибровочные коэффициенты, то считаем их
		if(TLE154::GET_MODULE_DESCRIPTION(&md) == FALSE) { LAST_ERROR_NUMBER(5); return FALSE; }
	}

	// подготовим коэффициенты
	gain_index=(channel >> 6) & 3;
	if(calibr == FALSE)
	{
		// без калибровки
		if(volt == FALSE)
				{
				// коды АЦП, без калибровки
				K_offset=0;
				K_scale=1.0;
				}
			  else
				{
				// вольты, без калибровки
				K_offset=0;
				K_scale=ScaleGain[gain_index];
				}
			}
		  else
			{
			// с калибровкой
			if(volt == FALSE)
				{
				// коды АЦП, с калибровкой
				K_offset=E154DescriptionStruct.Adc.OffsetCalibration[gain_index];
				K_scale=E154DescriptionStruct.Adc.ScaleCalibration[gain_index];
				}
			  else
				{
				// вольты, с калибровкой
				K_offset=E154DescriptionStruct.Adc.OffsetCalibration[gain_index];
				K_scale=E154DescriptionStruct.Adc.ScaleCalibration[gain_index]*ScaleGain[gain_index];
				}
			}
	 //  }

	*dest=(source+K_offset)*K_scale;

	// все хорошо :)))))
	return TRUE;
}


// -----------------------------------------------------------------------------
//  однократный ввод с заданного логического канала АЦП модуля
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::FIFO_STATUS(DWORD *FifoOverflowFlag, double *FifoMaxPercentLoad, DWORD *FifoSize, DWORD *MaxFifoBytesUsed)
{
	BYTE temp[5];

	if(!GetArray((BYTE*)temp, 0x5, FIFO_STATUS_ADDRESS)) { LAST_ERROR_NUMBER(2); return FALSE; }

	*FifoOverflowFlag=temp[0];
	*FifoSize=temp[3] | (temp[4] << 8);
	*MaxFifoBytesUsed=temp[1] | (temp[2] << 8);

	*FifoMaxPercentLoad=((double)(*MaxFifoBytesUsed))*100./((double)(*FifoSize));

	// всё хорошо
	return TRUE;
}


// -----------------------------------------------------------------------------
// запуск работы АЦП
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::START_ADC(void)
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
BOOL WINAPI TLE154::STOP_ADC(void)
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
BOOL WINAPI TLE154::ADC_KADR(SHORT * const Data)
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
	else if(AdcPars.ChannelsQuantity > 16) { LAST_ERROR_NUMBER(6); return FALSE; }
	// проверим флажок сбора данных - во время сбора данных нельзя выполнять данную фукнцию
	// предварительно необходимо вызвать функцию STOP_ADC()
	else if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(3); return FALSE; }
	// небходимо выполнить функцию STOP_ADC()
	else if(!STOP_ADC()) { LAST_ERROR_NUMBER(25); return FALSE; }
	// сбрасываем принимающий канал
	// отсылаем запрос на однократный запуск АЦП
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(5); return FALSE; }

	// формируем структуру запроса типа IO_REQUEST_LUSBAPI на ввод данных
	PointsToRead = 32;
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
	CopyMemory(Data, &IoReq.Buffer[0], AdcPars.ChannelsQuantity*sizeof(SHORT));
	// освобождаем память
	if(IoReq.Buffer) delete [] IoReq.Buffer;
	// всё хорошо
	return TRUE;
}

// -----------------------------------------------------------------------------
//  однократный ввод с заданного логического канала АЦП модуля
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::ADC_SAMPLE(SHORT * const Data, WORD Channel)
{
	WORD b = (WORD)Channel ;

	// проверим флажок сбора данных - во время сбора данных нельзя выполнять данную фукнцию
	// предварительно необходимо вызвать функцию STOP_ADC()
	if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(3); return FALSE; }
	// физический номер канала
	else if(!PutArray((BYTE*)&b, 0x2, ADC_CHANNEL_SELECT_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// читаем осчёт
	else if(!GetArray((BYTE*)Data, 0x2, ADC_DATA_REGISTER)) { LAST_ERROR_NUMBER(2); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
//  потоковое чтение данных с АЦП модуля
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::ReadData(IO_REQUEST_LUSBAPI * const ReadRequest)
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
// -----------------------------------------------------------------------------
//  однократный вывод на заданный канал ЦАП
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::DAC_SAMPLE(SHORT * const DacData, WORD DacChannel)
{
	SHORT DacParam;

	// подготовим значение для вывода на ЦАП
	if((*DacData) < -128) *DacData = -128;
	else if((*DacData) > 127) *DacData = 127;
	// формируем соотвестсвующее слово для передачи в AVR
	DacParam = (SHORT)((*DacData));
	// передаём в модуль
	if(!PutArray((BYTE*)&DacParam, 0x2, DAC_DATA_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
//  однократный вывод на заданный канал ЦАП
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::DAC_SAMPLE_VOLT(double  const DoubleDacData, BOOL calibr)
{
	double K_offset, K_scale, f;
	MODULE_DESCRIPTION_E154 md;
	double ScaleGain=128./5.4;
	SHORT  DacData;

	if(calibr == TRUE && !DescriptorReadFlag)
		{
		// если не были считаны калибровочные коэффициенты, то считаем их
		if(TLE154::GET_MODULE_DESCRIPTION(&md) == FALSE) { LAST_ERROR_NUMBER(5); return FALSE; }
		}

	// подготовим коэффициенты
		if(calibr == FALSE)
			{
			// вольты, без калибровки
			f = DoubleDacData*ScaleGain;
			}
		  else
			{
			// вольты, с калибровкой
			K_offset = E154DescriptionStruct.Dac.OffsetCalibration[0];
			K_scale = E154DescriptionStruct.Dac.ScaleCalibration[0];
			f = DoubleDacData*128./5.0;
			f = (f + K_offset) * K_scale;
			}

	DacData = (SHORT)((f > 0) ? f + 0.5 : f - 0.5);

	// подготовим значение для вывода на ЦАП
	if(DacData < -128) DacData = -128;
	else if(DacData > 127) DacData = 127;
	// передаём в модуль
	if(!PutArray((BYTE*)&DacData, 0x2, DAC_DATA_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}


// =============================================================================
//  функции для работы с ТТЛ линиями на внешнем цифровом разъёме
// =============================================================================
// -----------------------------------------------------------------------------
//  функция разрешения выходных линий внешнего цифрового разъёма
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::ENABLE_TTL_OUT(BOOL EnableTtlOut)
{
	BYTE Flag = (BYTE)(EnableTtlOut ? 0x1 : 0x0);

	// пишем флаг доступности цифровых выходов
	if(!PutArray(&Flag, 0x1, DOUT_ENABLE_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
//   функция чтения входных линии внешнего цифрового разъёма
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::TTL_IN(WORD * const TtlIn)
{
	// читаем состояние цифровых входов
	if(!GetArray((BYTE *)TtlIn, 0x2, DIN_REGISTER)) { LAST_ERROR_NUMBER(2); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
//  функция вывода на выходные линии внешнего цифрового разъёма
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::TTL_OUT(WORD TtlOut)
{
	// пишем состояние цифровых выходов
	if(!PutArray((BYTE*)&TtlOut, 0x2, DOUT_REGISTER)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}




// =============================================================================
//  функции для работы с пользовательской информацией ППЗУ
// =============================================================================
// -----------------------------------------------------------------------------
// разрешение/запрещение режима записи в пользовательскую область ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled)
{
	BYTE Flag = (BYTE)(IsUserFlashWriteEnabled ? 0x1 : 0x0);

	// передадим признак разрешения/запрещения записи в ППЗУ
	if(!PutArray(&Flag, 0x1, ENABLE_FLASH_WRITE_FLAG)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// запомним признак
	TLE154::IsUserFlashWriteEnabled = IsUserFlashWriteEnabled;
	// всё хорошо
	return TRUE;
}

// -----------------------------------------------------------------------------
// чтенние слова из пользовательской области ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::READ_FLASH_ARRAY(BYTE * const UserFlash)
{
	// пытаемся прочитать слово из ППЗУ
	if(!GetArray((BYTE*)UserFlash, 128, (WORD)(DATA_FLASH_BASE))) { LAST_ERROR_NUMBER(2); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
// запись слова в пользовательскую область ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::WRITE_FLASH_ARRAY(BYTE * const UserFlash)
{
	// проверим флажок сбора данных - во время сбора данных нельзя ничего записывать в пользовательское ППЗУ
	// предварительно необходимо вызвать функцию STOP_ADC()
	if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(3); return FALSE; }
	// запись должна быть разрешена
	else if(!IsUserFlashWriteEnabled) { LAST_ERROR_NUMBER(18); return FALSE; }
	else if(!PutArray((BYTE*)UserFlash, 128, (WORD)(DATA_FLASH_BASE ))) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}





// =============================================================================
//  функции для работы со служебной информацией ППЗУ
// =============================================================================
// -----------------------------------------------------------------------------
//  получим служебную информацию о модуле из ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE154::GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E154 * const md)
{
	char *pTag;
	BYTE Buffer[128];
	BYTE Str[17];
	WORD i;
	WORD error;

	error=0;
	// проверим структуру
	if(!md) { LAST_ERROR_NUMBER(1); return FALSE; }
	// считываем упакованный дескриптор модуля из ППЗУ
	else if(!GetArray(Buffer, sizeof(Buffer), DESCRIPTOR_BASE)) { LAST_ERROR_NUMBER(2); return FALSE; }
	// распаковываем дескриптор модуля
	else if(!UnpackModuleDescriptor(Buffer, &ModuleDescriptor)) { LAST_ERROR_NUMBER(19); error=1; }

	// Module Info
	strncpy((char *)md->Module.CompanyName,  			"L-Card Ltd.",    								sizeof(md->Module.CompanyName));
	if(!strcmp((char *)ModuleDescriptor.Name, 		"E-154")) strcpy((char *)md->Module.DeviceName, "E-154");
	else strncpy((char *)md->Module.DeviceName,		(char *)ModuleDescriptor.Name,	 		std::min(sizeof(md->Module.DeviceName), sizeof(ModuleDescriptor.Name)));
	strncpy((char *)md->Module.SerialNumber, 			(char *)ModuleDescriptor.SerialNumber,	std::min(sizeof(md->Module.SerialNumber), sizeof(ModuleDescriptor.SerialNumber)));
	strcpy((char *)md->Module.Comment,      			"Внешний модуль АЦП/ЦАП/ТТЛ общего назначения на шину USB 1.1");
	// получим ревизию модуля
	md->Module.Revision = ModuleDescriptor.Revision;
	// модуль E14-140 существует в единственном исполнении (по крайне мере пока)
	md->Module.Modification = NO_MODULE_MODIFICATION_LUSBAPI;

	// Interface Info
	md->Interface.Active = TRUE;
	strcpy((char *)md->Interface.Name, "USB 1.1");
	strcpy((char *)md->Interface.Comment, "Full-Speed Mode (12 Mbit/s)");

	// MCU Info
	if(!GetArray(Buffer, 0x10, LBIOS_VERSION)) { LAST_ERROR_NUMBER(2); return FALSE; }
	md->Mcu.Active = TRUE;
	strcpy((char *)md->Mcu.Name, "AT91SAM7S64");
	sprintf((char *)Str, "%1u.%1u", Buffer[0], Buffer[1]);
	strncpy((char *)md->Mcu.Version.Version,  		(char *)Str, 		std::min(sizeof(md->Mcu.Version.Version), sizeof(Str)));
	strncpy((char *)md->Mcu.Version.Date,  			(char *)"", 14);
	strcpy((char *)md->Mcu.Version.Manufacturer,  	"L-Card Ltd.");
	strcpy((char *)md->Mcu.Version.Author,  			"Syachinov");
	strncpy((char *)md->Mcu.Version.Comment,			"Штатная прошивка для модуля E-154", sizeof(md->Mcu.Version.Comment));
	strncpy((char *)md->Mcu.Comment, 					"32-bit Microcontroller with 64K Bytes In-System Programmable Flash", sizeof(md->Mcu.Comment));
	md->Mcu.ClockRate = 24000;		// в кГц
	// определимся с номером версии Firmware микроконтроллера
	FirmwareVersionNumber = strtod((char *)md->Mcu.Version.Version, &pTag);

	// ADC Info
	md->Adc.Active = TRUE;
	strcpy((char *)md->Adc.Name, "AD7895");
	// смещение при усилении i
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E154; i++)
	{
		md->Adc.OffsetCalibration[i] = ModuleDescriptor.AdcOffsetCoefs[i];
		md->Adc.ScaleCalibration[i] = ModuleDescriptor.AdcScaleCoefs[i];
	}
	strcpy((char *)md->Adc.Comment, "12-Bit 120 Ksps ADC converter");

	// DAC Info
	if(ModuleDescriptor.IsDacPresented)
	{
		md->Dac.Active = TRUE;
		strcpy((char *)md->Dac.Name, "PWM");
		for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E154; i++)
		{
			md->Dac.OffsetCalibration[i] = ModuleDescriptor.DacOffsetCoefs[i];
			md->Dac.ScaleCalibration[i] = ModuleDescriptor.DacScaleCoefs[i];
		}
		strcpy((char *)md->Dac.Comment, "8-Bit DAC converter");
	}
	else md->Dac.Active = FALSE;

	// IO Info
	md->DigitalIo.Active = TRUE;
	strcpy((char *)md->DigitalIo.Name, "74HC165/74HC574");
	md->DigitalIo.InLinesQuantity = 8;
	md->DigitalIo.OutLinesQuantity = 8;
	strcpy((char *)md->DigitalIo.Comment, "TTL/CMOS compatible");

	// всё хорошо
	if(!error)
		{
		DescriptorReadFlag=TRUE;
		E154DescriptionStruct=*md;
		}

	return (!error) ? TRUE : FALSE;
}
// -----------------------------------------------------------------------------
//  запишем служебную информацию о модуле в ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI WINAPI TLE154::SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E154 * const md)
{
	BYTE Buffer[128];
	WORD i;

	// проверим флажок сбора данных - во время сбора данных нельзя выполнять данную фукнцию
	// предварительно необходимо вызвать функцию STOP_ADC()
	if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(3); return FALSE; }

	DescriptorReadFlag=FALSE;
	strcpy((char *)ModuleDescriptor.Name, 				"E-154");
	strncpy((char *)ModuleDescriptor.SerialNumber,	(char *)md->Module.SerialNumber, std::min(sizeof(md->Module.SerialNumber), sizeof(ModuleDescriptor.SerialNumber)));

	// проверим требуемую ревизию модуля
	if((	md->Module.Revision > REVISIONS_E154[REVISIONS_QUANTITY_E154 - 0x1] &&
			md->Module.Revision <= 'Z')) { LAST_ERROR_NUMBER(23); return FALSE; }
	ModuleDescriptor.Revision = md->Module.Revision;

	ModuleDescriptor.IsDacPresented = (BYTE)0x1;
	ModuleDescriptor.QuartzFrequency = (long)(1000.0 * md->Mcu.ClockRate + 0.5);		// в Гц

	// корректировочные коэффициетны АЦП
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E154; i++)
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
	for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E154; i++)
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

	// проверим структуру
	if(!md) { LAST_ERROR_NUMBER(1); return FALSE; }
	// упаковываем дескриптор модуля
	else if(!PackModuleDescriptor(Buffer, &ModuleDescriptor)) { LAST_ERROR_NUMBER(20); return FALSE; }
	// разрешим процедуру записи в ППЗУ
	if(!ENABLE_FLASH_WRITE(TRUE)) { LAST_ERROR_NUMBER(21); return FALSE; }
	// пробуем записать дескриптор в ППЗУ
	else if(!PutArray(Buffer, sizeof(Buffer), DESCRIPTOR_BASE)) { LAST_ERROR_NUMBER(4); ENABLE_FLASH_WRITE(FALSE); return FALSE; }
	// запретим процедуру записи в ППЗУ
	else if(!ENABLE_FLASH_WRITE(FALSE)) { LAST_ERROR_NUMBER(21); return FALSE; }
	// обновляем поля сруктуры
	else if(!UnpackModuleDescriptor(Buffer, &ModuleDescriptor)) { LAST_ERROR_NUMBER(19); return FALSE; }
	// всё хорошо
	else
		{
		return TRUE;
		}
}





// =============================================================================
//  внутренние функции для работы с модулем
// =============================================================================
//------------------------------------------------------------------------------
// инициализация всех необходимых переменных
//------------------------------------------------------------------------------
BOOL TLE154::InitVars(void)
{
	// инициализация всех локальных структур класса
	ZeroMemory(&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR));
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
BOOL TLE154::SetSuspendModeFlag(BOOL SuspendModeFlag)
{
	BYTE Flag = (BYTE)(SuspendModeFlag ? 0x1 : 0x0);

	if(!PutArray(&Flag, 0x1, SUSPEND_MODE_FLAG)) { LAST_ERROR_NUMBER(4); return FALSE; }
	// всё хорошо
	else return TRUE;
}

// -----------------------------------------------------------------------------
// чтение массива данных из памяти микроконтроллера
// -----------------------------------------------------------------------------
BOOL WINAPI WINAPI TLE154::GetArray(BYTE * const Buffer, WORD Size, WORD Address)
{
	DWORD i;
	WORD InBuf[4] = { 0x0001, V_GET_ARRAY, Address, 0x0 };

	// проверим буфер и размер
	if(!Buffer || !Size) { LAST_ERROR_NUMBER(1); return FALSE; }
	// читаем данные
	for(i = 0x0; i < (DWORD)Size; i += MAX_USB_CONTROL_PIPE_BLOCK)
	{
		InBuf[0x2] = (WORD)(Address + i);
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND,
									 &InBuf, sizeof(InBuf),
									 Buffer + i, std::min( Size - i, (DWORD)MAX_USB_CONTROL_PIPE_BLOCK),
									 TimeOut))  { LAST_ERROR_NUMBER(5); return FALSE; }
	}
	// все хорошо :)))))
	return TRUE;
}

// -----------------------------------------------------------------------------
// запись массива данных в память микроконтроллера
// -----------------------------------------------------------------------------
BOOL WINAPI WINAPI TLE154::PutArray(BYTE * const Buffer, WORD Size, WORD Address)
{
	DWORD i;
	WORD InBuf[4] = { 0x0000, V_PUT_ARRAY, Address, 0x0 };

	// проверим буфер и размер
	if(!Buffer || !Size) { LAST_ERROR_NUMBER(1); return FALSE; }
	// пишем данные
	for(i = 0x0; i < (DWORD)Size; i += MAX_USB_CONTROL_PIPE_BLOCK)
	{
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND,
									 &InBuf, sizeof(InBuf),
									 Buffer + i, std::min(Size - i, (DWORD)MAX_USB_CONTROL_PIPE_BLOCK),
									 TimeOut)) { LAST_ERROR_NUMBER(5); return FALSE; }
	}
	// все хорошо :)))))
	return TRUE;
}

// -----------------------------------------------------------------------------
//  подсчет кс8
// -----------------------------------------------------------------------------
#define CRC8INIT         0x55
BYTE TLE154::CRC8ADD(BYTE a, BYTE b)
{
	return (BYTE)(a + b);
}
//
BYTE TLE154::CRC8CALC(BYTE *Buffer, WORD Size)
{
	BYTE crc = CRC8INIT;
	while(Size--) crc = CRC8ADD(crc, *Buffer++);
	return crc;
}

// -----------------------------------------------------------------------------
//  упаковка дескриптора модуля
// -----------------------------------------------------------------------------
BOOL TLE154::PackModuleDescriptor(BYTE * const Buffer, MODULE_DESCRIPTOR * const ModuleDescriptor)
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
	*(DWORD*)(Buffer+27) = ModuleDescriptor->QuartzFrequency;
	// флаг присутствия ЦАП
	Buffer[32] = (BYTE)(ModuleDescriptor->IsDacPresented ? 0x1 : 0x0);
	// упаковка коэффициентов коррекции АЦП
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E154; i++)
	{
		*(float*)(Buffer + 33 + (i + 0)*sizeof(float)) = (float)ModuleDescriptor->AdcOffsetCoefs[i];
		*(float*)(Buffer + 33 + (i + ADC_CALIBR_COEFS_QUANTITY_E154)*sizeof(float)) = (float)ModuleDescriptor->AdcScaleCoefs[i];
	}
	// упаковка коэффициентов коррекции ЦАП
	for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E154; i++)
	{
		*(float*)(Buffer + 65 + (i + 0)*sizeof(float)) = (float)ModuleDescriptor->DacOffsetCoefs[i];
		*(float*)(Buffer + 65 + (i + DAC_CALIBR_COEFS_QUANTITY_E154)*sizeof(float)) = (float)ModuleDescriptor->DacScaleCoefs[i];
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
BOOL TLE154::UnpackModuleDescriptor(BYTE * const Buffer, MODULE_DESCRIPTOR * const ModuleDescriptor)
{
	WORD i;
	WORD error;

	error=0;
	// проверим контрольную сумму
	if(CRC8CALC(Buffer+0, 31)  != Buffer[31])
		{
	// серийный номер изделия
	sprintf((char *)(ModuleDescriptor->SerialNumber), "%lu%c%lu", (DWORD)0, 'L', (DWORD)0);
	// название изделия
	sprintf((char *)(ModuleDescriptor->Name), "%.10s", "E-154");
	// ревизия изделия
	ModuleDescriptor->Revision = 0;
	// тип установленного процессора
	sprintf((char *)(ModuleDescriptor->CpuType), "%.10s", "AT91SAM7S64");
	// частота работы процессора
	ModuleDescriptor->QuartzFrequency = 24000;
	// флаг присутствия ЦАП на борту модуля
	ModuleDescriptor->IsDacPresented =  true;

		LAST_ERROR_NUMBER(24);
		error=1;
		}

	if(CRC8CALC(Buffer+32, 95) != Buffer[127])
		{
		for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E154; i++)
			{
		ModuleDescriptor->AdcOffsetCoefs[i]	= *(float*)(Buffer + 33 + (i + 0)*sizeof(float));
		ModuleDescriptor->AdcScaleCoefs[i]	= *(float*)(Buffer + 33 + (i + ADC_CALIBR_COEFS_QUANTITY_E154)*sizeof(float));
			}

		for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E154; i++)
			{
		ModuleDescriptor->DacOffsetCoefs[i]	= (*(float*)(Buffer + 65 + (i + 0)*sizeof(float)));
		ModuleDescriptor->DacScaleCoefs[i]	= (*(float*)(Buffer + 65 + (i + DAC_CALIBR_COEFS_QUANTITY_E154)*sizeof(float)));
			}
		LAST_ERROR_NUMBER(24);
		error=1;
		}

	if(error) return FALSE;

	// серийный номер изделия
	sprintf((char *)(ModuleDescriptor->SerialNumber), "%lu%c%lu", (DWORD)Buffer[0], Buffer[1], (*(DWORD*)(Buffer+2)) & 0xFFFFFFL);
	// название изделия
	sprintf((char *)(ModuleDescriptor->Name), "%.10s", Buffer+6);
	// ревизия изделия
	ModuleDescriptor->Revision = Buffer[16];
	// тип установленного процессора
	sprintf((char *)(ModuleDescriptor->CpuType), "%.10s", Buffer+17);
	// частота работы процессора
	ModuleDescriptor->QuartzFrequency = *(DWORD*)(Buffer+27);
	// флаг присутствия ЦАП на борту модуля
	ModuleDescriptor->IsDacPresented = Buffer[32] ? true : false;
	// распаковка коэффициентов коррекции АЦП
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E154; i++)
	{
		ModuleDescriptor->AdcOffsetCoefs[i]	= *(float*)(Buffer + 33 + (i + 0)*sizeof(float));
		ModuleDescriptor->AdcScaleCoefs[i]	= *(float*)(Buffer + 33 + (i + ADC_CALIBR_COEFS_QUANTITY_E154)*sizeof(float));
	}
	// распаковка коэффициентов коррекции ЦАП
	for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E154; i++)
	{
		ModuleDescriptor->DacOffsetCoefs[i]	= (*(float*)(Buffer + 65 + (i + 0)*sizeof(float)));
		ModuleDescriptor->DacScaleCoefs[i]	= (*(float*)(Buffer + 65 + (i + DAC_CALIBR_COEFS_QUANTITY_E154)*sizeof(float)));
	}
	// всё хорошо
	return TRUE;
}

