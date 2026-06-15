#include "../include/orbita.h"
#include "orbita_core.h"

namespace orbita {

Orbita::Orbita() : ctx_(std::make_unique<Context>()) {}
Orbita::~Orbita() = default;
Orbita::Orbita(Orbita&&) noexcept = default;
Orbita& Orbita::operator=(Orbita&&) noexcept = default;

void Orbita::setDeviceE2010(int channel, double rate_khz) { ctx_->setDeviceE2010(channel, rate_khz); }
void Orbita::setDeviceNone() { ctx_->setDeviceNone(); }

void Orbita::setChannels(const std::vector<ChannelSpec>& specs) { ctx_->setChannels(specs); }
std::vector<ChannelSpec> Orbita::getChannels() const { return ctx_->getChannels(); }

void Orbita::start()  { ctx_->start(); }
void Orbita::stop()   { ctx_->stop(); }
void Orbita::pause()  { ctx_->pause(); }
void Orbita::resume() { ctx_->resume(); }
bool Orbita::isRunning() const { return ctx_->isRunning(); }
bool Orbita::isPaused()  const { return ctx_->isPaused(); }

void Orbita::setInvertSignal(bool invert) { ctx_->setInvertSignal(invert); }

void Orbita::startRecording(const std::string& filename) { ctx_->startRecording(filename); }
void Orbita::stopRecording() { ctx_->stopRecording(); }
bool Orbita::isRecording() const { return ctx_->isRecording(); }

bool Orbita::waitForData(std::chrono::milliseconds timeout) { return ctx_->waitForData(timeout); }
Snapshot Orbita::getSnapshot() const { return ctx_->getSnapshot(); }
std::optional<double> Orbita::getValueByAddress(const std::string& address) const {
    return ctx_->getValueByAddress(address);
}
Stats Orbita::getStats() const { return ctx_->getStats(); }
uint32_t Orbita::getCurrentTimeSeconds() const { return ctx_->getCurrentTimeSeconds(); }
void Orbita::setDataCallback(DataCallback cb) { ctx_->setDataCallback(std::move(cb)); }

} // namespace orbita
