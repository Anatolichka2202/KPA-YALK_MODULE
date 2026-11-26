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

    const bool adapterRequested = std::any_of(
        profile.components.begin(), profile.components.end(),
        [&](const orbita::stand::ComponentProfile& component) {
            return component.kind == "equipment"
                && provides(component, "ulk.parameter_source")
                && providesAny(component, requiredCapabilities);
        });

    for (const auto& component : profile.components) {
        if (component.kind != "equipment") continue;

        const bool selected = providesAny(component, requiredCapabilities)
            || (adapterRequested && provides(component, "power.dc_supply"));
        if (!selected) continue;

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

EquipmentReadinessPlan buildScenarioEquipmentReadinessPlan(
    const orbita::stand::StandProfile& profile,
    const orbita::stand::ScenarioDefinition& scenario,
    const std::vector<orbita::stand::ResourceRequirement>& availableServices,
    const std::set<std::string>& availableCapabilities)
{
    auto selected = orbita::stand::selectScenarioEquipment(profile, scenario, availableServices, availableCapabilities);
    auto adapterScenario = scenario;
    std::function<void(orbita::stand::ScenarioNode&)> keepAdapterRequirements;
    keepAdapterRequirements = [&](orbita::stand::ScenarioNode& node) {
        for (auto it = node.requiredCapabilities.begin(); it != node.requiredCapabilities.end();) {
            if (*it != "ulk.parameter_source") it = node.requiredCapabilities.erase(it);
            else ++it;
        }
        node.requiredResources.erase(std::remove_if(node.requiredResources.begin(), node.requiredResources.end(),
            [](const auto& requirement) { return requirement.capability != "ulk.parameter_source"; }),
            node.requiredResources.end());
        for (auto& child : node.children) keepAdapterRequirements(child);
    };
    for (auto& node : adapterScenario.steps) keepAdapterRequirements(node);
    const auto adapters = orbita::stand::selectScenarioEquipment(
        profile, adapterScenario, availableServices, availableCapabilities);
    const bool needsAdapter = !adapters.empty();
    std::vector<const orbita::stand::ComponentProfile*> dutSupplies;
    for (const auto& component : profile.components) {
        if (!component.enabled || component.kind != "equipment" || !provides(component, "power.dc_supply")) continue;
        const bool dutRole = component.id == "power.dut"
            || std::find(component.bindings.begin(), component.bindings.end(), "power.dut") != component.bindings.end();
        if (dutRole) dutSupplies.push_back(&component);
    }
    const auto* dutSupply = dutSupplies.size() == 1 ? dutSupplies.front() : nullptr;
    if (needsAdapter && !dutSupply)
        throw std::invalid_argument("Нельзя однозначно выбрать питание УБСИ для адаптера");
    if (needsAdapter) selected.insert(dutSupply->id);
    auto limited = profile;
    limited.components.erase(std::remove_if(limited.components.begin(), limited.components.end(),
        [&](const auto& component) { return !selected.count(component.id); }), limited.components.end());
    auto plan = buildEquipmentReadinessPlan(limited);
    for (auto& item : plan.items) {
        // 27 В допустимы только для источника УБСИ, а не для любого другого
        // выбранного сценарием прибора с той же capability.
        if (item.stage == EquipmentReadinessStage::Power
            && (!dutSupply || item.componentId != dutSupply->id))
            item.stage = EquipmentReadinessStage::Independent;
        if (item.stage == EquipmentReadinessStage::Adapter && !adapters.count(item.componentId))
            item.stage = EquipmentReadinessStage::Independent;
    }
    std::stable_sort(plan.items.begin(), plan.items.end(), [](const auto& a, const auto& b) {
        return a.stage < b.stage;
    });
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
