#include "visa_device.h"
#include <cstring>
#include <cstdio>

VISADevice::VISADevice()
    : visa_handle(nullptr), default_rm(VI_NULL), instrument(VI_NULL), is_open(false)
{
    info_string = "VISA device - not opened";
}

VISADevice::~VISADevice() {
    close();
    unloadVisaLibrary();
}

bool VISADevice::loadVisaLibrary() {
    visa_handle = LoadLibraryA("visa32.dll");
    if (!visa_handle) return false;
    return true;
}

void VISADevice::unloadVisaLibrary() {
    if (visa_handle) {
        FreeLibrary(visa_handle);
        visa_handle = nullptr;
    }
}

bool VISADevice::open(const char* resource) {
    if (!loadVisaLibrary()) {
        info_string = "Failed to load visa32.dll";
        return false;
    }

    // Получаем функции
    viOpenDefaultRM_t viOpenDefaultRM = (viOpenDefaultRM_t)GetProcAddress(visa_handle, "viOpenDefaultRM");
    viOpen_t viOpen = (viOpen_t)GetProcAddress(visa_handle, "viOpen");
    viSetAttribute_t viSetAttribute = (viSetAttribute_t)GetProcAddress(visa_handle, "viSetAttribute");

    if (!viOpenDefaultRM || !viOpen || !viSetAttribute) {
        info_string = "Failed to get VISA functions";
        return false;
    }

    // Открываем ресурс-менеджер
    if (viOpenDefaultRM(&default_rm) != VI_SUCCESS) {
        info_string = "Failed to open VISA resource manager";
        return false;
    }

    // Открываем прибор
    if (viOpen(default_rm, (ViChar*)resource, VI_NULL, VI_NULL, &instrument) != VI_SUCCESS) {
        info_string = "Failed to open instrument";
        return false;
    }

    // Устанавливаем таймаут (5 секунд)
    viSetAttribute(instrument, 0x3FFF001A, 5000); // VI_ATTR_TMO_VALUE

    is_open = true;
    info_string = "VISA opened: ";
    info_string += resource;
    return true;
}

void VISADevice::close() {
    if (instrument != VI_NULL) {
        viClose_t viClose = (viClose_t)GetProcAddress(visa_handle, "viClose");
        if (viClose) viClose(instrument);
        instrument = VI_NULL;
    }
    if (default_rm != VI_NULL) {
        viClose_t viClose = (viClose_t)GetProcAddress(visa_handle, "viClose");
        if (viClose) viClose(default_rm);
        default_rm = VI_NULL;
    }
    is_open = false;
}

std::string VISADevice::query(const char* command) {
    if (!is_open) return "";
    write(command);
    return read();
}

bool VISADevice::write(const char* command) {
    if (!is_open) return false;

    viWrite_t viWrite = (viWrite_t)GetProcAddress(visa_handle, "viWrite");
    if (!viWrite) return false;

    ViUInt32 written;
    ViStatus status = viWrite(instrument, (ViChar*)command, (ViUInt32)strlen(command), &written);
    return status == VI_SUCCESS;
}

std::string VISADevice::read() {
    if (!is_open) return "";

    viRead_t viRead = (viRead_t)GetProcAddress(visa_handle, "viRead");
    if (!viRead) return "";

    char buffer[1024] = {0};
    ViUInt32 count;
    ViStatus status = viRead(instrument, (ViChar*)buffer, sizeof(buffer) - 1, &count);
    if (status != VI_SUCCESS) return "";

    return std::string(buffer, count);
}

bool VISADevice::init() {
    return open("USB0::0x164E::0x0DAD::...::INSTR"); // Пример, реальный ресурс нужно задать
}

bool VISADevice::startStream() { return false; }
bool VISADevice::stopStream() { return false; }
int VISADevice::readSamples(int16_t*, int, int) { return -1; }
bool VISADevice::setParam(const char*, double) { return false; }
const char* VISADevice::getInfo() const { return info_string.c_str(); }
