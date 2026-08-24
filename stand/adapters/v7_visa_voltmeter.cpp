#include "orbita_stand/v7_visa_voltmeter.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace orbita::stand {

struct V7VisaVoltmeter::Impl {
    explicit Impl(V7VisaConfig value)
        : config(std::move(value))
    {
#ifdef _WIN32
        open();
#else
        throw std::runtime_error("V7-78/1 VISA adapter is available only on Windows");
#endif
    }

    ~Impl()
    {
#ifdef _WIN32
        close();
#endif
    }

    V7VisaConfig config;
    std::string resource;

#ifdef _WIN32
    using ViStatus = std::int32_t;
    using ViSession = std::uint32_t;
    using ViFindList = std::uint32_t;
    using ViUInt32 = std::uint32_t;
    using ViAttr = std::uint32_t;
    using ViAttrState = std::uintptr_t;

    using OpenDefaultRmFn = ViStatus(WINAPI*)(ViSession*);
    using FindRsrcFn = ViStatus(WINAPI*)(
        ViSession, const char*, ViFindList*, ViUInt32*, char*);
    using OpenFn = ViStatus(WINAPI*)(
        ViSession, const char*, ViUInt32, ViUInt32, ViSession*);
    using CloseFn = ViStatus(WINAPI*)(ViSession);
    using SetAttributeFn = ViStatus(WINAPI*)(ViSession, ViAttr, ViAttrState);
    using WriteFn = ViStatus(WINAPI*)(
        ViSession, const unsigned char*, ViUInt32, ViUInt32*);
    using ReadFn = ViStatus(WINAPI*)(
        ViSession, unsigned char*, ViUInt32, ViUInt32*);

    static constexpr ViAttr timeoutAttribute = 0x3FFF001AU;
    static constexpr ViStatus success = 0;
    static constexpr ViStatus resourceNotFound = static_cast<ViStatus>(0xBFFF0011U);

    HMODULE library = nullptr;
    ViSession resourceManager = 0;
    ViSession instrument = 0;
    ViFindList findList = 0;

    OpenDefaultRmFn viOpenDefaultRM = nullptr;
    FindRsrcFn viFindRsrc = nullptr;
    OpenFn viOpen = nullptr;
    CloseFn viClose = nullptr;
    SetAttributeFn viSetAttribute = nullptr;
    WriteFn viWrite = nullptr;
    ReadFn viRead = nullptr;

    template<typename Function>
    Function loadFunction(const char* name)
    {
        const auto address = GetProcAddress(library, name);
        if (!address) {
            throw std::runtime_error(std::string("NI-VISA function is missing: ") + name);
        }
        return reinterpret_cast<Function>(address);
    }

    static void requireSuccess(ViStatus status, const char* operation)
    {
        if (status < success) {
            throw std::runtime_error(
                std::string(operation) + " failed with VISA status " + std::to_string(status));
        }
    }

    void open()
    {
        if (config.resourceExpression.empty()) {
            throw std::invalid_argument("V7-78/1 VISA resource expression is empty");
        }

        library = LoadLibraryExW(
            L"visa64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) {
            library = LoadLibraryExW(
                L"visa32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
        if (!library) {
            throw std::runtime_error("NI-VISA runtime DLL was not found in Windows System32");
        }

        try {
            viOpenDefaultRM = loadFunction<OpenDefaultRmFn>("viOpenDefaultRM");
            viFindRsrc = loadFunction<FindRsrcFn>("viFindRsrc");
            viOpen = loadFunction<OpenFn>("viOpen");
            viClose = loadFunction<CloseFn>("viClose");
            viSetAttribute = loadFunction<SetAttributeFn>("viSetAttribute");
            viWrite = loadFunction<WriteFn>("viWrite");
            viRead = loadFunction<ReadFn>("viRead");

            requireSuccess(viOpenDefaultRM(&resourceManager), "viOpenDefaultRM");

            char descriptor[256]{};
            ViUInt32 foundCount = 0;
            const auto findStatus = viFindRsrc(
                resourceManager,
                config.resourceExpression.c_str(),
                &findList,
                &foundCount,
                descriptor);
            if (findStatus == resourceNotFound) {
                throw std::runtime_error(
                    "V7-78/1 was not found by NI-VISA (VID 164E, PID 0DAD)");
            }
            requireSuccess(findStatus, "viFindRsrc(V7-78/1)");
            if (foundCount == 0 || descriptor[0] == '\0') {
                throw std::runtime_error("V7-78/1 was not found by NI-VISA");
            }
            resource = descriptor;

            requireSuccess(
                viOpen(resourceManager, resource.c_str(), 0, 0, &instrument),
                "viOpen(V7-78/1)");
            requireSuccess(
                viSetAttribute(
                    instrument,
                    timeoutAttribute,
                    static_cast<ViAttrState>(config.timeoutMilliseconds)),
                "viSetAttribute(VI_ATTR_TMO_VALUE)");

            if (findList != 0) {
                viClose(findList);
                findList = 0;
            }
        } catch (...) {
            close();
            throw;
        }
    }

    void close() noexcept
    {
        if (viClose) {
            if (instrument != 0) {
                viClose(instrument);
                instrument = 0;
            }
            if (findList != 0) {
                viClose(findList);
                findList = 0;
            }
            if (resourceManager != 0) {
                viClose(resourceManager);
                resourceManager = 0;
            }
        }
        if (library) {
            FreeLibrary(library);
            library = nullptr;
        }
    }

    double readVoltage()
    {
        static constexpr char command[] = "READ?";
        ViUInt32 written = 0;
        requireSuccess(
            viWrite(
                instrument,
                reinterpret_cast<const unsigned char*>(command),
                static_cast<ViUInt32>(sizeof(command) - 1),
                &written),
            "viWrite(READ?)");
        if (written != sizeof(command) - 1) {
            throw std::runtime_error("NI-VISA wrote an incomplete READ? command");
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(config.readDelayMilliseconds));

        unsigned char buffer[64]{};
        ViUInt32 received = 0;
        requireSuccess(
            viRead(instrument, buffer, sizeof(buffer) - 1, &received),
            "viRead(V7-78/1)");
        if (received == 0) {
            throw std::runtime_error("V7-78/1 returned an empty reading");
        }
        buffer[received] = '\0';

        const std::string response(reinterpret_cast<const char*>(buffer), received);
        std::size_t consumed = 0;
        const double value = std::stod(response, &consumed);
        if (consumed == 0 || !std::isfinite(value)) {
            throw std::runtime_error("V7-78/1 returned an invalid numeric reading");
        }
        return value;
    }
#endif
};

V7VisaVoltmeter::V7VisaVoltmeter(V7VisaConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

V7VisaVoltmeter::~V7VisaVoltmeter() = default;
V7VisaVoltmeter::V7VisaVoltmeter(V7VisaVoltmeter&&) noexcept = default;
V7VisaVoltmeter& V7VisaVoltmeter::operator=(V7VisaVoltmeter&&) noexcept = default;

double V7VisaVoltmeter::readVoltage()
{
#ifdef _WIN32
    return impl_->readVoltage();
#else
    throw std::runtime_error("V7-78/1 VISA adapter is available only on Windows");
#endif
}

const std::string& V7VisaVoltmeter::resourceName() const
{
    return impl_->resource;
}

} // namespace orbita::stand
