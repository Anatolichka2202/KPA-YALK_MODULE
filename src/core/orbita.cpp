#include "orbita.h"
#include "orbita_core.h"

namespace orbita {

Orbita::Orbita() : ctx_(std::make_unique<Context>()) {}

Orbita::~Orbita() = default;

Orbita::Orbita(Orbita&&) noexcept = default;
Orbita& Orbita::operator=(Orbita&&) noexcept = default;

void Orbita::setDeviceE2010(int channel, double sample_rate_khz) {
    ctx_->setDeviceE2010(channel, sample_rate_khz);
}

void Orbita::setChannelsFromFile(const std::string& filename) {
    ctx_->setChannelsFromFile(filename);
}

void Orbita::setChannelsFromLines(const std::vector<std::string>& lines) {
    ctx_->setChannelsFromLines(lines);
}

void Orbita::start() {
    ctx_->start();
}

void Orbita::stop() {
    ctx_->stop();
}

void Orbita::setInvertSignal(bool invert) {
    ctx_->setInvertSignal(invert);
}

void Orbita::startRecording(const std::string& filename) {
    ctx_->startRecording(filename);
}

void Orbita::stopRecording() {
    ctx_->stopRecording();
}

bool Orbita::waitForData(std::chrono::milliseconds timeout) {
    return ctx_->waitForData(timeout);
}

double Orbita::getAnalog(int idx) const {
    return ctx_->getAnalog(idx);
}

int Orbita::getContact(int idx) const {
    return ctx_->getContact(idx);
}

int Orbita::getFast(int idx) const {
    return ctx_->getFast(idx);
}

int Orbita::getTemperature(int idx) const {
    return ctx_->getTemperature(idx);
}

int Orbita::getBus(int idx) const {
    return ctx_->getBus(idx);
}

int Orbita::getPhraseErrorPercent() const {
    return ctx_->getPhraseErrorPercent();
}

int Orbita::getGroupErrorPercent() const {
    return ctx_->getGroupErrorPercent();
}

void Orbita::loadScript(const std::string& script_path) {
    ctx_->loadScript(script_path);
}

std::string Orbita::runScriptFunction(const std::string& func_name, const std::string& args_json) {
    return ctx_->runScriptFunction(func_name, args_json);
}

} // namespace orbita
