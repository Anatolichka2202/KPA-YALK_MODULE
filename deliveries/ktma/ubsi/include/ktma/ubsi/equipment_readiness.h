#pragma once

#include "orbita_stand/config.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace ktma::ubsi {

enum class EquipmentReadinessStage {
    Power,
    Independent,
    Adapter,
};

struct EquipmentReadinessItem {
    std::string componentId;
    EquipmentReadinessStage stage = EquipmentReadinessStage::Independent;
};

struct EquipmentReadinessPlan {
    std::vector<EquipmentReadinessItem> items;
    unsigned adapterBootDelayMilliseconds = 3000;
};

// Build the physical readiness order for the KTMA/UBSI delivery from the
// canonical component model. An empty required-capability set means all
// equipment declared by the delivery; otherwise only components providing at
// least one requested capability participate.
//
// The ordering is a product/delivery invariant, not a generic Station rule:
//   1) DUT power source(s),
//   2) independent equipment,
//   3) boot delay,
//   4) ULK/adapter parameter source(s).
EquipmentReadinessPlan buildEquipmentReadinessPlan(
    const orbita::stand::StandProfile& profile,
    const std::set<std::string>& requiredCapabilities = {});

struct EquipmentReadinessCallbacks {
    // `armSupply` is true only for the power stage. The callback owns concrete
    // provider creation/probe/binding so the sequence itself stays UI-free.
    std::function<void(const orbita::stand::ComponentProfile&, bool armSupply)> check;
    std::function<void(unsigned milliseconds)> wait;
};

// Execute the delivery-owned ordering without knowing Qt, plugins or operator
// UI. This makes the safety-critical power -> wait -> adapter sequence testable
// independently from MainWindow and lets the desktop become a presentation
// adapter around StationSession.
void executeEquipmentReadinessPlan(
    const orbita::stand::StandProfile& profile,
    const EquipmentReadinessPlan& plan,
    const EquipmentReadinessCallbacks& callbacks);

} // namespace ktma::ubsi
