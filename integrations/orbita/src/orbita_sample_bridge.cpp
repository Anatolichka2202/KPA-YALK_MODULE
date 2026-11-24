#include "miltech/orbita_sample_bridge.h"

#include "orbita.h"
#include "orbita_stand/sample_source.h"

#include <exception>
#include <utility>

namespace miltech::integration {

OrbitaSampleBridge::OrbitaSampleBridge(
    orbita::stand::ISampleSource& source,
    orbita::Orbita& decoder)
    : source_(source), decoder_(decoder)
{
}

OrbitaSampleBridge::~OrbitaSampleBridge()
{
    stop();
}

bool OrbitaSampleBridge::start()
{
    if (running_.load()) return true;

    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_.clear();
    }

    source_.setSamplesCallback([this](const std::vector<int16_t>& samples) {
        try {
            decoder_.pushSamples(samples);
        } catch (const std::exception& error) {
            recordError(std::string("Orbita sample delivery failed: ") + error.what());
        } catch (...) {
            recordError("Orbita sample delivery failed: unknown error");
        }
    });
    source_.setErrorCallback([this](const std::string& message) {
        recordError(message);
    });

    try {
        // A station-integrated decoder is started without selecting physical
        // acquisition hardware inside liborbita. The source below is the sole
        // producer for this bridge.
        decoder_.start();
    } catch (const std::exception& error) {
        recordError(std::string("Orbita decoder start failed: ") + error.what());
        detachCallbacks();
        return false;
    } catch (...) {
        recordError("Orbita decoder start failed: unknown error");
        detachCallbacks();
        return false;
    }

    try {
        if (!source_.start()) {
            recordError("Station sample source failed to start");
            source_.stop();
            detachCallbacks();
            decoder_.stop();
            return false;
        }
    } catch (const std::exception& error) {
        recordError(std::string("Station sample source start failed: ") + error.what());
        source_.stop();
        detachCallbacks();
        decoder_.stop();
        return false;
    } catch (...) {
        recordError("Station sample source start failed: unknown error");
        source_.stop();
        detachCallbacks();
        decoder_.stop();
        return false;
    }

    running_ = true;
    return true;
}

void OrbitaSampleBridge::stop() noexcept
{
    // ISampleSource::stop() is noexcept by contract and must stop/join its
    // producer before callbacks are detached.
    source_.stop();
    detachCallbacks();
    try {
        decoder_.stop();
    } catch (...) {
        // Teardown is best-effort/noexcept at the integration boundary.
    }
    running_ = false;
}

bool OrbitaSampleBridge::isRunning() const noexcept
{
    return running_.load() && source_.isRunning() && decoder_.isRunning();
}

void OrbitaSampleBridge::setErrorCallback(ErrorCallback callback)
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    errorCallback_ = std::move(callback);
}

std::string OrbitaSampleBridge::lastError() const
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

void OrbitaSampleBridge::recordError(const std::string& message) noexcept
{
    ErrorCallback callback;
    try {
        {
            std::lock_guard<std::mutex> lock(errorMutex_);
            lastError_ = message;
            callback = errorCallback_;
        }
        if (callback) callback(message);
    } catch (...) {
        // Error reporting from an acquisition thread must never unwind it.
    }
}

void OrbitaSampleBridge::detachCallbacks() noexcept
{
    try {
        source_.setSamplesCallback({});
    } catch (...) {
    }
    try {
        source_.setErrorCallback({});
    } catch (...) {
    }
}

} // namespace miltech::integration
