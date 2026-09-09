#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
#ifndef _WIN32
    std::cerr << "VISA discovery is available only on Windows\n";
    return EXIT_FAILURE;
#else
    using ViStatus = std::int32_t;
    using ViSession = std::uint32_t;
    using ViFindList = std::uint32_t;
    using ViUInt32 = std::uint32_t;
    using OpenDefaultRmFn = ViStatus(WINAPI*)(ViSession*);
    using FindRsrcFn = ViStatus(WINAPI*)(ViSession, const char*, ViFindList*, ViUInt32*, char*);
    using FindNextFn = ViStatus(WINAPI*)(ViFindList, char*);
    using CloseFn = ViStatus(WINAPI*)(ViSession);
    constexpr ViStatus resourceNotFound = static_cast<ViStatus>(0xBFFF0011U);

    HMODULE library = LoadLibraryExW(L"visa64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) library = LoadLibraryExW(L"visa32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) {
        std::cerr << "ERROR NI-VISA runtime DLL was not found in Windows System32\n";
        return EXIT_FAILURE;
    }
    const auto function = [library](const char* name) {
        const auto address = GetProcAddress(library, name);
        if (!address) throw std::runtime_error(std::string("Missing VISA function: ") + name);
        return address;
    };
    ViSession resourceManager = 0;
    ViFindList findList = 0;
    try {
        const auto openDefault = reinterpret_cast<OpenDefaultRmFn>(function("viOpenDefaultRM"));
        const auto find = reinterpret_cast<FindRsrcFn>(function("viFindRsrc"));
        const auto next = reinterpret_cast<FindNextFn>(function("viFindNext"));
        const auto close = reinterpret_cast<CloseFn>(function("viClose"));
        if (const auto status = openDefault(&resourceManager); status < 0) {
            throw std::runtime_error("viOpenDefaultRM failed: " + std::to_string(status));
        }
        char descriptor[256]{};
        ViUInt32 count = 0;
        const auto status = find(resourceManager, "?*INSTR", &findList, &count, descriptor);
        if (status == resourceNotFound) {
            std::cout << "VISA_RESOURCES 0\n";
        } else if (status < 0) {
            throw std::runtime_error("viFindRsrc failed: " + std::to_string(status));
        } else {
            std::cout << "VISA_RESOURCES " << count << '\n';
            if (count) std::cout << "RESOURCE " << descriptor << '\n';
            for (ViUInt32 index = 1; index < count; ++index) {
                if (const auto nextStatus = next(findList, descriptor); nextStatus < 0) {
                    throw std::runtime_error("viFindNext failed: " + std::to_string(nextStatus));
                }
                std::cout << "RESOURCE " << descriptor << '\n';
            }
        }
        if (findList) close(findList);
        if (resourceManager) close(resourceManager);
        FreeLibrary(library);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        const auto close = reinterpret_cast<CloseFn>(GetProcAddress(library, "viClose"));
        if (close) {
            if (findList) close(findList);
            if (resourceManager) close(resourceManager);
        }
        FreeLibrary(library);
        std::cerr << "ERROR " << error.what() << '\n';
        return EXIT_FAILURE;
    }
#endif
}
