#include "hardware/v7_meter.h"

#include <cmath>
#include <stdexcept>

namespace tu::hardware {

double readV7Voltage(const V7Config& config)
{
    VisaInstrument instrument(config.visa);
    const auto response = instrument.query(config.dcVoltageCommand);
    std::size_t parsed = 0;
    const double result = std::stod(response, &parsed);
    if (response.find_first_not_of(" \t\r\n", parsed) != std::string::npos
        || !std::isfinite(result) || std::abs(result) >= 1e30)
        throw std::runtime_error("В7 вернул ошибку диапазона или некорректное напряжение");
    return result;
}

} // namespace tu::hardware
