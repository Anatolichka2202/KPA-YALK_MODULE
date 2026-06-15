//------------------------------------------------------------------------------
#if defined(_MSC_VER)
	#define NOMINMAX	// https://demin.ws/blog/russian/2009/02/15/min-max-in-visual-studio/
#endif
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <algorithm>
#include <math.h>
//
#include "E2010.h"
#include "ErrorBaseIds.h"
//------------------------------------------------------------------------------

static const WORD InputRangeBitMap [0x8] = {  3,4, 1,2, 15,8, 13, 14 };
static const WORD InputSwitchBitMap[0x8] = { 10,0, 9,0, 12,0, 11  ,0 };

#define 	LAST_ERROR_NUMBER(ErrorNumber)	LastErrorNumber = E2010_BASE_ERROR_ID + ErrorNumber

//******************************************************************************
// Реализация интерфейса для модуля E20-10
//******************************************************************************
//------------------------------------------------------------------------------
// конструктор
//------------------------------------------------------------------------------
TLE2010::TLE2010(HINSTANCE hInst) : TLUSBBASE(hInst)
{
	// проинициализируем все необходимые переменные
	InitVars();
}

//------------------------------------------------------------------------------
// деструктор
//------------------------------------------------------------------------------
TLE2010::~TLE2010() { }



//==============================================================================
// Функции общего назначения для работы с модулем Е20-10
//==============================================================================
//-----------------------------------------------------------------------------
// функция отрытия виртального слота
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::OpenLDevice(WORD VirtualSlot)
{
	char ModuleName[LONG_MODULE_NAME_STRING_LENGTH];

	// проинициализируем все необходимые переменные
	InitVars();
	// сбросим последнюю ошибку
	TLUSBBASE::ResetLastErrorInfo();
	// попробуем открыть виртуальный слот для доступа к USB модулю
	if(!TLUSBBASE::OpenLDeviceByID(VirtualSlot, E2010_ID)) { return FALSE; }
	// попробуем прочитать название модуля
	else if(!TLUSBBASE::GetModuleName(ModuleName)) { TLUSBBASE::CloseLDevice(); return FALSE; }
	// убедимся, что это модуль E20-10
	else if(strcmp(ModuleName, "E20-10")) { TLUSBBASE::CloseLDevice(); LAST_ERROR_NUMBER(0); return FALSE; }
	// попробуем запустить микроконтроллер в режиме 'Загрузчика' (BootLoader)
	else if(!RunMcuApplication(BOOT_LOADER_START_ADDRESS)) { TLUSBBASE::CloseLDevice(); LAST_ERROR_NUMBER(1); return FALSE; }
	// а теперь запустим микроконтроллер в режиме 'Приложения' (Application)
	else if(!RunMcuApplication(FIRMWARE_START_ADDRESS, REINIT_SWITCH_PARAM)) { TLUSBBASE::CloseLDevice(); LAST_ERROR_NUMBER(2); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
//  Освободим текущий виртуальный слот
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::CloseLDevice(void)
{
	// закроем устройство
	if(!TLUSBBASE::CloseLDevice()) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
//  Освободим указатель на интерфейс модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::ReleaseLInstance(void)
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
//  Возвращается дескриптор устройства USB
//------------------------------------------------------------------------------
HANDLE WINAPI TLE2010::GetModuleHandle(void) { return TLUSBBASE::GetModuleHandle(); }

//------------------------------------------------------------------------------
//  Чтение название модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::GetModuleName(char * const ModuleName)
{
	// проверим буфер
	if(!ModuleName) { LAST_ERROR_NUMBER(3); return FALSE; }
	// теперь попробуем узнать название модуля
	else if(!TLUSBBASE::GetModuleNameByLength(ModuleName, SHORT_MODULE_NAME_STRING_LENGTH)) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
//  Получение текущей скорости работы шины USB
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::GetUsbSpeed(BYTE * const UsbSpeed)
{
	// проверим указатель
	if(!UsbSpeed) { LAST_ERROR_NUMBER(3); return FALSE; }
	// теперь попробуем узнать скорость работы USB шины
	else if(!TLUSBBASE::GetUsbSpeed(UsbSpeed)) return FALSE;
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// управления режимом низкого электропотребления модуля E20-10
// но т.к. данный режим не поддерживается модулем, то всегда возвращаем ошибку
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::LowPowerMode(BOOL LowPowerFlag)
{
	if(!TLUSBBASE::LowPowerMode(LowPowerFlag)) return FALSE;
   else return TRUE;
}

//------------------------------------------------------------------------------
// Функция выдачи строки с последней ошибкой
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::GetLastErrorInfo(LAST_ERROR_INFO_LUSBAPI * const LastErrorInfo)
{
	BOOL Status;

	// вычитываем название ошибки
	Status = TLUSBBASE::GetLastErrorInfo(LastErrorInfo);
	// если можно - сбросим последнюю ошибку
	if(Status) TLUSBBASE::ResetLastErrorInfo();
   // озвращаем статус выполнения функции
	return Status;
}






//------------------------------------------------------------------------------
// заливка микросхемы ПЛИС из ресусра библиотеки или файла
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::LOAD_MODULE(const char * const FileName)
{
	WORD i;
	DWORD	NBytes;
	// указатель на массив с кодом для ПЛИС
	BYTE *PldCodeBuffer;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// осуществим сброс микросхемы ПЛИС
//	else if(!ResetPld()) { LAST_ERROR_NUMBER(5); return FALSE; }

	// пробуем прочитать дескриптор модуля
	if(!GetModuleDescriptor(&ModuleDescriptor)) return FALSE;
	// проверим реальность Ревизии модуля
	for(i = 0x0; i< REVISIONS_QUANTITY_E2010; i++)
		if(ModuleDescriptor.Revision == REVISIONS_E2010[i]) break;
	if(i == REVISIONS_QUANTITY_E2010) { LAST_ERROR_NUMBER(35); return FALSE; }

	// для ревизии 'A'
	if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
	{
		// попробуем выделим буфер под образ ПЛИС
		PldCodeBuffer = new BYTE[EP1K10_SIZE];
		if(!PldCodeBuffer) { LAST_ERROR_NUMBER(6); return FALSE; }
		// обнулим массив PldCodeBuffer
		ZeroMemory(PldCodeBuffer, sizeof(EP1K10_SIZE));
	}

	// определимся откуда будем брать код для заливки в ПЛИС
	if(!FileName)
	{
		HRSRC RsRes;

		// найдем ресурс с кодом LBIOS для модуля ревизии 'A'
		if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
			RsRes = FindResource(hInstance, (LPCSTR)"E2010PLD", RT_RCDATA);
		// найдем ресурс с кодом LBIOS для модуля ревизии 'B'
		else if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_B_E2010])
		{
			BYTE tg;
			// номер версии Firmware микроконтроллера
			double FirmwareVersionNumber;
			// структура с информацией о драйвере микроконтроллера
			FIRMWARE_DESCRIPTOR fd;

			// cчитаем информацию о Firmware микроконтроллера
			if(!GetArray(FIRMWARE_DESCRIPTOR_ADDRESS, (BYTE*)&fd, sizeof(FIRMWARE_DESCRIPTOR))) { LAST_ERROR_NUMBER(16); return FALSE; }

			// сформируем номер версии Firmware микроконтроллера
			FirmwareVersionNumber = atof((char *)fd.Version);
			// если можно - попробуем получить температурный класс устройства
			if(2.35 < FirmwareVersionNumber)
			{
				// попробуем вычитать текущий температурный класс из устройства
				if(!GetArray(SEL_MODULE_TG_DATA, &tg, 0x1)) { LAST_ERROR_NUMBER(16); return FALSE; }
				// температурный класс устройства
				tg = (tg & 0x1) ? (BYTE)INDUSTRIAL_TEMPERATURE_GRADE_E2010 : (BYTE)COMMERCIAL_TEMPERATURE_GRADE_E2010;
			}
			else tg = COMMERCIAL_TEMPERATURE_GRADE_E2010;

			// попробуем получить требуемый ресурс
			if(tg == COMMERCIAL_TEMPERATURE_GRADE_E2010)
				RsRes = FindResource(hInstance, "E2010MPLD", RT_RCDATA);
			else if(tg == INDUSTRIAL_TEMPERATURE_GRADE_E2010)
				RsRes = FindResource(hInstance, "E2010MIPLD", RT_RCDATA);
		}
		else { LAST_ERROR_NUMBER(35); return FALSE; }

		// проверим полученный ресурс
		if(!RsRes) { LAST_ERROR_NUMBER(7); return FALSE; }
		// загрузим его
		HGLOBAL RcResHandle = LoadResource(hInstance, RsRes);
		if(!RcResHandle) { LAST_ERROR_NUMBER(8); return FALSE; }
		// зафиксируем ресурс
		char  *pRcData = (char *)LockResource(RcResHandle);
		if(!pRcData) { LAST_ERROR_NUMBER(9); return FALSE; }
		// определим размер ресурса
		NBytes = SizeofResource(hInstance, RsRes);
		// для ревизии 'A'
		if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
		{
			if(NBytes != EP1K10_SIZE) { LAST_ERROR_NUMBER(10); return FALSE; }
		}
		else
		{
			if(!NBytes) { LAST_ERROR_NUMBER(36); return FALSE; }

			// попробуем выделим буфер под образ ПЛИС
			PldCodeBuffer = new BYTE[NBytes];
			if(!PldCodeBuffer) { LAST_ERROR_NUMBER(6); return FALSE; }
		}

		// обнулим массив PldCodeBuffer
		ZeroMemory(PldCodeBuffer, NBytes);
		// скопируем данные ресурса
		for(DWORD i = 0x0; i < NBytes; i++) PldCodeBuffer[i] = pRcData[i];
	}
	else
	{
		int PldFile;

		// откроем файл с образом ПЛИС
		PldFile = open(FileName, O_BINARY | O_RDONLY);
		if(PldFile == -1) { LAST_ERROR_NUMBER(11); return FALSE; }

		// определим размер файла с образом ПЛИС
		NBytes = filelength(PldFile);
		if((long)NBytes == -1) { LAST_ERROR_NUMBER(12); return FALSE; }

		// для ревизии 'A'
		if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
			{ if(NBytes != EP1K10_SIZE) { LAST_ERROR_NUMBER(12); return FALSE; } }
		else
		{
			// попробуем выделим буфер под образ ПЛИС
			PldCodeBuffer = new BYTE[NBytes];
			if(!PldCodeBuffer) { LAST_ERROR_NUMBER(6); return FALSE; }
		}
		// обнулим массив PldCodeBuffer
		ZeroMemory(PldCodeBuffer, NBytes);

		// зачитаем содержимое файла в буфер
		if(read(PldFile, PldCodeBuffer, NBytes) == -1) { LAST_ERROR_NUMBER(13); return FALSE; }

		// закроем файл
		if(close(PldFile) == -1) { LAST_ERROR_NUMBER(14); return FALSE; }
	}
	// пробуем залить ПЛИС
	if(!LoadPld(PldCodeBuffer, NBytes)) { LAST_ERROR_NUMBER(15); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// проверка заливки микросхемы ПЛИС
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::TEST_MODULE(WORD TestModeMask)
{
	// проверим название модуля
	if(strcmp((char *)ModuleDescriptor.ModuleName, "E20-10"))
	{
		// пробуем прочитать дескриптор модуля
		if(!GetModuleDescriptor(&ModuleDescriptor)) return FALSE;
		// и снова проверим название модуля
		else if(strcmp((char *)ModuleDescriptor.ModuleName, "E20-10")) { LAST_ERROR_NUMBER(37); return FALSE; }
	}

	// проверку можно делать для модуля ревизии 'B' и выше
	if((REVISIONS_E2010[REVISION_A_E2010] < ModuleDescriptor.Revision) &&
		(ModuleDescriptor.Revision <= REVISIONS_E2010[REVISIONS_QUANTITY_E2010 - 0x1]))
	{
		// формируем вендор запуска ПЛИС
		WORD InBuf[4] = { 0x0, V_TEST_PLD, 0x0, 0x0 };

		// посылаем запрос в модуль
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(19); return FALSE; }

		// если нужно - пробуем перевести модуль в соотвествующие тестовые режимы
		if(!SetTestMode(TestModeMask)) return FALSE;
	}
	// все хорошо :)))))
	return TRUE;
}



//==============================================================================
// функции для работы с АЦП модуля
//==============================================================================
//------------------------------------------------------------------------------
// чтение параметров работы АЦП
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::GET_ADC_PARS(ADC_PARS_E2010 * const AdcPars)
{
	WORD i, j, k;
	BYTE bParam;
	WORD wParam;

	// обнулим массив ParamArray
	ZeroMemory(&AdcParamsArray, sizeof(AdcParamsArray));

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру
	else if(!AdcPars) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим реально используемый размер массива AdcParamsArray
	else if(!AdcParamSize) { LAST_ERROR_NUMBER(777); return FALSE; }
	// проверим флажок сбора данных - во время сбора данных менять параметры АЦП нельзя
	else if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(17); return FALSE; }

	// проверим реальность Ревизии модуля
	for(i = 0x0; i< REVISIONS_QUANTITY_E2010; i++)
		if(ModuleDescriptor.Revision == REVISIONS_E2010[i]) break;
	if(i == REVISIONS_QUANTITY_E2010) { LAST_ERROR_NUMBER(35); return FALSE; }

	// теперь можно считать текущие параметры работы АЦП
	if(!GetArray(SEL_ADC_PARAM, AdcParamsArray, AdcParamSize)) { LAST_ERROR_NUMBER(16); return FALSE; }
	// проверим возможность использования расширенной управляющей таблицы
	if(IsExtendedControlTableEnabled)
	{
		if(!GetArray(SEL_EXT_ADC_PARAM, &bParam, 0x1)) { LAST_ERROR_NUMBER(16); return FALSE; }
	}
	else bParam = 0x0;

	// *** определим режим работы АЦП ***
	// параметры синхронизации ввода данных
	if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
		GetSynchroParsForRevA(AdcParamsArray[0], AdcPars);
	else
	{
		// основные параметры синхронизации сбора данных
		GetSynchroParsForRevBandAbove(AdcParamsArray[0], AdcPars);
		// дополнительные параметры синхронизации сбора данных
		if(!GetExtraSynchroParsForRevBandAbove(AdcPars)) return FALSE;
	}

	// длина управляющей таблицы
	AdcPars->ChannelsQuantity = (WORD)(((WORD)(bParam << 0x8) | AdcParamsArray[6]) + 0x1);
	// управляющая таблица с логическими номерами каналов
	for(i = 0x0; i < AdcPars->ChannelsQuantity; i++) AdcPars->ControlTable[i]=AdcParamsArray[7+i];
	// ADC Rate в кГц
	AdcPars->AdcRate = 30000.0/(AdcParamsArray[1] + 1.0);
	// ADC InterFadrDelay в мс
	AdcPars->InterKadrDelay = 	(*(WORD*)(AdcParamsArray+2) + 1.0)/AdcPars->AdcRate;
	// частота кадра
	AdcPars->KadrRate = 1.0/((AdcPars->ChannelsQuantity - 1.0)/AdcPars->AdcRate + AdcPars->InterKadrDelay);
	// диапазоны входных напряжений для каналов АЦП
	wParam = *(WORD*)(AdcParamsArray+4);
	for(i = j = 0x0; i < ADC_CHANNELS_QUANTITY_E2010; i++)
	{
		j = (WORD)(((wParam >> InputRangeBitMap[2*i+0]) & 0x1) << 0x0);
		j |= (WORD)(((wParam >> InputRangeBitMap[2*i+1]) & 0x1) << 0x1);
		AdcPars->InputRange[i] = j;
	}
	// типы входов для каналов АЦП
	for(i = j = 0x0; i < ADC_CHANNELS_QUANTITY_E2010; i++)
	{
		j = (WORD)(((wParam >> InputSwitchBitMap[2*i+0]) & 0x1) << 0x0);
//		j |= ((wParam >> InputSwitchBitMap[2*i+1]) & 0x1) << 0x1;
		AdcPars->InputSwitch[i] = j;
	}
	// управление входным током смещения для ревизии 'B' и выше
	if(ModuleDescriptor.Revision != REVISIONS_E2010[REVISION_A_E2010])
	{
		if(wParam & (0x1 << 0x5)) AdcPars->InputCurrentControl = TRUE;
		else  AdcPars->InputCurrentControl = FALSE;
	}
	else AdcPars->InputCurrentControl = TRUE;

	// ************* КОРРЕКТИРОВОЧНЫЕ КОЭФФИЦИЕНТЫ АЦП **************************
	// проверка возможности управления автоматической корректировкой данных с АЦП
	if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010]) AdcPars->IsAdcCorrectionEnabled = FALSE;
	else
	{
		// считаем текущий признак возможности управления автоматической корректировкой данных с АЦП
		if(!GetArray(ADC_CORRECTION_ADDRESS, &bParam, 0x1)) { LAST_ERROR_NUMBER(18); return FALSE; }
		AdcPars->IsAdcCorrectionEnabled = bParam ? TRUE : FALSE;

		// теперь зачитаем текущие корректировочные коэффиценты АЦП из микроконтроллера
		if(!GetArray(SEL_ADC_CALIBR_KOEFS, (BYTE *)AdcCalibrCoefsArray, 2*ADC_CALIBR_COEFS_QUANTITY)) { LAST_ERROR_NUMBER(16); return FALSE; }
		// разберёмся с корректировочными коэффициентами АЦП
		for(i = k = 0x0; i < ADC_INPUT_RANGES_QUANTITY_E2010; i++)
			for(j = 0x0; j < ADC_CHANNELS_QUANTITY_E2010; j++)
			{
				// корректировка смещения
				AdcPars->AdcOffsetCoefs[i][j] = (SHORT)AdcCalibrCoefsArray[k++];
				// корректировка масштаба
				AdcPars->AdcScaleCoefs[i][j] = (double)AdcCalibrCoefsArray[k++]/(double)0x8000;
			}
	}
	// **************************************************************************

	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// установка параметров работы АЦП
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::SET_ADC_PARS(ADC_PARS_E2010 * const AdcPars)
{
	WORD i, j, k;
	BYTE bParam;
	WORD wParam;
	DWORD dwParam;

	// обнулим массив AdcParamsArray
	ZeroMemory(&AdcParamsArray, sizeof(AdcParamsArray));

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру
	else if(!AdcPars) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим флажок сбора данных - во время сбора данных менять параметры АЦП нельзя
	else if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(17); return FALSE; }
	// проверим дескриптор модуля
	else if(strcmp((char *)ModuleDescriptor.ModuleName, "E20-10")) { LAST_ERROR_NUMBER(37); return FALSE; }

	// проверим реальность Ревизии модуля
	for(i = 0x0; i< REVISIONS_QUANTITY_E2010; i++)
		if(ModuleDescriptor.Revision == REVISIONS_E2010[i]) break;
	if(i == REVISIONS_QUANTITY_E2010) { LAST_ERROR_NUMBER(35); return FALSE; }

	// длина управляющей таблицы не может быть равнв нулю
	if(!AdcPars->ChannelsQuantity) { LAST_ERROR_NUMBER(34); return FALSE; }
	// проверка на максимальную длину управляющей таблицы
	else if(AdcPars->ChannelsQuantity > MAX_CONTROL_TABLE_LENGTH_E2010)
					AdcPars->ChannelsQuantity = MAX_CONTROL_TABLE_LENGTH_E2010;
	AdcParamsArray[6] = (BYTE)(AdcPars->ChannelsQuantity - 0x1);
	// управляющая таблица с логическими номерами каналов
	for(i = 0x0; i < AdcPars->ChannelsQuantity; i++)
			AdcParamsArray[7+i] = (BYTE)AdcPars->ControlTable[i];

	// задаём типы тактирования ввода данных для разных ревизий модуля
	if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
	{
		// обычные параметры сбора данных
		MakeSynchroParsForRevA(AdcParamsArray[0], AdcPars);
		// обнулим дополнительные параметры сбора данных
		AdcPars->SynchroPars.StartDelay						= 0x0;
		AdcPars->SynchroPars.StopAfterNKadrs				= 0x0;
		AdcPars->SynchroPars.SynchroAdMode					= NO_ANALOG_SYNCHRO_E2010;
		AdcPars->SynchroPars.SynchroAdChannel				= 0x0;
		AdcPars->SynchroPars.SynchroAdPorog					= 0x0;
		AdcPars->SynchroPars.IsBlockDataMarkerEnabled	= 0x0;
		// для внешней синхронизации старта необходима прошивка Firmware MCU не ниже 1.8
		if(AdcPars->SynchroPars.StartSource > INT_ADC_START_WITH_TRANS_E2010)
			{ if(FirmwareVersionNumber < 1.75) { LAST_ERROR_NUMBER(31); return FALSE; } }
	}
	else
	{
		// обычные параметры сбора данных
		MakeSynchroParsForRevBandAbove(AdcParamsArray[0], AdcPars);
		// дополнительные параметры сбора данных
		if(!SetExtraSynchroParsForRevBandAbove(AdcPars)) return FALSE;
	}

	// частота работы АЦП в кГц
	AdcPars->AdcRate = fabs(AdcPars->AdcRate);
	if(AdcPars->AdcRate > 10000.0) AdcPars->AdcRate = 10000.0;
	else if(AdcPars->AdcRate < 1000.0) AdcPars->AdcRate = 1000.0;
	AdcParamsArray[1] = (BYTE)(30000.0/AdcPars->AdcRate - 0.5);
	AdcPars->AdcRate = 30000.0/(AdcParamsArray[1] + 1.0);
	// величина межкадровой задержки
	AdcPars->InterKadrDelay = fabs(AdcPars->InterKadrDelay);
	if((1.0/(AdcPars->AdcRate)) > (AdcPars->InterKadrDelay)) AdcPars->InterKadrDelay = 1.0/AdcPars->AdcRate;
	dwParam = (DWORD)(AdcPars->InterKadrDelay*AdcPars->AdcRate - 0.5);
	// проверка макс. межкадровой задержки
	if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
		{ if(dwParam > 255) dwParam = 255; }
	else
		{ if(dwParam > 65535) dwParam = 65535; }
	*(WORD*)(AdcParamsArray+2) = (WORD)(dwParam & 0xFFFF);
	// окончательная величина межкадровой задержки
	AdcPars->InterKadrDelay = (dwParam + 1.0)/AdcPars->AdcRate;
	// частота кадра отчётов
	AdcPars->KadrRate = 1.0/((AdcPars->ChannelsQuantity - 1.0)/AdcPars->AdcRate + AdcPars->InterKadrDelay);
	// диапазоны входных напряжений для каналов АЦП
	for(i = wParam = 0x0; i < ADC_CHANNELS_QUANTITY_E2010; i++)
	{
		wParam |= (WORD)(((AdcPars->InputRange[i] >> 0x0) & 0x1) << InputRangeBitMap[2*i + 0x0]);
		wParam |= (WORD)(((AdcPars->InputRange[i] >> 0x1) & 0x1) << InputRangeBitMap[2*i + 0x1]);
	}
	// типы входов для каналов АЦП
	for(i = 0x0; i < ADC_CHANNELS_QUANTITY_E2010; i++)
	{
		wParam |= (WORD)(((AdcPars->InputSwitch[i] >> 0x0) & 0x1) << InputSwitchBitMap[2*i + 0x0]);
//		wParam|=((AdcPars->InputSwitch[i]>>1)&1)<<InputSwitchBitMap[2*i+1];
	}
	// управление входным током смещения для ревизии 'B' и выше
	if(ModuleDescriptor.Revision != REVISIONS_E2010[REVISION_A_E2010])
	{
		if(AdcPars->InputCurrentControl) wParam |= 0x1 << 0x5;
	}
	//
	*(WORD*)(AdcParamsArray+4) = wParam;

	// теперь передадим в микроконтроллер соответствующие параметры АЦП
	if(!PutArray(SEL_ADC_PARAM, AdcParamsArray, AdcParamSize)) { LAST_ERROR_NUMBER(18); return FALSE; }

	// если нужно - дополнительно передаем старший байт кол-ва каналов в управляющей таблице
	if(IsExtendedControlTableEnabled)
	{
		bParam = (BYTE)(((AdcPars->ChannelsQuantity - 0x1) >> 0x8) & 0x001F);
		if(!PutArray(SEL_EXT_ADC_PARAM, &bParam, 0x1)) { LAST_ERROR_NUMBER(18); return FALSE; }
	}

	// ************* КОРРЕКТИРОВОЧНЫЕ КОЭФФИЦИЕНТЫ АЦП **************************
	// проверка возможности управления автоматической корректировкой данных с АЦП
	if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010]) AdcPars->IsAdcCorrectionEnabled = FALSE;
	else
	{
		// установим признак возможности управления автоматической корректировкой данных с АЦП
		bParam = (BYTE)(AdcPars->IsAdcCorrectionEnabled ? 0x1 : 0x0);
		if(!PutArray(ADC_CORRECTION_ADDRESS, &bParam, 0x1)) { LAST_ERROR_NUMBER(18); return FALSE; }

		// разберёмся с корректировочными коэффициентами АЦП
		for(i = k = 0x0; i < ADC_INPUT_RANGES_QUANTITY_E2010; i++)
			for(j = 0x0; j < ADC_CHANNELS_QUANTITY_E2010; j++)
			{
				if(AdcPars->IsAdcCorrectionEnabled)
				{
					// корректировка смещения
					if(AdcPars->AdcOffsetCoefs[i][j] < 0.0) AdcCalibrCoefsArray[k++] = (WORD)(AdcPars->AdcOffsetCoefs[i][j] - 0.5);
					else AdcCalibrCoefsArray[k++] = (WORD)(AdcPars->AdcOffsetCoefs[i][j] + 0.5);
					// корректировка масштаба
					if((AdcPars->AdcScaleCoefs[i][j] < 0.1) || (AdcPars->AdcScaleCoefs[i][j] > 1.9))  { LAST_ERROR_NUMBER(38); return FALSE; }
					AdcCalibrCoefsArray[k++] = (WORD)(AdcPars->AdcScaleCoefs[i][j]*(double)0x8000 + 0.5);
				}
				else
				{
					// умолчателльная корректировка смещения
					AdcCalibrCoefsArray[k++] = 0x0;
					// умолчателльная корректировка масштаба
					AdcCalibrCoefsArray[k++] = 0x8000;
				}
			}
		// теперь передадим корректировочные коэффиценты АЦП в микроконтроллер модуля
		if(!PutArray(SEL_ADC_CALIBR_KOEFS, (BYTE *)AdcCalibrCoefsArray, 2*ADC_CALIBR_COEFS_QUANTITY)) { LAST_ERROR_NUMBER(18); return FALSE; }
	}
	// **************************************************************************

	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// запуск сбора данных с АЦП
//-----------------------------------------------------------------------------
BOOL WINAPI TLE2010::START_ADC(void)
{
	WORD InBuf[4] = { 0x0, V_START_ADC, 0x0, 0x1};

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// посылаем запрос в модуль
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(19); return FALSE; }
	// установми флажок сбора данных
	IsDataAcquisitionInProgress = TRUE;
	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// останов сбора данных с АЦП
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::STOP_ADC(void)
{
	WORD InBuf[4] = { 0x0, V_STOP_ADC, 0x0, 0x1};

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// посылаем запрос в модуль
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(19); return FALSE; }
	// сбросим все отложенные запросы на ввод данных
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_ABORT_PIPE3/*abort Read Pipe*/, NULL, 0, NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(39); return FALSE; }
	// дважды зачистим канал ввода данных
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE3/*reset Read Pipe */, NULL, 0, NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(40); return FALSE; }
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE3/*reset Read Pipe */, NULL, 0, NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(40); return FALSE; }
	// сбросим флажок сбора данных
	IsDataAcquisitionInProgress = FALSE;

	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// проверка статуса собираемых данных, в том числе состояния внутреннего буфера модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::GET_DATA_STATE(DATA_STATE_E2010 * const DataState)
{
	WORD InBuf[4] = { 0x01, V_GET_ARRAY, (WORD)(DATA_STATE_ADDRESS & 0xFFFF), (WORD)(DATA_STATE_ADDRESS >> 0x10)};

	// проверим указатель на структуру
	if(!DataState) { LAST_ERROR_NUMBER(3); return FALSE; }
	// обнулим структуру
	ZeroMemory(DataState, sizeof(DATA_STATE_E2010));

	// для ревизии 'A'
	if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
	{
		// определимся с номером версии Firmware микроконтроллера
		if(FirmwareVersionNumber < 1.65) { LAST_ERROR_NUMBER(31); return FALSE; }

		// захватим ресурс критической секции
		EnterCriticalSection(&cs);
		// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
		if(!IsMcuApplicationActive) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(4); return FALSE; }
		// посылаем запрос в модуль на считывание текущего признака целостности собираемых данных
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), &LocDataStatus.Status, sizeof(LocDataStatus.Status), TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(19); return FALSE; }
		// освободим ресурс критической секции
		LeaveCriticalSection(&cs);
		// теперь можно разобраться что почём :)
		DataState->BufferOverrun = (BYTE)(LocDataStatus.Status & 0x1);
	}
	else
	{
		// захватим ресурс критической секции
		EnterCriticalSection(&cs);
		// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
		if(!IsMcuApplicationActive) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(4); return FALSE; }
		// посылаем запрос в модуль на считывание текущего признака целостности собираемых данных
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), &LocDataStatus, sizeof(LocDataStatus), TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(19); return FALSE; }
		// освободим ресурс критической секции
		LeaveCriticalSection(&cs);

		// теперь можно разобраться что почём :)
		DataState->ChannelsOverFlow = (BYTE)(((LocDataStatus.Status & 0x08) << 0x4) |
														 ((LocDataStatus.Status & 0xF0) >> 0x4));
		DataState->BufferOverrun = (BYTE)(LocDataStatus.Status & 0x1);
		DataState->CurBufferFilling = LocDataStatus.CurBufferFilling >> 0x1;
		DataState->MaxOfBufferFilling = LocDataStatus.MaxOfBufferFilling >> 0x1;
		DataState->BufferSize = LocDataStatus.BufferSize >> 0x1;
		DataState->CurBufferFillingPercent = 100.0*LocDataStatus.CurBufferFilling/LocDataStatus.BufferSize;
		DataState->MaxOfBufferFillingPercent = 100.0*LocDataStatus.MaxOfBufferFilling/LocDataStatus.BufferSize;
	}

	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// потоковое чтение данных из модуля
//   если ReadRequest->Overlapped != NULL - асинхронный запрос на сбор необходимого кол-ва данных
//   если ReadRequest->Overlapped == NULL - синхронный сбор необходимого кол-ва данных
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::ReadData(IO_REQUEST_LUSBAPI * const ReadRequest)
{
	DWORD NumberOfBytesRead;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим буфер данных
	else if(!ReadRequest->Buffer) { LAST_ERROR_NUMBER(3); return FALSE; }

	// захватим ресурс критической секции
	EnterCriticalSection(&cs);
	//
	if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
	{
		// число отсчетов должно находиться в диапазоне от 0x100(256)
		if(ReadRequest->NumberOfWordsToPass < 0x100) ReadRequest->NumberOfWordsToPass = 0x100;
		// и вплоть до 0x100000(1024*1024)
		else if(ReadRequest->NumberOfWordsToPass > (1024*1024)) ReadRequest->NumberOfWordsToPass = 1024*1024;
		// также число отсчетов должно быть кратно 0x100(256)
		ReadRequest->NumberOfWordsToPass -= ReadRequest->NumberOfWordsToPass%256;
	}
	else
	{
		DWORD BytesToPass;
		WORD InBuf[4] = { 0x00, V_PUT_ARRAY, 0x0, (WORD)(SEL_BULK_REQ_SIZE >> 0x10) };

		// число отсчетов должно находиться в диапазоне от 0x1
		if(ReadRequest->NumberOfWordsToPass < 0x1) ReadRequest->NumberOfWordsToPass = 0x1;
		// и вплоть до 0x100000(1024*1024)
		else if(ReadRequest->NumberOfWordsToPass > (1024*1024)) ReadRequest->NumberOfWordsToPass = 1024*1024;
		// запомним кол-во передаваемых байт (размер запроса)
		BytesToPass = 2*ReadRequest->NumberOfWordsToPass;
		// передаём в модуль размер очередного запроса на вывод данных
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), &BytesToPass, sizeof(DWORD), TimeOut)) { LAST_ERROR_NUMBER(19); LeaveCriticalSection(&cs); return FALSE; }
	}
	//
	NumberOfBytesRead = 0x0;

	// выполняем асинхронный запрос
	if(ReadRequest->Overlapped)
	{
		// посылаем асинхронный запрос на сбор необходимого кол-ва данных
		if(!ReadFile(hDevice, ReadRequest->Buffer, 2*ReadRequest->NumberOfWordsToPass, &NumberOfBytesRead, ReadRequest->Overlapped))
			{ if(GetLastError() != ERROR_IO_PENDING) { LAST_ERROR_NUMBER(21); LeaveCriticalSection(&cs); CancelIo(hDevice); return FALSE; } }
		// освободим ресурс критической секции
		LeaveCriticalSection(&cs);
		// кол-во полученных отсчётов (в силу асинхронности запроса эта величина вполне может быть равна 0)
		ReadRequest->NumberOfWordsPassed = NumberOfBytesRead/2;
	}
	// выполняем синхронный запрос
	else
	{
		OVERLAPPED Ov;

		// инициализируем OVERLAPPED структуру
		ZeroMemory(&Ov, sizeof(OVERLAPPED));
		Ov.hEvent = CreateEvent(NULL, FALSE , FALSE, NULL);
		if(!Ov.hEvent) { LAST_ERROR_NUMBER(20); LeaveCriticalSection(&cs); return FALSE; }
		// посылаем асинхронный запрос на сбор необходимого кол-ва данных
		if(!ReadFile(hDevice, ReadRequest->Buffer, 2*ReadRequest->NumberOfWordsToPass, &NumberOfBytesRead, &Ov))
			{ if(GetLastError() != ERROR_IO_PENDING) { LAST_ERROR_NUMBER(21); LeaveCriticalSection(&cs); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; } }
		// освободим ресурс критической секции
		LeaveCriticalSection(&cs);
		// ждём окончания выполнения запроса
		if(WaitForSingleObject(Ov.hEvent, ReadRequest->TimeOut) == WAIT_TIMEOUT) { LAST_ERROR_NUMBER(22); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; }
		// попробуем получить кол-во реально переданных байт данных
		else if(!GetOverlappedResult(hDevice, &Ov, &NumberOfBytesRead, TRUE)) { LAST_ERROR_NUMBER(23); CancelIo(hDevice); CloseHandle(Ov.hEvent); return FALSE; }
		// закроем событие
		else if(!CloseHandle(Ov.hEvent)) { LAST_ERROR_NUMBER(24); return FALSE; }
		// кол-во полученных отсчётов
		ReadRequest->NumberOfWordsPassed = NumberOfBytesRead/2;
		// сравним сколько было реально полученно данных (в словах) с запрашиваемым кол-вом отсчётов
		if(ReadRequest->NumberOfWordsToPass != ReadRequest->NumberOfWordsPassed) { LAST_ERROR_NUMBER(25); return FALSE; }
	}
	// все хорошо :)))))
	return TRUE;
}




//==============================================================================
// функции для работы с ЦАП модуля
//==============================================================================
//------------------------------------------------------------------------------
// однократный вывод отсчёта на ЦАП
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::DAC_SAMPLE(SHORT * const DacData, WORD DacChannel)
{
	SHORT Param;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// а есть ли ЦАП на данном экземпляре модуля
	else if(ModuleDescriptor.IsDacPresented == DAC_INACCESSIBLED_E440)  { LAST_ERROR_NUMBER(26); return FALSE; }
	// проверим указатель
	else if(!DacData) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим номер канала ЦАП
	else if(DacChannel >= DAC_CHANNELS_QUANTITY_E2010) { LAST_ERROR_NUMBER(27); return FALSE; }
	// подготовим значение для вывода на ЦАП
	else if((*DacData) < -2048) *DacData = -2048;
	else if((*DacData) > 2047) *DacData = 2047;
	// формируем соотвестсвующий параметр для микроконтроллера
	Param = (SHORT)(((*DacData) & 0xFFF) | (WORD)(DacChannel << 12));
	Param = (SHORT)(((Param << 0x8) & 0xFF00) | ((Param >> 0x8) & 0x00FF));
	// передадим соответствующие данные в микроконтроллер
	if(!PutArray(SEL_DAC_DATA, (BYTE*)&Param, 2)) { LAST_ERROR_NUMBER(18); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}




//==============================================================================
// функции для работы с цифровыми линиями на внешнем цифровом разъёме
//==============================================================================
//------------------------------------------------------------------------------
// функция разрешения выходных линий внешнего цифрового разъёма
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::ENABLE_TTL_OUT(BOOL EnableTtlOut)
{
	BYTE b = (BYTE)(EnableTtlOut ? 0x1 : 0x0);

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// передадим  соответствующие данные в микроконтроллер
	else if(!PutArray(SEL_DIO_PARAM, &b, 0x1)) { LAST_ERROR_NUMBER(18); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// функция чтения входных линии внешнего цифрового разъёма
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::TTL_IN(WORD * const TtlIn)
{
	SHORT TtlParam;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверяем переменную
	else if(!TtlIn) { LAST_ERROR_NUMBER(3); return FALSE; }
	// читаем соответствующие данные из микроконтроллера
	else if(!GetArray(SEL_DIO_DATA, (BYTE*)&TtlParam, 0x2)) { LAST_ERROR_NUMBER(16); return FALSE; }
	// формируем слово состояния цифровых входных линий
	*TtlIn = (WORD)(((TtlParam << 0x8) & 0xFF00) | ((TtlParam >> 0x8) & 0x00FF));
	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// функция вывода на выходные линии внешнего цифрового разъёма
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::TTL_OUT(WORD TtlOut)
{
	SHORT TtlParam;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// формируем слово состояния цифровых выходных линий
	TtlParam = (WORD)(((TtlOut << 0x8) & 0xFF00) | ((TtlOut >> 0x8) & 0x00FF));
	// передадим соответствующие данные в микроконтроллер
	if(!PutArray(SEL_DIO_DATA, (BYTE*)&TtlParam, 0x2)) { LAST_ERROR_NUMBER(18); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}




// =============================================================================
//  функции для работы с пользовательской информацией ППЗУ
// =============================================================================
// -----------------------------------------------------------------------------
// разрешение/запрещение режима записи в пользовательское ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE2010::ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled)
{
	// запомним признак
	TLE2010::IsUserFlashWriteEnabled = IsUserFlashWriteEnabled;
	// все хорошо :)))))
	return TRUE;
}

// -----------------------------------------------------------------------------
// чтенние массива из пользовательского ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE2010::READ_FLASH_ARRAY(USER_FLASH_E2010 * const UserFlash)
{
	// проверим структуру
	if(!UserFlash) { LAST_ERROR_NUMBER(3); return FALSE; }
	// тупо читаем массив
	else if(!GetArray(USER_FLASH_ADDRESS, (BYTE *)UserFlash, sizeof(USER_FLASH_E2010))) { LAST_ERROR_NUMBER(16); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

// -----------------------------------------------------------------------------
// запись массива в пользовательскуе ППЗУ
// -----------------------------------------------------------------------------
BOOL WINAPI TLE2010::WRITE_FLASH_ARRAY(USER_FLASH_E2010 * const UserFlash)
{
	BYTE *ptr;
	DWORD Address, Size;
	DWORD i = 0x0;

	// проверим флажок сбора данных - во время сбора данных нельзя
	// ничего записывать в пользовательское ППЗУ
	if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(17); return FALSE; }
	// для начала проверим разрешение на запись
	else if(!IsUserFlashWriteEnabled) { LAST_ERROR_NUMBER(28); return FALSE; }
	// теперь проверим структуру
	else if(!UserFlash) { LAST_ERROR_NUMBER(3); return FALSE; }

	// захватим ресурс критической секции
	EnterCriticalSection(&cs);
	// если нужно попробуем перевести микроконтроллер в режим 'Загрузчика' (BootLoader)
	if(IsMcuApplicationActive)
		if(!RunMcuApplication(BOOT_LOADER_START_ADDRESS)) { LAST_ERROR_NUMBER(1); LeaveCriticalSection(&cs); return FALSE; }

	Address = USER_FLASH_ADDRESS;
	Size = sizeof(USER_FLASH_E2010);
	ptr = (BYTE *)UserFlash;
	while(i < Size)
	{
		DWORD sz = std::min((DWORD)MAX_USB_CONTROL_PIPE_BLOCK, Size - i);
		WORD InBuf[4] = {0x0000, 13/*Special PutArray*/, (WORD)(Address & 0xFFFF), (WORD)(Address >> 0x10) };

		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), ptr + i, sz, TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(19); return FALSE; }
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




//==============================================================================
// функции для работы с информацией о модуле E20-10
//==============================================================================
//------------------------------------------------------------------------------
// чтение информации о модуле
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::GET_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E2010 * const md)
{
	BYTE PldTemperatureGrade;
	WORD i;
	char *pTag, *p1, *p2;
	MODIFICATION_E2010	Modification;
	PLD_VERSION				PldVersion;

	// проверим флажок работы микроконтроллера в режиме 'Приложение' (Application)
	if(!IsMcuApplicationActive) { LAST_ERROR_NUMBER(4); return FALSE; }
	// проверим указатель на структуру
	else if(!md) { LAST_ERROR_NUMBER(3); return FALSE; }

	// пробуем прочитать дескриптор модуля
	if(!GetModuleDescriptor(&ModuleDescriptor)) return FALSE;

	// читаем остальные дескрипторы
	if(!GetArray(FIRMWARE_DESCRIPTOR_ADDRESS, (BYTE*)&FirmwareDescriptor, sizeof(FIRMWARE_DESCRIPTOR))) { LAST_ERROR_NUMBER(16); return FALSE; }
	else if(!GetArray(BOOT_LOADER_DESCRIPTOR_ADDRESS, (BYTE*)&BootLoaderDescriptor, sizeof(BOOT_LOADER_DESCRIPTOR))) { LAST_ERROR_NUMBER(16); return FALSE; }

	// обнулим всю структуру MODULE_DESCRIPTION_E2010
	ZeroMemory(md, sizeof(MODULE_DESCRIPTION_E2010));

	// Module Info
	strncpy((char *)md->Module.CompanyName,  "L-Card Ltd.",    sizeof(md->Module.CompanyName));
	strncpy((char *)md->Module.DeviceName,   (char *)ModuleDescriptor.ModuleName,    std::min(sizeof(md->Module.DeviceName), sizeof(ModuleDescriptor.ModuleName)));
	strncpy((char *)md->Module.SerialNumber, (char *)ModuleDescriptor.SerialNumber,  std::min(sizeof(md->Module.SerialNumber), sizeof(ModuleDescriptor.SerialNumber)));
	strcpy((char *)md->Module.Comment,      			"Внешний быстродействующий USB модуль АЦП/ЦАП/ТТЛ общего назначения на шину USB 2.0");
	md->Module.Revision = ModuleDescriptor.Revision;

	// Interface Info
	md->Interface.Active = TRUE;
	strcpy((char *)md->Interface.Name, "USB 2.0");
	strcpy((char *)md->Interface.Comment, "Full-Speed (12 Mbit/s) and High-Speed(480 Mbit/s) Modes");

	// MCU Info
	md->Mcu.Active = TRUE;
	strncpy((char *)md->Mcu.Name, 									(char *)FirmwareDescriptor.McuName,  			std::min(sizeof(md->Mcu.Name), sizeof(FirmwareDescriptor.McuName)));
	strncpy((char *)md->Mcu.Version.FwVersion.Version,  		(char *)FirmwareDescriptor.Version, 			std::min(sizeof(md->Mcu.Version.FwVersion.Version), sizeof(FirmwareDescriptor.Version)));
	strncpy((char *)md->Mcu.Version.FwVersion.Date,  			(char *)FirmwareDescriptor.Created, 			std::min(sizeof(md->Mcu.Version.FwVersion.Date), sizeof(FirmwareDescriptor.Created)));
	strncpy((char *)md->Mcu.Version.FwVersion.Manufacturer,  (char *)FirmwareDescriptor.Manufacturer, 		std::min(sizeof(md->Mcu.Version.FwVersion.Manufacturer), sizeof(FirmwareDescriptor.Manufacturer)));
	strncpy((char *)md->Mcu.Version.FwVersion.Author,  		(char *)FirmwareDescriptor.Author, 				std::min(sizeof(md->Mcu.Version.FwVersion.Author), sizeof(FirmwareDescriptor.Author)));
	strncpy((char *)md->Mcu.Version.FwVersion.Comment,			(char *)FirmwareDescriptor.Comment, 			std::min(sizeof(md->Mcu.Version.FwVersion.Comment), sizeof(FirmwareDescriptor.Comment)));
	strncpy((char *)md->Mcu.Version.BlVersion.Version,  		(char *)BootLoaderDescriptor.Version, 			std::min(sizeof(md->Mcu.Version.BlVersion.Version), sizeof(BootLoaderDescriptor.Version)));
	strncpy((char *)md->Mcu.Version.BlVersion.Date,  			(char *)BootLoaderDescriptor.Created, 			std::min(sizeof(md->Mcu.Version.BlVersion.Date), sizeof(BootLoaderDescriptor.Created)));
	strncpy((char *)md->Mcu.Version.BlVersion.Manufacturer,  (char *)BootLoaderDescriptor.Manufacturer, 	std::min(sizeof(md->Mcu.Version.BlVersion.Manufacturer), sizeof(BootLoaderDescriptor.Manufacturer)));
	strncpy((char *)md->Mcu.Version.BlVersion.Author,  		(char *)BootLoaderDescriptor.Author, 			std::min(sizeof(md->Mcu.Version.BlVersion.Author), sizeof(BootLoaderDescriptor.Author)));
	strncpy((char *)md->Mcu.Version.BlVersion.Comment,			"Штатный Загрузчик для внешнего USB модуля E20-10", sizeof(md->Mcu.Version.BlVersion.Comment));
	strncpy((char *)md->Mcu.Comment, 								"8-bit Microcontroller with 16K Bytes In-System Programmable Flash", sizeof(md->Mcu.Comment));
	md->Mcu.ClockRate = ModuleDescriptor.ClockRate/1000.0;	// в кГц
	
	// определимся с номером версии Firmware микроконтроллера
	FirmwareVersionNumber = atof((char *)md->Mcu.Version.FwVersion.Version);
	if((FirmwareVersionNumber < 0.75) || (FirmwareVersionNumber > 20.0)) { LAST_ERROR_NUMBER(30); return FALSE; }

	// признак Temperature Grade у прошивки ПЛИС'а (с версии 02.0x.12): Commercial or Industrial
	PldVersion.Value = dwPldVersionNumber;
	if((((0x2 == PldVersion.BYTES.HighNumber) && (12 <= PldVersion.BYTES.LowNumber)) ||
		(0x3 <= PldVersion.BYTES.HighNumber)) && (2.35 < FirmwareVersionNumber))
	{
		// попробуем вычитать текущий температурный ранг прошивки ПЛИС'а
		if(!GetArray(SEL_PLD_TG_DATA, &PldTemperatureGrade, 0x1)) { LAST_ERROR_NUMBER(16); return FALSE; }
		PldTemperatureGrade = (PldTemperatureGrade & 0x1) ? (BYTE)INDUSTRIAL_TEMPERATURE_GRADE_E2010 : (BYTE)COMMERCIAL_TEMPERATURE_GRADE_E2010;
	}
	else PldTemperatureGrade = COMMERCIAL_TEMPERATURE_GRADE_E2010;

	// исполнение модуля
	Modification.Value = ModuleDescriptor.Modification;
	Modification.BITS.PldTemperatureGrade = PldTemperatureGrade;
	md->Module.Modification	= Modification.Value;

	// установим признак возможности использования расширенной управляющей таблицы (до 8192 элементов)
	if((2.15 < FirmwareVersionNumber) && (200.065 < fPldVersionNumber))
	{
		if(((0x2 == PldVersion.BYTES.HighNumber) && (13 <= PldVersion.BYTES.LowNumber)))
		{
			IsExtendedControlTableEnabled		= TRUE;
			MaxControlTableLength				= MAX_EXTENDED_CONTROL_TABLE_LENGTH_E2010 / 2;
			AdcParamSize							= HALF_EXTENDED_ADC_PARAM_SIZE;
		}
		else
		{
			IsExtendedControlTableEnabled		= TRUE;
			MaxControlTableLength				= MAX_EXTENDED_CONTROL_TABLE_LENGTH_E2010;
			AdcParamSize							= EXTENDED_ADC_PARAM_SIZE;
		}
	}
	else
	{
		IsExtendedControlTableEnabled			= FALSE;
		MaxControlTableLength					= MAX_CONTROL_TABLE_LENGTH_E2010;
		AdcParamSize								= ADC_PARAM_SIZE;
	}

	// ПЛИС Info
	if(PldInfoString[0])
	{
		md->Pld.Active = TRUE;
		// для ревизии 'A'
		if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
			strncpy((char *)md->Pld.Name, "ALTERA ACEX EP1K10TC144", sizeof(md->Pld.Name));
		else if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_B_E2010])
			strncpy((char *)md->Pld.Name, "ALTERA Cyclone EP1C3T144", sizeof(md->Pld.Name));
		else
			strncpy((char *)md->Pld.Name, "Unknown PLD type", sizeof(md->Pld.Name));
		md->Pld.ClockRate = MASTER_QUARTZ;		// в кГц
		// ПЛИС Name
/*		pTag = strstr((char*)PldInfoString, "<Name>");
		if(pTag && ((p1 = strchr(pTag, '{')) != NULL) && ((p2 = strchr(pTag, '}')) != NULL))
			strncpy((char *)md->Pld.Name, (char *)(p1 + 0x1), std::min(sizeof(md->Pld.Name), (UINT)(p2-p1-0x1)));
		else
			strcpy((char *)md->Pld.Name, "Invalid Pld Name");
*/		// ПЛИС Version.Version
		pTag = strstr((char*)PldInfoString, "<Version>");
		if(pTag && ((p1 = strchr(pTag, '{')) != NULL) && ((p2 = strchr(pTag, ' ')) != NULL))
		{
			strncpy((char *)md->Pld.Version.Version, (char *)(p1 + 0x1), std::min(static_cast<UINT>(sizeof(md->Pld.Version.Version)), (UINT)(p2-p1-0x1)));
//			else
//				md->Pld.FirmwareVersion = INVALID_PLD_FIRMWARE_VERSION;
		}
		else strncpy((char *)md->Pld.Version.Version, "????????", sizeof(md->Pld.Version.Version));
		// ПЛИС Version.Date
		pTag = strstr((char*)PldInfoString, "(build");
		if(pTag && ((p1 = strchr(pTag, ' ')) != NULL) && ((p2 = strchr(pTag, ')')) != NULL))
		{
			strncpy((char*)md->Pld.Version.Date, (char*)(p1 + 0x1), std::min(static_cast<UINT>(sizeof(md->Pld.Version.Date)), (UINT)(p2 - p1 - 0x1)));
//			else
//				md->Pld.FirmwareVersion = INVALID_PLD_FIRMWARE_VERSION;
		}
		else strncpy((char *)md->Pld.Version.Date, "????????", sizeof(md->Pld.Version.Version));
		// ПЛИС Version.Manufacturer
		strncpy((char *)md->Pld.Version.Manufacturer, "L-Card Ltd.", sizeof(md->Pld.Version.Manufacturer));
		// ПЛИС Version.Author
		md->Pld.Version.Author[0x0] = '\0';
		// ПЛИС Version.Comment
		md->Pld.Version.Comment[0x0] = '\0';
		// ПЛИС Comment
		pTag = strstr((char*)PldInfoString, "<Comments>");
		if(pTag && ((p1 = strchr(pTag, '{')) != NULL) && ((p2 = strchr(pTag, '}')) != NULL))
			strncpy((char *)md->Pld.Version.Comment, (char *)(p1 + 0x1), std::min(static_cast<UINT>(sizeof(md->Pld.Version.Comment)), (UINT)(p2-p1-0x1)));
		else
			strncpy((char *)md->Pld.Version.Comment, "Invalid Pld Comments", sizeof(md->Pld.Version.Comment));
	}
	else md->Pld.Active = FALSE;

	// ADC Info
	md->Adc.Active = TRUE;
	strcpy((char *)md->Adc.Name, "LTC2245");
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E2010; i++)
	{
		md->Adc.OffsetCalibration[i] 	= ModuleDescriptor.AdcOffsetCoefs[i];
		md->Adc.ScaleCalibration[i] 	= ModuleDescriptor.AdcScaleCoefs[i];
	}
	strcpy((char *)md->Adc.Comment, "14-Bit 10Msps six pipelined stages ADC converter");

	// DAC Info
	if(ModuleDescriptor.IsDacPresented)
	{
		md->Dac.Active = TRUE;
		strcpy((char *)md->Dac.Name, "AD7249");
		for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E2010; i++)
		{
			md->Dac.OffsetCalibration[i] = ModuleDescriptor.DacOffsetCoefs[i];
			md->Dac.ScaleCalibration[i] = ModuleDescriptor.DacScaleCoefs[i];
		}
		strcpy((char *)md->Dac.Comment, "12-Bit 125Ksps DAC converter");
	}
	else md->Dac.Active = FALSE;

	// IO Info
	md->DigitalIo.Active = TRUE;
	strcpy((char *)md->DigitalIo.Name, "74HCT595");
	md->DigitalIo.InLinesQuantity = 16;
	md->DigitalIo.OutLinesQuantity = 16;
	strcpy((char *)md->DigitalIo.Comment, "TTL/CMOS compatible");

	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// запись информации о модуле
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::SAVE_MODULE_DESCRIPTION(MODULE_DESCRIPTION_E2010 * const md)
{
	WORD i;
	BYTE ModuleTemperatureGrade;
	MODIFICATION_E2010 Modification;

	// проверим указатель на структуру
	if(!md) { LAST_ERROR_NUMBER(3); return FALSE; }
	// проверим флажок сбора данных - во время сбора данных нельзя
	// записывать дескриптор модуля
	else if(IsDataAcquisitionInProgress) { LAST_ERROR_NUMBER(17); return FALSE; }

	// обнулим всю структуру MODULE_DESCRIPTOR
	ZeroMemory(&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR));

	// Module Info
	strcpy((char *)ModuleDescriptor.ModuleName,		"E20-10");
	ZeroMemory(md->Module.DeviceName, sizeof(md->Module.DeviceName));
	strcpy((char *)md->Module.DeviceName,				"E20-10");
//	strncpy((char *)ModuleDescriptor.ModuleName,		(char *)md->Module.DeviceName,	 	std::min(sizeof(md->Module.DeviceName), sizeof(ModuleDescriptor.ModuleName)));
	ZeroMemory(ModuleDescriptor.SerialNumber, sizeof(ModuleDescriptor.SerialNumber));
	strncpy((char *)ModuleDescriptor.SerialNumber, 	(char *)md->Module.SerialNumber, 	std::min(sizeof(md->Module.SerialNumber), sizeof(ModuleDescriptor.SerialNumber)));
//	strncpy((char *)ModuleDescriptor.Reserved,     	(char *)md->Module.Comment,      	std::min(sizeof(md->Module.Comment), sizeof(ModuleDescriptor.Reserved)));

	// проверим требуемую ревизию модуля
	if((	md->Module.Revision > REVISIONS_E2010[REVISIONS_QUANTITY_E2010 - 0x1] ||
			md->Module.Revision < REVISIONS_E2010[REVISION_A_E2010])) { LAST_ERROR_NUMBER(35); return FALSE; }
	else ModuleDescriptor.Revision = md->Module.Revision;

	// скопируем флажки, определяющие наличие различных модификации модуля
	Modification.Value = md->Module.Modification;
	
	// проверим требуемый тип ФНЧ
	if(LPF_TYPES_QUANTITY_E2010 <= Modification.BITS.LowPassFilterType) { LAST_ERROR_NUMBER(42); return FALSE; }

	// если Firmware позволяет - попробуем получить температурный ранг железа модуля
	if(2.35 < FirmwareVersionNumber)
	{
		// попробуем вычитать текущий температурный класс из устройства
		if(!GetArray(SEL_MODULE_TG_DATA, &ModuleTemperatureGrade, 0x1)) { LAST_ERROR_NUMBER(16); return FALSE; }
		// проверим температурный класс устройства
		if(TEMPERATURE_GRADES_QUANTITY_E2010 <= ModuleTemperatureGrade) { LAST_ERROR_NUMBER(43); return FALSE; }
		// запомним температурный класс устройства
		Modification.BITS.ModuleTemperatureGrade = ModuleTemperatureGrade;
	}
	else Modification.BITS.ModuleTemperatureGrade = COMMERCIAL_TEMPERATURE_GRADE_E2010;
	// сбросим температурный ранг прошивки ПЛИС'а (нефига ему храниться в ППЗУ)
	Modification.BITS.PldTemperatureGrade = COMMERCIAL_TEMPERATURE_GRADE_E2010;

	// сохраним исполнение в локальной структуре
	ModuleDescriptor.Modification	= Modification.Value;
	// восстановим признак температурного ранга прошивки ПЛИС'а
	Modification.BITS.PldTemperatureGrade = ((MODIFICATION_E2010 *)&md->Module.Modification)->BITS.PldTemperatureGrade;
	// сохраним испольнение в структуре входного параметра
	md->Module.Modification			= Modification.Value;

	// MCU Info
	strncpy((char *)ModuleDescriptor.McuType, (char *)md->Mcu.Name, std::min(sizeof(md->Mcu.Name), sizeof(ModuleDescriptor.McuType)));
	ModuleDescriptor.ClockRate = (DWORD)(1000.0*MASTER_QUARTZ/8.0 + 0.5);	// в Гц

	// ADC Info
	for(i = 0x0; i < ADC_CALIBR_COEFS_QUANTITY_E2010; i++)
	{
		// смещение АЦП канала i
		if((fabs(md->Adc.OffsetCalibration[i]) > 2048.0) ||
			(fabs(md->Adc.OffsetCalibration[i]) < FloatEps)) md->Adc.OffsetCalibration[i] = 0.0;
		ModuleDescriptor.AdcOffsetCoefs[i] = (float)md->Adc.OffsetCalibration[i];
		// масштаб АЦП канала i
		if((fabs(md->Adc.ScaleCalibration[i]) > 1.9) ||
			(fabs(md->Adc.ScaleCalibration[i]) < FloatEps)) md->Adc.ScaleCalibration[i] = 1.0;
		ModuleDescriptor.AdcScaleCoefs[i]	= (float)md->Adc.ScaleCalibration[i];
	}

	// DAC Info
	ModuleDescriptor.IsDacPresented = (BYTE)(md->Dac.Active ? 0x1 : 0x0);
	for(i = 0x0; i < DAC_CALIBR_COEFS_QUANTITY_E2010; i++)
	{
		// проверим - есть ли ЦАП на борту
		if(md->Dac.Active)
		{
			// смещение ЦАП канала i
			if((fabs(md->Dac.OffsetCalibration[i]) > 512.0) ||
				(fabs(md->Dac.OffsetCalibration[i]) < FloatEps)) md->Dac.OffsetCalibration[i] = 0.0;
			ModuleDescriptor.DacOffsetCoefs[i] = (float)md->Dac.OffsetCalibration[i];
			// масштаб ЦАП канала i
			if((fabs(md->Dac.ScaleCalibration[i]) > 1.9) ||
				(fabs(md->Dac.ScaleCalibration[i]) < FloatEps)) md->Dac.ScaleCalibration[i] = 1.0;
			ModuleDescriptor.DacScaleCoefs[i] = (float)md->Dac.ScaleCalibration[i];
		}
		else
		{
			// смещение ЦАП канала i
			ModuleDescriptor.DacOffsetCoefs[i] = 0.0;
			// масштаб ЦАП канала i
			ModuleDescriptor.DacScaleCoefs[i] = 1.0;
		}
	}

	// теперь вычислим CRC16 получившейся структуры
	ModuleDescriptor.CRC16 = 0x0;
	ModuleDescriptor.CRC16 = CalculateCrc16((BYTE *)&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR) - sizeof(WORD));

	// попробуем записать сформированный дескриптор модуля
	if(!SaveModuleDescriptor()) { LAST_ERROR_NUMBER(29); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}




//==============================================================================
// внутренние функции для работы с модулем E20-10
//==============================================================================
//------------------------------------------------------------------------------
// инициализация всех необходимых переменных
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::InitVars(void)
{
	// инициализация всех локальных структур класса
	ZeroMemory(&ModuleDescriptor, sizeof(MODULE_DESCRIPTOR));
	ZeroMemory(&FirmwareDescriptor, sizeof(FIRMWARE_DESCRIPTOR));
	ZeroMemory(&BootLoaderDescriptor, sizeof(BOOT_LOADER_DESCRIPTOR));
	ZeroMemory(&PldInfoString, sizeof(PldInfoString));
	// по-умолчанию будем считать, что  микроконтроллер находится в режиме 'Приложение' (Application)
	IsMcuApplicationActive = TRUE;
	// сбросим флажок сбора данных
	IsDataAcquisitionInProgress = FALSE;
	// сбросим флажок разрешения записи в пользователькое ППЗУ
	IsUserFlashWriteEnabled = FALSE;
	// сбросим индикатор возможности использования расширенной управляющей таблицы (до 8192 элементов)
	IsExtendedControlTableEnabled = FALSE;
	// максимально возможное кол-во каналов в управляющей таблице
	MaxControlTableLength = MAX_CONTROL_TABLE_LENGTH_E2010;
	// реально используемый размер массива AdcParamsArray
	AdcParamSize = ADC_PARAM_SIZE;
	// сбросим номер версии Firmware микроконтроллера
	FirmwareVersionNumber = 0.0;
	// сбросим номер версии прошивки ПЛИС'а
	fPldVersionNumber = 0.0; dwPldVersionNumber = 0x0;

	// всё хорошо
	return TRUE;
}

//------------------------------------------------------------------------------
// сброс микросхемы ПЛИС
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::ResetPld(void)
{
	// формируем вендор сброса микросхемы ПЛИС
	WORD InBuf[4] = { 0x0, V_RESET_PLD, 0x0, 0x0 };

	// посылаем запрос в модуль
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(19); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// запуск загруженной микросхемы ПЛИС
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::StartPld(void)
{
	// формируем вендор запуска ПЛИС
	WORD InBuf[4] = { 0x0, V_START_PLD, 0x0, 0x0 };

	// посылаем запрос в модуль
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, InBuf, sizeof(InBuf), NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(19); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// загрузка микросхемы ПЛИС
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::LoadPld(BYTE * const PldData, DWORD NBytes)
{
	IO_REQUEST_LUSBAPI IoReq;

	// проверяем буфер
	if(!PldData) { LAST_ERROR_NUMBER(3); return FALSE; }
	// осуществляем сброс микросхемы ПЛИС
	else if(!ResetPld()) { return FALSE; }
	// зачистим канал передачи данных  - Reset Write Pipe
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE1, NULL, 0, NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(19); return FALSE; }
	// зачистим канал приёма данных - Reset Read Pipe
	else if(!TLUSBBASE::LDeviceIoControl(DIOC_RESET_PIPE3, NULL, 0, NULL, 0, TimeOut)) { LAST_ERROR_NUMBER(40); return FALSE; }
	// заливаем ПЛИС
	else if(!PutArray(SEL_PLD_DATA, PldData, NBytes)) { LAST_ERROR_NUMBER(18); return FALSE; }
	// запускаем ПЛИС
	else if(!StartPld()) { return FALSE; }

	// обнулим строку с информацией об используемом ПЛИС
	ZeroMemory(&PldInfoString, sizeof(PldInfoString));
	// сбросим номер версии прошивки ПЛИС
	fPldVersionNumber = 0.0; dwPldVersionNumber = 0x0;

	// формируем структуру запроса типа IO_REQUEST_LUSBAPI на ввод данных
	IoReq.Buffer 					= (SHORT *)PldInfoString;			// буфер данных
	if(ModuleDescriptor.Revision == REVISIONS_E2010[REVISION_A_E2010])
		IoReq.NumberOfWordsToPass 	= sizeof(PldInfoString)/2;		// кол-во данных в SHORT
	else
		IoReq.NumberOfWordsToPass 	= sizeof(PldInfoString)/2 - 0x1;	// кол-во данных в SHORT
	IoReq.NumberOfWordsPassed 	= 0x0;
	IoReq.Overlapped			  	= NULL;							// у нас будет синхронный запрос
	IoReq.TimeOut					= TimeOut;						// таймаут выполнения запроса
	// читаем ответ микросхемы ПЛИС в локальную структуру PldInfoString
	if(!ReadData(&IoReq))
	{
		// должно прочитаться ровно 255 слов (510 байт)
		if(IoReq.NumberOfWordsPassed == 255) return TRUE;
		else { return FALSE; }
	}
	// попробуем определить номер версии прошивки ПЛИС
	if(!GetPldVersionNumber(&fPldVersionNumber, &dwPldVersionNumber)) { LAST_ERROR_NUMBER(44); return FALSE; }

	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// разбор текущего режима работы модуля (только Rev.A)
//------------------------------------------------------------------------------
void WINAPI TLE2010::GetSynchroParsForRevA(BYTE ModeParam, ADC_PARS_E2010 * const AdcPars)
{
	// режим фиксации факта перегрузки входных каналов модуля (только для Rev.A)
	AdcPars->OverloadMode = (BYTE)((ModeParam & (0x1 << 0x3)) ? MARKER_OVERLOAD_E2010 : CLIPPING_OVERLOAD_E2010);

	// источник импульса начала сбора данны с АЦП
	if(ModeParam & (0x1 << 0x0))
	{
		// внутренний импульс начала сбора данных без трансляции на разъём модуля
		if(ModeParam &(0x1 << 0x7)) AdcPars->SynchroPars.StartSource = INT_ADC_START_E2010;
		// внутренний импульс начала сбора данных с трансляцией на разъём модуля
		else AdcPars->SynchroPars.StartSource = INT_ADC_START_WITH_TRANS_E2010;
	}
	else
	{
		// внешний импульс начала сбора данных по фронту
		if(!(ModeParam & (0x1 << 0x4))) AdcPars->SynchroPars.StartSource = EXT_ADC_START_ON_RISING_EDGE_E2010;
		// внешний импульс начала сбора данных по спаду
		else AdcPars->SynchroPars.StartSource = EXT_ADC_START_ON_FALLING_EDGE_E2010;
	}
	// источник тактовых импульсов запуска АЦП
	if(!(ModeParam & (0x1 << 0x1)))
	{
		// внутренние тактовые импульсы АЦП без трансляции на разъём модуля
		if(ModeParam & (0x1 << 0x6)) AdcPars->SynchroPars.SynhroSource = INT_ADC_CLOCK_E2010;
		// внутренние тактовые импульсы АЦП с трансляцией на разъём модуля
		else AdcPars->SynchroPars.SynhroSource = INT_ADC_CLOCK_WITH_TRANS_E2010;
	}
	else
	{
		// внешние тактовые импульсы АЦП, по фронту
		if(ModeParam & (0x1 << 0x5)) AdcPars->SynchroPars.SynhroSource = EXT_ADC_CLOCK_ON_FALLING_EDGE_E2010;
		// внешние тактовые импульсы АЦП, по спаду
		else AdcPars->SynchroPars.SynhroSource = EXT_ADC_CLOCK_ON_RISING_EDGE_E2010;
	}
}

//------------------------------------------------------------------------------
// разбор текущего режима работы модуля (Rev.B и выше)
//------------------------------------------------------------------------------
void WINAPI TLE2010::GetSynchroParsForRevBandAbove(BYTE ModeParam, ADC_PARS_E2010 * const AdcPars)
{
	// только один режим фиксации факта перегрузки входных каналов модуля: ограничение
	AdcPars->OverloadMode = CLIPPING_OVERLOAD_E2010;

	// внутренний источник сигнала начала сбора данных с АЦП
	if(ModeParam & (0x1 << 0x0))
	{
		// внутренний импульс начала сбора данных без трансляции на разъём модуля
		if(ModeParam &(0x1 << 0x7)) AdcPars->SynchroPars.StartSource = INT_ADC_START_E2010;
		// внутренний импульс начала сбора данных с трансляцией на разъём модуля
		else AdcPars->SynchroPars.StartSource = INT_ADC_START_WITH_TRANS_E2010;
	}
	// внешний источник сигнала начала сбора данных с АЦП
	else AdcPars->SynchroPars.StartSource = (WORD)((ModeParam >> 0x3) & 0x3);

	// источник тактовых импульсов запуска АЦП
	if(!(ModeParam & (0x1 << 0x1)))
	{
		// внутренние тактовые импульсы АЦП без трансляции на разъём модуля
		if(ModeParam & (0x1 << 0x6)) AdcPars->SynchroPars.SynhroSource = INT_ADC_CLOCK_E2010;
		// внутренние тактовые импульсы АЦП с трансляцией на разъём модуля
		else AdcPars->SynchroPars.SynhroSource = INT_ADC_CLOCK_WITH_TRANS_E2010;
	}
	else
	{
		// внешние тактовые импульсы АЦП, по фронту
		if(ModeParam & (0x1 << 0x5)) AdcPars->SynchroPars.SynhroSource = EXT_ADC_CLOCK_ON_FALLING_EDGE_E2010;
		// внешние тактовые импульсы АЦП, по спаду
		else AdcPars->SynchroPars.SynhroSource = EXT_ADC_CLOCK_ON_RISING_EDGE_E2010;
	}
}

//------------------------------------------------------------------------------
// чтение дополнительных параметров синхронизации сбора данных (для Rev.B и выше)
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::GetExtraSynchroParsForRevBandAbove(ADC_PARS_E2010 * const AdcPars)
{
	// считаем дополнительные параметры синхронизации сбора данных
	if(!GetArray(EXTRA_SYNCHRO_PARS_ADDRESS, (BYTE *)&ExtraSynchroPars, sizeof(EXTRA_SYNCHRO_PARS))) { LAST_ERROR_NUMBER(16); return FALSE; }

	// проверим есть аналоговая синхронизация
	if(ExtraSynchroPars.SynchroAdMode & (0x1 << AD_ENA))
	{
		// физический канал АЦП для аналоговой синхронизации
		AdcPars->SynchroPars.SynchroAdChannel = (WORD)(ExtraSynchroPars.SynchroAdMode & 0x3);
		// режим аналоговой сихронизации: переход или уровень
		AdcPars->SynchroPars.SynchroAdMode = (WORD)(((ExtraSynchroPars.SynchroAdMode >> AD_M0) & 0x3) + 0x1);
	}
	else AdcPars->SynchroPars.SynchroAdMode = NO_ANALOG_SYNCHRO_E2010;
	// физический номер канала для аналоговой синхронизации
	AdcPars->SynchroPars.SynchroAdChannel = (WORD)(ExtraSynchroPars.SynchroAdMode & 0x3);
	// порог срабатывания при аналоговой синхронизации
	AdcPars->SynchroPars.SynchroAdPorog = ExtraSynchroPars.SynchroAdPorog;
	// задержка старта сбора данных в кадрах отсчётов АЦП
	AdcPars->SynchroPars.StartDelay = ExtraSynchroPars.StartDelay;
	// останов сбора данных после задаваемого здесь кол-ва собранных кадров отсчётов АЦП
	AdcPars->SynchroPars.StopAfterNKadrs = ExtraSynchroPars.StopAfterNKadrs;
	// маркирование начала блоков вводимых данных (удобно, например, при аналоговой синхронизации ввода по уровню)
	AdcPars->SynchroPars.IsBlockDataMarkerEnabled = ExtraSynchroPars.IsBlockDataMarkerEnabled;

	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// формирование режима работы модуля (только Rev.A)
//------------------------------------------------------------------------------
void WINAPI TLE2010::MakeSynchroParsForRevA(BYTE &ModeParam, ADC_PARS_E2010 * const AdcPars)
{
	// задаём режим фиксации факта перегрузки входных каналов (только для Rev.A)
	if(AdcPars->OverloadMode >= INVALID_OVERLOAD_E2010) AdcPars->OverloadMode = CLIPPING_OVERLOAD_E2010;
	AdcParamsArray[0] |= (BYTE)((AdcPars->OverloadMode ? 0x1 : 0x0) << 0x3);

	// зачистим поле ADC mode
	ModeParam = 0x0;
	// источник импульса начала сбора данных с АЦП
	if(AdcPars->SynchroPars.StartSource <= INT_ADC_START_WITH_TRANS_E2010)
	{
		// внутренний импульс начала сбора данных с трансляцией на разъём модуля
		ModeParam |= (0x1 << 0x0);
		// внутренний импульс начала сбора данных без трансляции на разъём модуля
		if(AdcPars->SynchroPars.StartSource == INT_ADC_START_E2010) ModeParam |= (0x1 << 0x7);
	}
	else
	{
		// не все режимы тактирования поддерживаются данной ревизией модуля
		if(AdcPars->SynchroPars.StartSource > EXT_ADC_START_ON_FALLING_EDGE_E2010)
							AdcPars->SynchroPars.StartSource = EXT_ADC_START_ON_FALLING_EDGE_E2010;
		// внешний импульс начала сбора данных, по фронту
		ModeParam |= (0x1 << 0x2) | (0x1 << 0x7);
		// внешний импульса начала сбора данных по спаду
		if(AdcPars->SynchroPars.StartSource == EXT_ADC_START_ON_FALLING_EDGE_E2010) ModeParam |= (0x1 << 0x4);
	}
	// источник тактовых импульсов запуска АЦП
	if(AdcPars->SynchroPars.SynhroSource <= INT_ADC_CLOCK_WITH_TRANS_E2010)
	{
		// внутренние тактовые импульсы АЦП с трансляцией на разъём модуля
		// .....
		// внутренние тактовые импульсы АЦП без трансляции на разъём модуля
		if(AdcPars->SynchroPars.SynhroSource == INT_ADC_CLOCK_E2010) ModeParam |= (0x1 << 0x6);
	}
	else
	{
		// внешние тактовые импульсы АЦП, по фронту
		ModeParam |= (0x1 << 0x1) | (0x1 << 0x6);
		// внешние тактовые импульсы АЦП, по спаду
		if(AdcPars->SynchroPars.SynhroSource == EXT_ADC_CLOCK_ON_FALLING_EDGE_E2010) ModeParam |= (0x1 << 0x5);
	}
}

//------------------------------------------------------------------------------
// формирование режима работы модуля (Rev.B и выше)
//------------------------------------------------------------------------------
void WINAPI TLE2010::MakeSynchroParsForRevBandAbove(BYTE &ModeParam, ADC_PARS_E2010 * const AdcPars)
{
	// зачистим поле ADC mode
	ModeParam = 0x0;
	// источник импульса начала сбора данных с АЦП
	if(AdcPars->SynchroPars.StartSource <= INT_ADC_START_WITH_TRANS_E2010)
	{
		// внутренний импульс начала сбора данных с трансляцией на разъём модуля
		ModeParam |= (0x1 << 0x0);
		// внутренний импульс начала сбора данных без трансляции на разъём модуля
		if(AdcPars->SynchroPars.StartSource == INT_ADC_START_E2010) ModeParam |= (0x1 << 0x7);
	}
	else
	{
		// проверим заданный режим внешнего тактирования ввода данных
		if(AdcPars->SynchroPars.StartSource >= INVALID_ADC_START_E2010)
							AdcPars->SynchroPars.StartSource = EXT_ADC_START_ON_FALLING_EDGE_E2010;//EXT_ADC_START_ON_LOW_LEVEL_E2010;
		// внешний импульс начала сбора данных
		ModeParam |= (0x1 << 0x2) | (0x1 << 0x7);
		// тип внешнего импульса начала сбора данных: по фронту или спаду
		ModeParam |= (BYTE)(((AdcPars->SynchroPars.StartSource - 0x2) & 0x3) << 0x3);
	}
	// источник тактовых импульсов запуска АЦП
	if(AdcPars->SynchroPars.SynhroSource <= INT_ADC_CLOCK_WITH_TRANS_E2010)
	{
		// внутренние тактовые импульсы АЦП с трансляцией на разъём модуля
		// .....
		// внутренние тактовые импульсы АЦП без трансляции на разъём модуля
		if(AdcPars->SynchroPars.SynhroSource == INT_ADC_CLOCK_E2010) ModeParam |= (0x1 << 0x6);
	}
	else
	{
		// внешние тактовые импульсы АЦП, по фронту
		ModeParam |= (0x1 << 0x1) | (0x1 << 0x6);
		// внешние тактовые импульсы АЦП, по спаду
		if(AdcPars->SynchroPars.SynhroSource == EXT_ADC_CLOCK_ON_FALLING_EDGE_E2010) ModeParam |= (0x1 << 0x5);
	}
}

//------------------------------------------------------------------------------
// дополнительные параметры сбора данных
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::SetExtraSynchroParsForRevBandAbove(ADC_PARS_E2010 * const AdcPars)
{
	BYTE i;

	// проверим номер физического канала АЦП
	if(AdcPars->SynchroPars.SynchroAdChannel >= ADC_CHANNELS_QUANTITY_E2010)
			AdcPars->SynchroPars.SynchroAdChannel = ADC_CHANNELS_QUANTITY_E2010 - 0x1;
	// проверим есть аналоговая синхронизация
	if(AdcPars->SynchroPars.SynchroAdMode != NO_ANALOG_SYNCHRO_E2010)
	{
		// проверим наличие в управляющей таблице физического канала аналоговой синхронизации
		for(i = 0x0; i < AdcPars->ChannelsQuantity; i++)
			if(AdcPars->SynchroPars.SynchroAdChannel == AdcPars->ControlTable[i]) break;
		if(i == AdcPars->ChannelsQuantity)  { LAST_ERROR_NUMBER(41); return FALSE; }

		// разрешение аналоговой синхронизации
		ExtraSynchroPars.SynchroAdMode = 0x1 << AD_ENA;
		// режим аналоговой сихронизации: переход или уровень
		ExtraSynchroPars.SynchroAdMode |= (WORD)(((AdcPars->SynchroPars.SynchroAdMode - 0x1) & 0x3) << AD_M0);
	}
	else ExtraSynchroPars.SynchroAdMode = 0x0;
	// физический канал АЦП для аналоговой синхронизации
	ExtraSynchroPars.SynchroAdMode |= (WORD)(AdcPars->SynchroPars.SynchroAdChannel & 0x3);
	// порог срабатывания при аналоговой синхронизации
	ExtraSynchroPars.SynchroAdPorog = AdcPars->SynchroPars.SynchroAdPorog;
	// проверим есть ли аналоговая синхронизация по уровню
	if((AdcPars->SynchroPars.SynchroAdMode == ANALOG_SYNCHRO_ON_HIGH_LEVEL_E2010) ||
		(AdcPars->SynchroPars.SynchroAdMode == ANALOG_SYNCHRO_ON_LOW_LEVEL_E2010))
	{
		// задержка старта сбора данных в кадрах отсчётов АЦП
		ExtraSynchroPars.StartDelay = AdcPars->SynchroPars.StartDelay = 0x0;
		// останов сбора данных после задаваемого здесь кол-ва собранных кадров отсчётов АЦП
		ExtraSynchroPars.StopAfterNKadrs = AdcPars->SynchroPars.StopAfterNKadrs = 0x0;
	}
	else
	{
		// задержка старта сбора данных в кадрах отсчётов АЦП
		if(AdcPars->SynchroPars.StartDelay <= MAX_START_DELAY) ExtraSynchroPars.StartDelay = AdcPars->SynchroPars.StartDelay;
		else ExtraSynchroPars.StartDelay = AdcPars->SynchroPars.StartDelay = MAX_START_DELAY;
		// останов сбора данных после задаваемого здесь кол-ва собранных кадров отсчётов АЦП
		if(AdcPars->SynchroPars.StopAfterNKadrs <= MAX_STOP_AFTER_NKADRS) ExtraSynchroPars.StopAfterNKadrs = AdcPars->SynchroPars.StopAfterNKadrs;
		else ExtraSynchroPars.StopAfterNKadrs = AdcPars->SynchroPars.StopAfterNKadrs = MAX_STOP_AFTER_NKADRS;
	}
	// маркирование начала блоков вводимых данных (удобно, например, при аналоговой синхронизации ввода по уровню)
	ExtraSynchroPars.IsBlockDataMarkerEnabled = (AdcPars->SynchroPars.IsBlockDataMarkerEnabled == 0x0) ? 0x0 : 0x1;

	// теперь передадим соответствующие данные в микроконтроллер
	if(!PutArray(EXTRA_SYNCHRO_PARS_ADDRESS, (BYTE *)&ExtraSynchroPars, sizeof(EXTRA_SYNCHRO_PARS))) { LAST_ERROR_NUMBER(18); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}

//------------------------------------------------------------------------------
// чтение дескриптора модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::GetModuleDescriptor(MODULE_DESCRIPTOR const * ModuleDescriptor)
{
	BOOL Error;
	BYTE *ptr;
	WORD crc16;

	// попробуем выделить буфер под массив размером sizeof(MODULE_DESCRIPTOR) байтов
	ptr = new BYTE[sizeof(MODULE_DESCRIPTOR)];
	if(!ptr) { LAST_ERROR_NUMBER(32); return FALSE; }

	// сбросим флаг ошибки
	Error = FALSE;
	// процесс чтения и анализа
	do
	{
		// читаем из ППЗУ байтовый образ структуры типа MODULE_DESCRIPTOR
		if(!GetArray(MODULE_DESCRIPTOR_ADDRESS, ptr, sizeof(MODULE_DESCRIPTOR))) { LAST_ERROR_NUMBER(16); Error = TRUE; break; }
		// формируем CRC считанного из ППЗУ байтового образа
		crc16 = (WORD)(((WORD)ptr[sizeof(MODULE_DESCRIPTOR) - 0x1] << 0x8) | ptr[sizeof(MODULE_DESCRIPTOR) - 0x2]);
		// если CRC16 = 0x0, то полагаем, что в модуле содержится структура от
		// предыдущей версии библиотеки и потому не будем проверять её контрольную сумму
		if(crc16)
		{
			// иначе проверим CRC считанного из ППЗУ байтового образа
			if(crc16 != CalculateCrc16(ptr, sizeof(MODULE_DESCRIPTOR) - sizeof(WORD))) { LAST_ERROR_NUMBER(33); Error = TRUE; break; }
		}
		// всё в порядке - копируем всё в структуру типа MODULE_DESCRIPTOR
		CopyMemory((BYTE *)ModuleDescriptor, ptr, sizeof(MODULE_DESCRIPTOR));
	}
	while(false);
	// освободим ресурс
	delete[] ptr;

	if(Error) return FALSE;
	else return TRUE;
}

//------------------------------------------------------------------------------
// попробуем определить номер версии прошивки ПЛИС
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::GetPldVersionNumber(double * const fPVN, DWORD * const dwPVN)
{
	char *pTag, *p1, *p2;
	char TempStr[0x7];
	char PldVersionStr[17];

	// проверим входные параметры
	if(!fPVN || !dwPVN) return FALSE;
	// сбросим значения входных параметров
	*fPVN = 0.0; *dwPVN = 0x0;

	// обнулим локальные строки
	ZeroMemory(TempStr, 7);
	ZeroMemory(PldVersionStr, 17);
	// проанализируем массив PldInfoString
	pTag = strstr((char*)PldInfoString, "<Version>");
	if(pTag && ((p1 = strchr(pTag, '{')) != NULL) && ((p2 = strchr(pTag, ' ')) != NULL))
	{
		// копируем строку с версией - типа 2.00.06
		strncpy((char *)PldVersionStr, (char *)(p1 + 0x1), (UINT)(p2-p1-0x1));

		// -=== формируем версию в формате DWORD ===-
		// выделяем первое число версии
		p1 = PldVersionStr; p2 = strchr(p1, '.');
		if(p2 && ((UINT)(p2 - p1) < 0x7))
		{
			strncpy((char *)TempStr, p1, (UINT)(p2 - p1));
			*dwPVN |=  (atoi(TempStr) & 0xFF) << 0x10;
		}
		else { *fPVN = 0.0; *dwPVN = 0x0; return FALSE; }
		// выделяем второе число версии
		p1 = p2 + 0x1; p2 = strchr(p1, '.');
		if(p2 && ((UINT)(p2 - p1) < 0x7))
		{
			strncpy((char *)TempStr, p1, (UINT)(p2 - p1));
			*dwPVN |=  (atoi(TempStr) & 0xFF) << 0x8;
		}
		else { *fPVN = 0.0; *dwPVN = 0x0; return FALSE; }
		// выделяем третье число версии
		p1 = p2 + 0x1; p2 = strchr(p1, '\0');
		if(p2 && ((UINT)(p2 - p1) < 0x7))
		{
			strncpy((char *)TempStr, p1, (UINT)(p2 - p1));
			*dwPVN |=  (atoi(TempStr) & 0xFF) << 0x0;
		}
		else { *fPVN = 0.0; *dwPVN = 0x0; return FALSE; }
		// -========================================-

		// -=== формируем версию в формате float ===-
		// ищем в строке первую точку и самый последний 0
		if((p1 = strchr(PldVersionStr, '.')) != NULL && ((p2 = strchr(PldVersionStr, 0)) != NULL))
		{
			// убираем первую точку и получаем осмысленное число
			MoveMemory(p1, p1 + 0x1, (UINT)(p2-p1));
			// текущий номер версии в формате xxx.yy
			*fPVN = atof(PldVersionStr);
		}
		else { *fPVN = 0.0; *dwPVN = 0x0; return FALSE; }

		// всё хорошо
		return TRUE;
	}
	else { *fPVN = 0.0; *dwPVN = 0x0; return FALSE; }
}

//------------------------------------------------------------------------------
// чтение массива данных из памяти микроконтроллера
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::GetArray(DWORD Address, BYTE * const Data, DWORD Size)
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
		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + i, sz, TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(19); return FALSE; }

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
// запись массива данных в память микроконтроллера
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::PutArray(DWORD Address, BYTE * const Data, DWORD Size)
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
		else if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), Data + i, sz, TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(19); return FALSE; }

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
BOOL WINAPI TLE2010::RunMcuApplication(DWORD Address, WORD BitParam)
{
	// формируем вендор запуска приложения в микроконтроллере
	WORD InBuf[4] = { 0x00, V_CALL_APPLICATION, (WORD)(Address & 0xFFFF), 0x0 };//(WORD)(Address >> 0x10) };
//	WORD InBuf[4] = { 0x00, V_CALL_APPLICATION, (WORD)(Address & 0xFFFF), (WORD)(Address >> 0x10) };
//	WORD InBuf[4] = { 0x00, 0x0, 0x0, 0x0 };

	// проверим адрес
	if(Address == (DWORD)FIRMWARE_START_ADDRESS)
	{
		InBuf[0x3] = BitParam;
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
	if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, InBuf, sizeof(InBuf), NULL, 0x0, TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(19); return FALSE; }
	// мы запустили приложение в микроконтроллере?
	IsMcuApplicationActive = (Address >= (DWORD)BOOT_LOADER_START_ADDRESS) ? FALSE : TRUE;
	// освободим ресурс критической секции
	LeaveCriticalSection(&cs);

	// все хорошо :)))))
	return TRUE;
}

//------------------------------------------------------------------------------
// запись дескриптора модуля
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::SaveModuleDescriptor(void)
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

		if(!TLUSBBASE::LDeviceIoControl(DIOC_SEND_COMMAND, &InBuf, sizeof(InBuf), ptr + i, sz, TimeOut)) { LeaveCriticalSection(&cs); LAST_ERROR_NUMBER(19); return FALSE; }
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

//------------------------------------------------------------------------------
// перевод модуля в соотвествующие тестовые режимы
//------------------------------------------------------------------------------
BOOL WINAPI TLE2010::SetTestMode(WORD TestModeMask)
{
	WORD LocTestModeMask;

	// сохраним параметр в локальной переменной
	LocTestModeMask = TestModeMask;
	// передадим соответствующие данные в микроконтроллер
	if(!PutArray(SEL_TEST_MODE, (BYTE *)&LocTestModeMask, 2)) { LAST_ERROR_NUMBER(18); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}


//------------------------------------------------------------------------------
// получение отдадочной информации
//------------------------------------------------------------------------------
/*BOOL WINAPI TLE2010::GET_DEBUG_INFO(WORD * const DebugInfo)
{
	// передадим запрос на получение отладочной информации
	if(!GetArray(SEL_DEBUG_INFO, (BYTE *)DebugInfo, 0x2)) { LAST_ERROR_NUMBER(18); return FALSE; }
	// все хорошо :)))))
	else return TRUE;
}*/

