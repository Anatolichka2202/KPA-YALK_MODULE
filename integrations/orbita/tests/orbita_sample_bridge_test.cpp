#include "miltech/orbita_sample_bridge.h"

#include "orbita.h"
#include "orbita_stand/sample_source.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class FakeSampleSource final : public orbita::stand::ISampleSource {
public:
    bool open() override
    {
        open_ = true;
        return true;
    }

    void close() noexcept override
    {
        stop();
        open_ = false;
    }

    bool isOpen() const noexcept override { return open_; }

    bool start() override
    {
        ++startCalls;
        if (failStart) return false;
        open();
        running_ = true;
        return true;
    }

    void stop() noexcept override
    {
        ++stopCalls;
        running_ = false;
    }

    bool isRunning() const noexcept override { return running_; }

    void setSamplesCallback(SamplesCallback callback) override
    {
        samplesCallback_ = std::move(callback);
    }

    void setErrorCallback(ErrorCallback callback) override
    {
        errorCallback_ = std::move(callback);
    }

    void emitSamples(const std::vector<int16_t>& samples)
    {
        if (samplesCallback_) samplesCallback_(samples);
    }

    void emitError(const std::string& message)
    {
        if (errorCallback_) errorCallback_(message);
    }

    bool hasSamplesCallback() const { return static_cast<bool>(samplesCallback_); }
    bool hasErrorCallback() const { return static_cast<bool>(errorCallback_); }

    bool failStart = false;
    int startCalls = 0;
    int stopCalls = 0;

private:
    bool open_ = false;
    bool running_ = false;
    SamplesCallback samplesCallback_;
    ErrorCallback errorCallback_;
};

orbita::Orbita configuredDecoder()
{
    orbita::Orbita decoder;
    // This address is part of the repository's confirmed Orbita address set
    // data/catalog/address_sets/ubsi_yvp_fast.txt.
    decoder.setChannels({{"M16P1A11B21T21", "test", "test"}});
    if (decoder.getChannels().size() != 1) {
        throw std::runtime_error("test Orbita address was not accepted");
    }
    return decoder;
}

void lifecycleContract()
{
    auto decoder = configuredDecoder();
    FakeSampleSource source;
    miltech::integration::OrbitaSampleBridge bridge(source, decoder);

    require(bridge.start(), "bridge failed to start");
    require(bridge.isRunning(), "bridge did not enter running state");
    require(source.isRunning(), "sample source did not start");
    require(decoder.isRunning(), "Orbita decoder did not start");
    require(source.hasSamplesCallback(), "sample callback was not attached");
    require(source.hasErrorCallback(), "error callback was not attached");

    source.emitSamples(std::vector<int16_t>(256, 0));
    source.emitError("synthetic-source-error");
    require(bridge.lastError() == "synthetic-source-error",
            "source error was not propagated through the bridge");

    bridge.stop();
    require(!bridge.isRunning(), "bridge remained running after stop");
    require(!source.isRunning(), "sample source remained running after stop");
    require(!decoder.isRunning(), "Orbita decoder remained running after stop");
    require(!source.hasSamplesCallback(), "sample callback survived bridge stop");
    require(!source.hasErrorCallback(), "error callback survived bridge stop");
}

void failedSourceRollbackContract()
{
    auto decoder = configuredDecoder();
    FakeSampleSource source;
    source.failStart = true;
    miltech::integration::OrbitaSampleBridge bridge(source, decoder);

    require(!bridge.start(), "bridge must fail when station sample source fails");
    require(!bridge.isRunning(), "failed bridge start left running state set");
    require(!decoder.isRunning(), "failed source start left Orbita decoder running");
    require(!source.hasSamplesCallback(), "failed start leaked sample callback");
    require(!source.hasErrorCallback(), "failed start leaked error callback");
    require(!bridge.lastError().empty(), "failed start did not preserve diagnostic");
}

} // namespace

int main()
{
    try {
        lifecycleContract();
        failedSourceRollbackContract();
        std::cout << "Orbita station sample bridge contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Orbita station sample bridge contract failed: "
                  << error.what() << '\n';
        return 1;
    }
}
