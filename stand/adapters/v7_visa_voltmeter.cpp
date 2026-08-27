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
          readDelayMilliseconds(config.readDelayMilliseconds),
          voltageCommand(std::move(config.voltageCommand)),
          currentCommand(std::move(config.currentCommand)),
          acVoltageCommand(std::move(config.acVoltageCommand)),
          frequencyCommand(std::move(config.frequencyCommand)) {}
    VisaInstrument instrument;
    unsigned readDelayMilliseconds;
    std::string voltageCommand;
    std::string currentCommand;
    std::string acVoltageCommand;
    std::string frequencyCommand;
};

double numericQuery(VisaInstrument& instrument, const std::string& command,
                    unsigned delay, const char* diagnostic)
{
    const std::string response = instrument.query(command, delay);
    std::size_t consumed = 0;
    const double value = std::stod(response, &consumed);
    if (!consumed || !std::isfinite(value)) throw std::runtime_error(diagnostic);
    return value;
}

V7VisaVoltmeter::V7VisaVoltmeter(V7VisaConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
V7VisaVoltmeter::~V7VisaVoltmeter() = default;
V7VisaVoltmeter::V7VisaVoltmeter(V7VisaVoltmeter&&) noexcept = default;
V7VisaVoltmeter& V7VisaVoltmeter::operator=(V7VisaVoltmeter&&) noexcept = default;

double V7VisaVoltmeter::readVoltage()
{
    return numericQuery(impl_->instrument, impl_->voltageCommand,
        impl_->readDelayMilliseconds, "V7-78/1 returned an invalid DC voltage");
}

double V7VisaVoltmeter::readCurrent()
{
    return numericQuery(impl_->instrument, impl_->currentCommand,
        impl_->readDelayMilliseconds, "V7-78/1 returned an invalid current");
}

double V7VisaVoltmeter::readAcVoltage()
{
    return numericQuery(impl_->instrument, impl_->acVoltageCommand,
        impl_->readDelayMilliseconds, "V7-78/1 returned an invalid AC voltage");
}

double V7VisaVoltmeter::readFrequency()
{
    return numericQuery(impl_->instrument, impl_->frequencyCommand,
        impl_->readDelayMilliseconds, "V7-78/1 returned an invalid frequency");
}

const std::string& V7VisaVoltmeter::resourceName() const
{
    return impl_->instrument.resourceName();
}

} // namespace orbita::stand
