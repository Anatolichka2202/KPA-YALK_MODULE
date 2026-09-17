#ifndef IAP_H
#define IAP_H

#define IAP_LOCATION 0x7ffffff1

#define CRP1 0x12345678
#define CRP2 0x87654321
#define CRP3 0x34218765

typedef enum IAP_CODE_COMMAND{
    PREPARE_SECTOR = 50,
    COPY_RAM_TO_FLASH = 51,
    ERASE_SECTOR = 52,
    BLANK_CHECK = 53,
    READ_PART_ID = 54,
    READ_BOOT_CODE = 55,
    COMPARE = 56,
    REINVOKE_ISP = 57
} IAP_COMMAND;

typedef enum IAP_CODE_STATUS{
    CMD_SUCCESS,
    INVALID_COMMAND,
    SRC_ADDR_ERROR,
    DST_ADDR_ERROR,
    SRC_ADDR_NOT_MAPPED,
    DST_ADDR_NOT_MAPPED,
    COUNT_ERROR,
    INVALID_SECTOR,
    SECTOR_NOT_BLANK,
    SECTOR_NOT_PREPARED_FOR_WRITE_OPERATION,
    COMPARE_ERROR,
    BUSY
} IAP_STATUS;

enum MIAP_CODE{
    OK,
    FAILED_TO_PREPARE_ERASE,
    FAILED_TO_ERASE,
    FAILED_TO_PREPARE_COPY,
    FAILED_TO_COPY,
    EMPTY
};

struct AdapterSettings{
    unsigned int Serial_Number;
    unsigned int MACAddress;
    unsigned int IPAddress;
    unsigned int SubnetMask;
    unsigned int ListeningPort;
    unsigned int CRC;
};

#pragma pack(push, 1)
struct Struct_Analog
{
	unsigned char work;
	float val_volt;
	unsigned short val; 
	unsigned char setupAsCode; 					//1 - настраивать по коду, 0 - по вольтам
	unsigned char connectToBus;
};

struct Struct_State
{
	unsigned char marker;
	unsigned char id;
	unsigned int IP;
	unsigned short Port;
	struct Struct_Analog Analog[96];
	unsigned char Digital[64];
};

struct Struct_Calibr
{
	float k[96];
	short b[96];
};
#pragma pack(pop)

unsigned short crc16(unsigned char * data, unsigned int length);

void miap_read_settings(struct AdapterSettings* asettings);
int miap_write_settings(struct AdapterSettings* asettings);
void ReadCalibrFromFlash(struct Struct_Calibr* data);
int WriteCalibrToFlash(struct Struct_Calibr* data);
void ReadCRCFromFlash(unsigned short* crc);
#endif
