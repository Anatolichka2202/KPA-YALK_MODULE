#include "e2010_sample_source.h"

#include <stdexcept>
#include <utility>

namespace orbita::stand {
namespace {

int integerValue(const std::map<std::string, std::string>& configuration,
                 const std::string& key, int fallback)
{
    const auto iterator = configuration.find(key);
    if (iterator == configuration.end()) return fallback;
    return std::stoi(iterator->second);
}

double doubleValue(const std::map<std::string, std::string>& configuration,
                   const std::string& key, double fallback)
{
    const auto iterator = configuration.find(key);
    if (iterator == configuration.end()) return fallback;
    return std::stod(iterator->second);
}

std::size_t sizeValue(const std::map<std::string, std::string>& configuration,
                      const std::string& key, std::size_t fallback)
{
    const auto iterator = configuration.find(key);
    if (iterator == configuration.end()) return fallback;
    return static_cast<std::size_t>(std::stoull(iterator->second));
}

} // namespace

E2010SampleSource::E2010SampleSource(std::map<std::string, std::string> configuration)
    : configuration_(std::move(configuration))
{
    slot_ = integerValue(configuration_, "slot", 0);
    channel_ = integerValue(configuration_, "channel", 0);
    sampleRateKHz_ = doubleValue(configuration_, "sample_rate_khz", 10000.0);
    bufferWords_ = sizeValue(configuration_, "buffer_words", 1024u * 1024u);

    if (slot_ < 0) throw std::invalid_argument("E2010 slot must be non-negative");
    if (channel_ < 0 || channel_ >= 4) {
        throw std::invalid_argument("E2010 channel must be in range 0..3");
    }
    if (sampleRateKHz_ <= 0.0) {
        throw std::invalid_argument("E2010 sample_rate_khz must be positive");
    }
    if (bufferWords_ == 0) {
        throw std::invalid_argument("E2010 buffer_words must be positive");
    }
}

E2010SampleSource::~E2010SampleSource()
{
    close();
}

void E2010SampleSource::setSamplesCallback(SamplesCallback callback)
{
    samplesCallback_ = std::move(callback);
}

void E2010SampleSource::setErrorCallback(ErrorCallback callback)
{
    errorCallback_ = std::move(callback);
}

void E2010SampleSource::notifyError(const std::string& message) noexcept
{
    try {
        if (errorCallback_) errorCallback_(message);
    } catch (...) {
        // Hardware error propagation must not unwind the acquisition thread.
    }
}

#ifdef _WIN32

bool E2010SampleSource::configureHardware()
{
    if (!module_) return false;

    ADC_PARS_E2010 parameters{};
    module_->GET_ADC_PARS(&parameters);

    if (!module_->GET_MODULE_DESCRIPTION(&moduleDescription_)) {
        notifyError("E2010: GET_MODULE_DESCRIPTION failed");
        return false;
    }

    for (int range = 0; range < ADC_INPUT_RANGES_QUANTITY_E2010; ++range) {
        for (int channel = 0; channel < ADC_CHANNELS_QUANTITY_E2010; ++channel) {
            const int index = channel + range * ADC_CHANNELS_QUANTITY_E2010;
            parameters.AdcOffsetCoefs[range][channel] =
                moduleDescription_.Adc.OffsetCalibration[index];
            parameters.AdcScaleCoefs[range][channel] =
                moduleDescription_.Adc.ScaleCalibration[index];
        }
    }

    parameters.IsAdcCorrectionEnabled = TRUE;
    parameters.SynchroPars.StartSource = INT_ADC_START_E2010;
    parameters.SynchroPars.SynhroSource = INT_ADC_CLOCK_E2010;
    parameters.OverloadMode = CLIPPING_OVERLOAD_E2010;
    parameters.ChannelsQuantity = 1;
    parameters.ControlTable[0] = channel_;
    parameters.AdcRate = sampleRateKHz_;
    parameters.InterKadrDelay = 0.0;
    parameters.InputRange[0] = ADC_INPUT_RANGE_3000mV_E2010;
    parameters.InputSwitch[0] = ADC_INPUT_SIGNAL_E2010;
    parameters.SynchroPars.StartDelay = 0;
    parameters.SynchroPars.StopAfterNKadrs = 0;
    parameters.SynchroPars.SynchroAdMode = NO_ANALOG_SYNCHRO_E2010;
    parameters.SynchroPars.SynchroAdChannel = 0;
    parameters.SynchroPars.SynchroAdPorog = 0;
    parameters.SynchroPars.IsBlockDataMarkerEnabled = 0;

    if (!module_->SET_ADC_PARS(&parameters)) {
        notifyError("E2010: SET_ADC_PARS failed");
        return false;
    }

    adcParams_ = parameters;
    for (auto& buffer : buffers_) buffer.resize(bufferWords_);
    module_->STOP_ADC();
    return true;
}

void E2010SampleSource::releaseHardware() noexcept
{
    if (module_) {
        module_->STOP_ADC();
        module_->CloseLDevice();
        module_->ReleaseLInstance();
        module_ = nullptr;
    }
    moduleHandle_ = nullptr;
    createInstance_ = nullptr;
    getDllVersion_ = nullptr;
    if (lusbapiDll_) {
        FreeLibrary(lusbapiDll_);
        lusbapiDll_ = nullptr;
    }
}

void E2010SampleSource::closeIoEvents() noexcept
{
    for (auto& overlap : overlap_) {
        if (overlap.hEvent) CloseHandle(overlap.hEvent);
        overlap.hEvent = nullptr;
    }
    if (stopEvent_) CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

bool E2010SampleSource::open()
{
    if (open_.load()) return true;

    lusbapiDll_ = LoadLibraryA("Lusbapi64.dll");
    if (!lusbapiDll_) {
        notifyError("E2010: failed to load Lusbapi64.dll");
        return false;
    }

    getDllVersion_ = reinterpret_cast<GetDllVersionFn>(
        GetProcAddress(lusbapiDll_, "GetDllVersion"));
    createInstance_ = reinterpret_cast<CreateInstanceFn>(
        GetProcAddress(lusbapiDll_, "CreateLInstance"));
    if (!getDllVersion_ || !createInstance_) {
        notifyError("E2010: required Lusbapi functions are missing");
        releaseHardware();
        return false;
    }

    if (getDllVersion_() != CURRENT_VERSION_LUSBAPI) {
        notifyError("E2010: incompatible Lusbapi64.dll version");
        releaseHardware();
        return false;
    }

    module_ = createInstance_("e2010");
    if (!module_) {
        notifyError("E2010: CreateLInstance failed");
        releaseHardware();
        return false;
    }

    if (!module_->OpenLDevice(slot_)) {
        notifyError("E2010: OpenLDevice failed for slot " + std::to_string(slot_));
        releaseHardware();
        return false;
    }

    moduleHandle_ = module_->GetModuleHandle();
    if (!moduleHandle_) {
        notifyError("E2010: GetModuleHandle failed");
        releaseHardware();
        return false;
    }

    if (!module_->LOAD_MODULE(nullptr)) {
        notifyError("E2010: LOAD_MODULE failed");
        releaseHardware();
        return false;
    }

    if (!configureHardware()) {
        releaseHardware();
        return false;
    }

    open_ = true;
    return true;
}

bool E2010SampleSource::start()
{
    if (running_.load()) return true;
    if (!open() || !module_) return false;

    closeIoEvents();
    stopEvent_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) {
        notifyError("E2010: cannot create stop event");
        return false;
    }

    for (int index = 0; index < 2; ++index) {
        ZeroMemory(&overlap_[index], sizeof(OVERLAPPED));
        overlap_[index].hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!overlap_[index].hEvent) {
            notifyError("E2010: cannot create I/O event");
            closeIoEvents();
            return false;
        }

        requests_[index].Buffer = buffers_[index].data();
        requests_[index].NumberOfWordsToPass = static_cast<DWORD>(bufferWords_);
        requests_[index].NumberOfWordsPassed = 0;
        requests_[index].Overlapped = &overlap_[index];
        requests_[index].TimeOut =
            static_cast<DWORD>(static_cast<double>(bufferWords_) / adcParams_.AdcRate) + 1000u;
    }

    requestNumber_ = 0;
    if (!module_->ReadData(&requests_[requestNumber_])) {
        notifyError("E2010: initial ReadData failed");
        closeIoEvents();
        return false;
    }

    if (!module_->START_ADC()) {
        notifyError("E2010: START_ADC failed");
        closeIoEvents();
        return false;
    }

    stopRequested_ = false;
    running_ = true;
    readerThread_ = std::thread(&E2010SampleSource::readerLoop, this);
    return true;
}

void E2010SampleSource::stop() noexcept
{
    if (!running_.exchange(false)) {
        if (readerThread_.joinable()) readerThread_.join();
        return;
    }

    stopRequested_ = true;
    if (stopEvent_) SetEvent(stopEvent_);
    if (readerThread_.joinable()) readerThread_.join();
    if (module_) module_->STOP_ADC();
    closeIoEvents();
}

void E2010SampleSource::close() noexcept
{
    stop();
    releaseHardware();
    open_ = false;
}

void E2010SampleSource::readerLoop()
{
    DWORD bytesTransferred = 0;

    while (!stopRequested_.load()) {
        requestNumber_ ^= 1;

        if (!module_->ReadData(&requests_[requestNumber_])) {
            notifyError("E2010: ReadData failed");
            break;
        }

        HANDLE waitObjects[2] = {
            requests_[requestNumber_ ^ 1].Overlapped->hEvent,
            stopEvent_
        };
        const DWORD waitResult = WaitForMultipleObjects(
            2, waitObjects, FALSE, requests_[requestNumber_ ^ 1].TimeOut);

        if (waitResult == WAIT_OBJECT_0) {
            if (!GetOverlappedResult(
                    moduleHandle_, requests_[requestNumber_ ^ 1].Overlapped,
                    &bytesTransferred, FALSE)) {
                notifyError("E2010: GetOverlappedResult failed, code="
                            + std::to_string(GetLastError()));
                break;
            }

            const auto words = static_cast<std::size_t>(bytesTransferred / sizeof(int16_t));
            if (words > 0 && samplesCallback_) {
                const auto* begin = buffers_[requestNumber_ ^ 1].data();
                samplesCallback_(std::vector<int16_t>(begin, begin + words));
            }
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            break;
        } else if (waitResult == WAIT_TIMEOUT) {
            notifyError("E2010: data wait timeout");
        } else {
            notifyError("E2010: WaitForMultipleObjects failed");
            break;
        }
    }

    if (module_) module_->STOP_ADC();
    running_ = false;
}

#else

bool E2010SampleSource::open()
{
    notifyError("E2010 sample source is available only on Windows");
    return false;
}

bool E2010SampleSource::start()
{
    notifyError("E2010 sample source is available only on Windows");
    return false;
}

void E2010SampleSource::stop() noexcept
{
    running_ = false;
    if (readerThread_.joinable()) readerThread_.join();
}

void E2010SampleSource::close() noexcept
{
    stop();
    open_ = false;
}

void E2010SampleSource::readerLoop()
{
}

#endif

std::unique_ptr<ISampleSource> createSampleSource(
    const std::string& provider,
    const std::map<std::string, std::string>& configuration)
{
    if (provider == "miltech.sample.e2010") {
        return std::make_unique<E2010SampleSource>(configuration);
    }
    throw std::invalid_argument("Unknown sample-source provider: " + provider);
}

} // namespace orbita::stand
