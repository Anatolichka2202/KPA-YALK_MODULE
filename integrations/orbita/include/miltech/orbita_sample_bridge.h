#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace orbita {
class Orbita;
namespace stand {
class ISampleSource;
}
} // namespace orbita

namespace miltech::integration {

// Narrow integration boundary between a station-owned raw sample source and
// the Orbita protocol decoder. Neither side needs to know the concrete type of
// the other side's hardware/provider.
class OrbitaSampleBridge final {
public:
    using ErrorCallback = std::function<void(const std::string&)>;

    OrbitaSampleBridge(orbita::stand::ISampleSource& source, orbita::Orbita& decoder);
    ~OrbitaSampleBridge();

    OrbitaSampleBridge(const OrbitaSampleBridge&) = delete;
    OrbitaSampleBridge& operator=(const OrbitaSampleBridge&) = delete;

    // Starts liborbita in decode-only mode first and only then starts the
    // station sample producer. Returns false on any startup failure; details
    // are available through lastError().
    bool start();

    // Stops the producer before the decoder so no callback can feed a decoder
    // that is already shutting down.
    void stop() noexcept;

    bool isRunning() const noexcept;

    void setErrorCallback(ErrorCallback callback);
    std::string lastError() const;

private:
    void recordError(const std::string& message) noexcept;
    void detachCallbacks() noexcept;

    orbita::stand::ISampleSource& source_;
    orbita::Orbita& decoder_;
    std::atomic_bool running_{false};

    mutable std::mutex errorMutex_;
    std::string lastError_;
    ErrorCallback errorCallback_;
};

} // namespace miltech::integration
