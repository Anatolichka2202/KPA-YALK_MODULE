#pragma once

#include "hardware/visa_instrument.h"

namespace tu::hardware {

// Тот же DC-запрос В7-78/1 через VISA, что в поставке tu-minimal-clean.
struct V7Config {
    VisaConfig visa;
    std::string dcVoltageCommand;
};

double readV7Voltage(const V7Config& config);

} // namespace tu::hardware
