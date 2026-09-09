#include "orbita_stand/visa_instrument.h"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace orbita::stand {

struct VisaInstrument::Impl {
    explicit Impl(VisaInstrumentConfig value) : config(std::move(value))
    {
#ifdef _WIN32
        open();
#else
        throw std::runtime_error("VISA equipment adapters are available only on Windows");
#endif
    }
    ~Impl() {
#ifdef _WIN32
        close();
#endif
    }

    VisaInstrumentConfig config;
    std::string resource;

#ifdef _WIN32
    using ViStatus = std::int32_t;
    using ViSession = std::uint32_t;
    using ViFindList = std::uint32_t;
    using ViUInt32 = std::uint32_t;
    using ViAttr = std::uint32_t;
    using ViAttrState = std::uintptr_t;
    using OpenDefaultRmFn = ViStatus(WINAPI*)(ViSession*);
    using FindRsrcFn = ViStatus(WINAPI*)(ViSession, const char*, ViFindList*, ViUInt32*, char*);
    using OpenFn = ViStatus(WINAPI*)(ViSession, const char*, ViUInt32, ViUInt32, ViSession*);
    using CloseFn = ViStatus(WINAPI*)(ViSession);
    using SetAttributeFn = ViStatus(WINAPI*)(ViSession, ViAttr, ViAttrState);
    using WriteFn = ViStatus(WINAPI*)(ViSession, const unsigned char*, ViUInt32, ViUInt32*);
    using ReadFn = ViStatus(WINAPI*)(ViSession, unsigned char*, ViUInt32, ViUInt32*);

    static constexpr ViAttr timeoutAttribute = 0x3FFF001AU;
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

    template<typename Function> Function loadFunction(const char* name)
    {
        const auto address = GetProcAddress(library, name);
        if (!address) throw std::runtime_error(std::string("NI-VISA function is missing: ") + name);
        return reinterpret_cast<Function>(address);
    }
    static void requireSuccess(ViStatus status, const char* operation)
    {
        if (status < 0) throw std::runtime_error(std::string(operation)
            + " failed with VISA status " + std::to_string(status));
    }
    void open()
    {
        if (config.resourceExpressions.empty()) throw std::invalid_argument("VISA resource list is empty");
        library = LoadLibraryExW(L"visa64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) library = LoadLibraryExW(L"visa32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) throw std::runtime_error("NI-VISA runtime DLL was not found in Windows System32");
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
            for (const auto& expression : config.resourceExpressions) {
                if (expression.empty()) continue;
                ViUInt32 foundCount = 0;
                findList = 0;
                const auto status = viFindRsrc(resourceManager, expression.c_str(), &findList,
                                               &foundCount, descriptor);
                if (status == resourceNotFound) continue;
                requireSuccess(status, "viFindRsrc");
                if (foundCount && descriptor[0]) break;
                if (findList) { viClose(findList); findList = 0; }
                descriptor[0] = '\0';
            }
            if (!descriptor[0]) throw std::runtime_error("VISA instrument was not found");
            resource = descriptor;
            requireSuccess(viOpen(resourceManager, resource.c_str(), 0, 0, &instrument), "viOpen");
            requireSuccess(viSetAttribute(instrument, timeoutAttribute, config.timeoutMilliseconds),
                           "viSetAttribute(VI_ATTR_TMO_VALUE)");
            if (findList) { viClose(findList); findList = 0; }
        } catch (...) { close(); throw; }
    }
    void close() noexcept
    {
        if (viClose) {
            if (instrument) viClose(instrument);
            if (findList) viClose(findList);
            if (resourceManager) viClose(resourceManager);
        }
        instrument = resourceManager = findList = 0;
        if (library) FreeLibrary(library);
        library = nullptr;
    }
    void write(const std::string& command)
    {
        ViUInt32 written = 0;
        requireSuccess(viWrite(instrument, reinterpret_cast<const unsigned char*>(command.data()),
                               static_cast<ViUInt32>(command.size()), &written), "viWrite");
        if (written != command.size()) throw std::runtime_error("VISA wrote an incomplete command");
    }
    std::string query(const std::string& command, unsigned delayMilliseconds)
    {
        write(command);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMilliseconds));
        const auto bytes = readRaw(1024);
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }
    std::vector<std::uint8_t> readRaw(std::size_t maximumBytes)
    {
        if (!maximumBytes) throw std::invalid_argument("VISA read buffer must not be empty");
        std::vector<std::uint8_t> buffer(maximumBytes);
        ViUInt32 received = 0;
        requireSuccess(viRead(instrument, buffer.data(), static_cast<ViUInt32>(buffer.size()), &received), "viRead");
        if (!received) throw std::runtime_error("VISA instrument returned an empty response");
        buffer.resize(received);
        return buffer;
    }
#endif
};

VisaInstrument::VisaInstrument(VisaInstrumentConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
VisaInstrument::~VisaInstrument() = default;
VisaInstrument::VisaInstrument(VisaInstrument&&) noexcept = default;
VisaInstrument& VisaInstrument::operator=(VisaInstrument&&) noexcept = default;
void VisaInstrument::write(const std::string& command) {
#ifdef _WIN32
    impl_->write(command);
#else
    (void)command; throw std::runtime_error("VISA adapter is available only on Windows");
#endif
}
std::vector<std::uint8_t> VisaInstrument::readRaw(std::size_t maximumBytes) {
#ifdef _WIN32
    return impl_->readRaw(maximumBytes);
#else
    (void)maximumBytes; throw std::runtime_error("VISA adapter is available only on Windows");
#endif
}
std::string VisaInstrument::query(const std::string& command, unsigned delayMilliseconds) {
#ifdef _WIN32
    return impl_->query(command, delayMilliseconds);
#else
    (void)command; (void)delayMilliseconds; throw std::runtime_error("VISA adapter is available only on Windows");
#endif
}
std::vector<std::uint8_t> VisaInstrument::queryRaw(
    const std::string& command, std::size_t maximumBytes, unsigned delayMilliseconds) {
#ifdef _WIN32
    impl_->write(command);
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMilliseconds));
    return impl_->readRaw(maximumBytes);
#else
    (void)command; (void)maximumBytes; (void)delayMilliseconds;
    throw std::runtime_error("VISA adapter is available only on Windows");
#endif
}
const std::string& VisaInstrument::resourceName() const { return impl_->resource; }

} // namespace orbita::stand
