#include "hardware/stand_hardware.h"

#include <utility>

namespace tu::hardware {

StandHardware::StandHardware(StandConfig config)
    : config_(std::move(config)),
      supply_(config_.supply),
      yalk_(config_.yalk),
      isd_(config_.isd)
{
}

StandHardware::~StandHardware()
{
    safeStop();
}

std::string StandHardware::probeSupplyCold()
{
    const std::string identity = supply_.probe();
    supply_.setOutput(false);
    return identity;
}

std::string StandHardware::probeIsd()
{
    return isd_.probe();
}

V7Meter& StandHardware::v7()
{
    if (!v7_) v7_ = std::make_unique<V7Meter>(config_.v7);
    return *v7_;
}

RigolGenerator& StandHardware::generator()
{
    if (!generator_) generator_ = std::make_unique<RigolGenerator>(config_.generator);
    return *generator_;
}

std::string StandHardware::probeV7()
{
    return v7().identity();
}

std::string StandHardware::probeGenerator()
{
    auto& value = generator();
    const auto identity = value.identity();
    value.safeOff();
    return identity;
}

void StandHardware::safeStop() noexcept
{
    // Сначала снимаем внешнее возбуждение, затем только адресно освобождаем
    // маршруты ИСД. Глобальный type=4 здесь намеренно не используется.
    if (generator_) generator_->safeOff();
    isd_.safeStop();
    yalk_.stop();
    supply_.safeOff();
}

} // namespace tu::hardware
