#include "orbita_stand/v7_visa_voltmeter.h"
#include "orbita_stand/visa_instrument.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace orbita::stand {

struct V7VisaVoltmeter::Impl {
    explicit Impl(V7VisaConfig config)
        : instrument({{std::move(config.resourceExpression),
                       std::move(config.fallbackResourceExpression)},
                      config.timeoutMilliseconds}),
          readDelayMilliseconds(config.readDelayMilliseconds) {}
    VisaInstrument instrument;
    unsigned readDelayMilliseconds;
};

V7VisaVoltmeter::V7VisaVoltmeter(V7VisaConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
V7VisaVoltmeter::~V7VisaVoltmeter() = default;
V7VisaVoltmeter::V7VisaVoltmeter(V7VisaVoltmeter&&) noexcept = default;
V7VisaVoltmeter& V7VisaVoltmeter::operator=(V7VisaVoltmeter&&) noexcept = default;

double V7VisaVoltmeter::readVoltage()
{
    const std::string response = impl_->instrument.query("READ?", impl_->readDelayMilliseconds);
    std::size_t consumed = 0;
    const double value = std::stod(response, &consumed);
    if (!consumed || !std::isfinite(value)) {
        throw std::runtime_error("V7-78/1 returned an invalid numeric reading");
    }
    return value;
}

const std::string& V7VisaVoltmeter::resourceName() const
{
    return impl_->instrument.resourceName();
}

} // namespace orbita::stand
