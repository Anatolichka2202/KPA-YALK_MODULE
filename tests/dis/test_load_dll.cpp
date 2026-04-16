// тест загрузчика
// проверяем загрузку библиотек
// открываем устройства
// обкатываем конфигурации
#include <windows.h>
#include <stdio.h>

int main() {
    HMODULE h = LoadLibraryA("Lusbapi.dll");
    if (!h) {
        printf("LoadLibrary failed, error code: %lu\n", GetLastError());
        // Коды ошибок: 126 = модуль не найден, 193 = неверный формат и т.д.
        return 1;
    }
    printf("Lusbapi.dll loaded successfully at address %p\n", h);

    // Попробуем получить функцию GetDllVersion
    typedef unsigned int (*GetDllVersion_t)();
    GetDllVersion_t getVer = (GetDllVersion_t)GetProcAddress(h, "GetDllVersion");
    if (!getVer) {
        printf("GetProcAddress failed for GetDllVersion\n");
    } else {
        unsigned int ver = getVer();
        printf("DLL version: %u.%u\n", ver >> 16, ver & 0xFFFF);
    }

    FreeLibrary(h);
    return 0;
}
