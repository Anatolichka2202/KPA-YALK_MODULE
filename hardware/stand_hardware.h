#pragma once

#include "hardware/akip1160.h"
#include "hardware/bench_instruments.h"
#include "hardware/isd_router.h"
#include "hardware/stand_config.h"
#include "hardware/yalk_reference_link.h"

#include <memory>
#include <string>

namespace tu::hardware {

class StandHardware final {
public:
    explicit StandHardware(StandConfig config);
    ~StandHardware();

    StandHardware(const StandHardware&) = delete;
    StandHardware& operator=(const StandHardware&) = delete;

    std::string probeSupplyCold();
    std::string probeIsd();
    std::string probeV7();
    std::string probeGenerator();
    void safeStop() noexcept;

    Akip1160& supply() noexcept { return supply_; }
    YalkReferenceLink& yalk() noexcept { return yalk_; }
    IsdRouter& isd() noexcept { return isd_; }
    V7Meter& v7();
    RigolGenerator& generator();
    const StandConfig& config() const noexcept { return config_; }

private:
    StandConfig config_;
    Akip1160 supply_;
    YalkReferenceLink yalk_;
    IsdRouter isd_;
    std::unique_ptr<V7Meter> v7_;
    std::unique_ptr<RigolGenerator> generator_;
};

} // namespace tu::hardware
