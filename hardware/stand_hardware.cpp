#include "hardware/stand_hardware.h"

#include <utility>

namespace tu::hardware {

StandHardware::StandHardware(StandConfig config)
    : config_(std::move(config)), supply_(config_.supply), yalk_(config_.yalk)
{
}

StandHardware::~StandHardware()
{
    safeStop();
}

std::string StandHardware::probeSupplyCold()
{
    const std::string identity = supply_.probe();
    // Проверка стенда не должна прогревать УБСИ перед нормативной проверкой
    // готовности. После *IDN? принудительно оставляем выход выключенным и
    // подтверждаем это через OUTP?.
    supply_.setOutput(false);
    return identity;
}

void StandHardware::safeStop() noexcept
{
    yalk_.stop();
    supply_.safeOff();
}

} // namespace tu::hardware
