/*
    Модуль работы с встроенной флеш-памятью контроллера LPC2478
*/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
//#include <LPC24xx.h>
#include "LPC177x_8x.h" 
#include "iap.h"

//const unsigned fcclk_KHz = FCCLK / 1000;
const unsigned int fcclock = 76800;
unsigned int iap_command[5];
unsigned int iap_result[3];

typedef void (*IAP)(unsigned int[], unsigned int[]);
IAP iap_entry = (IAP) IAP_LOCATION;

/*
    Подготовка секторов памяти к записи. Необходимо использовать перед операциями записи и стирания.
    @ start_sector - номер начального сектора.
    @ end_sector - номер конечного сектора, должен быть больше или равен номеру начального сектора.
*/
IAP_STATUS inap_prepare(unsigned int start_sector, unsigned int end_sector){
	unsigned int VICTmp=VICIntEnable;
    iap_command[0] = PREPARE_SECTOR;
    iap_command[1] = start_sector;
    iap_command[2] = end_sector;
	VICIntEnClr=0xFFFFFFFF;
    iap_entry(iap_command, iap_result);
	VICIntEnable=VICTmp;

    return (IAP_STATUS)iap_result[0];
}

/*
    Осуществляет запись из RAM во флеш-память.
    @ destination - адрес во флеш-памяти для записи. Должен быть на границе 256 байт.
    @ source - адрес в RAM, из которого считывать данные. Должен быть на границе слова.
    @ amount - количество байт для записи. Должно быть 256, 512, 1024 или 4096.
*/
IAP_STATUS inap_copy(unsigned int destination, unsigned int source, unsigned int amount){
	unsigned int VICTmp=VICIntEnable;
    iap_command[0] = COPY_RAM_TO_FLASH;
    iap_command[1] = destination;
    iap_command[2] = source;
    iap_command[3] = amount;
    iap_command[4] = fcclock;
	VICIntEnClr=0xFFFFFFFF;
    iap_entry(iap_command, iap_result);
	VICIntEnable=VICTmp;

    return (IAP_STATUS)iap_result[0];
}

/*
    Стирает сектор или несколько. Необходимо сначала выполнить prepare.
    @ start_sector - номер начального сектора.
    @ end_sector - номер конечного сектора, должен быть больше или равен номеру начального сектора.
*/
IAP_STATUS inap_erase(unsigned int start_sector, unsigned int end_sector){
	unsigned int VICTmp=VICIntEnable;
    iap_command[0] = ERASE_SECTOR;
    iap_command[1] = start_sector;
    iap_command[2] = end_sector;
    iap_command[3] = fcclock;
	VICIntEnClr=0xFFFFFFFF;
    iap_entry(iap_command, iap_result);
	VICIntEnable=VICTmp;

    return (IAP_STATUS)iap_result[0];
}

/*
    Проверяет секторы на пустоту.
    @ start_sector - номер начального сектора.
    @ end_sector - номер конечного сектора, должен быть больше или равен номеру начального сектора.
*/
IAP_STATUS inap_blankcheck(unsigned int start_sector, unsigned int end_sector){
	unsigned int VICTmp=VICIntEnable;
    iap_command[0] = BLANK_CHECK;
    iap_command[1] = start_sector;
    iap_command[2] = end_sector;
	VICIntEnClr=0xFFFFFFFF;
    iap_entry(iap_command,iap_result);
	VICIntEnable=VICTmp;

    return (IAP_STATUS)iap_result[0];
}

/*
    Проверяет секторы на пустоту и заполняет относительную позицию и содержимое первого непустого слова.
    @ start_sector - номер начального сектора.
    @ end_sector - номер конечного сектора, должен быть больше или равен номеру начального сектора.
    @ offset_addr - адрес, в который записать значение относительной позиции первого непустого слова.
    @ word_content - адрес, в который записать первое непустое слово.
*/
IAP_STATUS inap_blankcheck_r(unsigned int start_sector, unsigned int end_sector,
                            unsigned int* offset_addr, unsigned int* word_content){
	unsigned int VICTmp=VICIntEnable;
    iap_command[0] = BLANK_CHECK;
    iap_command[1] = start_sector;
    iap_command[2] = end_sector;
	VICIntEnClr=0xFFFFFFFF;
    iap_entry(iap_command,iap_result);
	VICIntEnable=VICTmp;

    *offset_addr = iap_result[1];
    *word_content = iap_result[2];
    return (IAP_STATUS)iap_result[0];
}

/*
    CRC-16/CCITT-FALSE
*/
unsigned short crc16(unsigned char * data, unsigned int length){
    // https://gist.github.com/tijnkooijmans/10981093
    unsigned char i;
    unsigned short wcrc = 0xFFFF;
    while(length--){
        wcrc ^= *(unsigned char*)data++ << 8;
        for (i=0; i<8; i++) wcrc = wcrc & 0x8000 ? (wcrc << 1) ^ 0x1021 : wcrc << 1;
    }
    return wcrc & 0xFFFF;
}

/*
    Производит чтение настроек из флеш-памяти в структуру
    @ asettings - указатель на структуру, в которую считать настройки
*/
void miap_read_settings(struct AdapterSettings* asettings){
    memcpy(asettings, (void*)(0x050000), sizeof(struct AdapterSettings));
}

/*
    Производит запись настроек во флеш-память из структуры
    Возвращает статус записи, описанный в enum MIAP_CODE
    @ asettings - указатель на структуру с найстройками
*/
int miap_write_settings(struct AdapterSettings* asettings){
    //unsigned int currserial = (*(unsigned int*)(0x010000));
    int status = OK;

    unsigned int *p = (unsigned int*) malloc(256);
    /*if(currserial!=0xFFFFFFF){
        asettings->Serial_Number = currserial;
    }*/
    asettings->CRC = crc16((unsigned char*)asettings, sizeof(struct AdapterSettings)-4);

    memset(p, 0xFE, 256);
    memcpy(p, asettings, sizeof(struct AdapterSettings));
    
    if(inap_blankcheck(17,17)==CMD_SUCCESS){
        if(inap_prepare(17,17)==CMD_SUCCESS){
            if(inap_copy(0x050000, (unsigned int)p, 256)!=CMD_SUCCESS)
            status = FAILED_TO_COPY;
        }else status=FAILED_TO_PREPARE_COPY;
    }else{
        if(inap_prepare(17,17)==CMD_SUCCESS){
            if(inap_erase(17,17)==CMD_SUCCESS){
                if(inap_prepare(17,17)==CMD_SUCCESS){
                if(inap_copy(0x050000, (unsigned int)p, 256)!=CMD_SUCCESS)
                    status = FAILED_TO_COPY;
                } else status = FAILED_TO_PREPARE_COPY;
            }else status = FAILED_TO_ERASE;
        }else status = FAILED_TO_PREPARE_ERASE;
    }
  
    free(p);
  
    return status;
}

void ReadCalibrFromFlash(struct Struct_Calibr* data){
    memcpy(data, (void*)(0x058000), sizeof(struct Struct_Calibr));
}

/*
    Производит запись настроек во флеш-память из структуры
    Возвращает статус записи, описанный в enum MIAP_CODE
    @ asettings - указатель на структуру с найстройками
*/
int WriteCalibrToFlash(struct Struct_Calibr* data){

    int status = OK;

    unsigned int *p = (unsigned int*) malloc(1024);

    memset(p, 0xFE, 1024);
    memcpy(p, data, sizeof(struct Struct_Calibr));  
    
	if(inap_prepare(18,18)==CMD_SUCCESS){
		if(inap_erase(18,18)==CMD_SUCCESS){
			if(inap_prepare(18,18)==CMD_SUCCESS){
                if(inap_copy(0x058000, (unsigned int)p, 1024)!=CMD_SUCCESS)
                    status = FAILED_TO_COPY;
			} else status = FAILED_TO_PREPARE_COPY;   
		}else status = FAILED_TO_ERASE;
    }else status = FAILED_TO_PREPARE_ERASE;
  
    free(p);
  
    return status;
}
