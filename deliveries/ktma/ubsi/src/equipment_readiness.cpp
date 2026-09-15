#include "ktma/ubsi/equipment_readiness.h"

#include <algorithm>
#include <stdexcept>

namespace ktma::ubsi {
namespace {

bool providesAny(
    const orbita::stand::ComponentProfile& component,
    const std::set<std::string>& required)
{
    if (required.empty()) return true;
    const auto& capabilities = component.capabilities.empty()
        ? component.bindings
        : component.capabilities;
    return std::any_of(capabilities.begin(), capabilities.end(),
        [&](const std::string& capability) { return required.count(capability) != 0; });
}

bool provides(
    const orbita::stand::ComponentProfile& component,
    const std::string& capability)
{
    const auto& capabilities = component.capabilities.empty()
        ? component.bindings
        : component.capabilities;
    return std::find(capabilities.begin(), capabilities.end(), capability)
        != capabilities.end();
}

const orbita::stand::ComponentProfile& requiredComponent(
    const orbita::stand::StandProfile& profile,
    const std::string& id)
{
    const auto* component = orbita::stand::findComponentById(profile, id);
    if (!component) {
        throw std::logic_error("Readiness plan references missing component: " + id);
    }
    return *component;
}

} // namespace

EquipmentReadinessPlan buildEquipmentReadinessPlan(
    const orbita::stand::StandProfile& profile,
    const std::set<std::string>& requiredCapabilities)
{
    EquipmentReadinessPlan plan;
    std::vector<EquipmentReadinessItem> power;
    std::vector<EquipmentReadinessItem> independent;
    std::vector<EquipmentReadinessItem> adapter;

    for (const auto& component : profile.components) {
        if (component.kind != "equipment" || !providesAny(component, requiredCapabilities)) {
            continue;
        }

        if (provides(component, "power.dc_supply")) {
            power.push_back({component.id, EquipmentReadinessStage::Power});
        } else if (provides(component, "ulk.parameter_source")) {
            adapter.push_back({component.id, EquipmentReadinessStage::Adapter});
        } else {
            independent.push_back({component.id, EquipmentReadinessStage::Independent});
        }
    }

    plan.items.reserve(power.size() + independent.size() + adapter.size());
    plan.items.insert(plan.items.end(), power.begin(), power.end());
    plan.items.insert(plan.items.end(), independent.begin(), independent.end());
    plan.items.insert(plan.items.end(), adapter.begin(), adapter.end());
    return plan;
}

void executeEquipmentReadinessPlan(
    const orbita::stand::StandProfile& profile,
    const EquipmentReadinessPlan& plan,
    const EquipmentReadinessCallbacks& callbacks)
{
    if (!callbacks.check) {
        throw std::invalid_argument("Equipment readiness requires a check callback");
    }

    bool waitedForAdapter = false;
    for (const auto& item : plan.items) {
        if (item.stage == EquipmentReadinessStage::Adapter && !waitedForAdapter) {
            if (plan.adapterBootDelayMilliseconds > 0) {
                if (!callbacks.wait) {
                    throw std::invalid_argument(
                        "Equipment readiness requires a wait callback before adapter probe");
                }
                callbacks.wait(plan.adapterBootDelayMilliseconds);
            }
            waitedForAdapter = true;
        }
        const auto& component = requiredComponent(profile, item.componentId);
        callbacks.check(component, item.stage == EquipmentReadinessStage::Power);
    }
}

} // namespace ktma::ubsi
