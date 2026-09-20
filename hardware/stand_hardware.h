#pragma once

#include "hardware/akip1160.h"
#include "hardware/stand_config.h"
#include "hardware/yalk_reference_link.h"

#include <string>

namespace tu::hardware {

class StandHardware final {
public:
    explicit StandHardware(StandConfig config);
    ~StandHardware();

    StandHardware(const StandHardware&) = delete;
    StandHardware& operator=(const StandHardware&) = delete;

    std::string probeSupplyCold();
    void safeStop() noexcept;

    Akip1160& supply() noexcept { return supply_; }
    YalkReferenceLink& yalk() noexcept { return yalk_; }
    const StandConfig& config() const noexcept { return config_; }

private:
    StandConfig config_;
    Akip1160 supply_;
    YalkReferenceLink yalk_;
};

} // namespace tu::hardware
