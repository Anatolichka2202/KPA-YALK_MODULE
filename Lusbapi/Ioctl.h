//------------------------------------------------------------------------------
#ifndef __IOCTLH__
#define __IOCTLH__
//------------------------------------------------------------------------------
	// общие запросы в драйвер
	#define DIOC_SEND_COMMAND \
		CTL_CODE(FILE_DEVICE_UNKNOWN, 15, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)

	#define DIOC_GET_PARAMS \
		CTL_CODE(FILE_DEVICE_UNKNOWN, 23, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)


	// запросы в драйвер предназначенный для работы с библиотекой Lusbapi
	#define DIOC_RESET_PIPE1 \
		CTL_CODE (FILE_DEVICE_UNKNOWN,0x105,METHOD_OUT_DIRECT,FILE_ANY_ACCESS)

	#define DIOC_RESET_PIPE3 \
		CTL_CODE (FILE_DEVICE_UNKNOWN,0x106,METHOD_OUT_DIRECT,FILE_ANY_ACCESS)

	#define DIOC_ABORT_PIPE1 \
		CTL_CODE (FILE_DEVICE_UNKNOWN,0x107,METHOD_OUT_DIRECT,FILE_ANY_ACCESS)

	#define DIOC_ABORT_PIPE3 \
		CTL_CODE (FILE_DEVICE_UNKNOWN,0x108,METHOD_OUT_DIRECT,FILE_ANY_ACCESS)

#endif

