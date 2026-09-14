#include "orbita_stand/component_runtime.h"
#include "orbita_stand/config.h"

#include <stdexcept>
#include <utility>

namespace orbita::stand {

void ComponentRuntime::registerKindFactory(std::string kind, KindFactory factory)
{
    if (kind.empty() || !factory) {
        throw std::invalid_argument("Component kind factory requires kind and factory");
    }
    const auto [iterator, inserted] = kindFactories_.emplace(std::move(kind), std::move(factory));
    if (!inserted) {
        throw std::invalid_argument("Component kind factory already registered: " + iterator->first);
    }
}

bool ComponentRuntime::hasKindFactory(const std::string& kind) const noexcept
{
    return kindFactories_.count(kind) != 0;
}

void ComponentRuntime::instantiate(
    const StandProfile& profile,
    const std::set<std::string>& kinds)
{
    clear();
    try {
        for (const auto& definition : profile.components) {
            if (!definition.enabled) continue;
            if (!kinds.empty() && kinds.count(definition.kind) == 0) continue;

            const auto factory = kindFactories_.find(definition.kind);
            if (factory == kindFactories_.end()) {
                throw std::runtime_error(
                    "No station component factory for kind: " + definition.kind);
            }
            if (instances_.count(definition.id)) {
                throw std::runtime_error("Duplicate station component id: " + definition.id);
            }

            auto component = factory->second(definition);
            if (!component) {
                throw std::runtime_error("Component factory returned null: " + definition.id);
            }
            if (component->componentKind() != definition.kind) {
                throw std::runtime_error(
                    "Component kind mismatch for " + definition.id + ": profile="
                    + definition.kind + ", implementation="
                    + std::string(component->componentKind()));
            }

            ComponentDescriptor descriptor{
                definition.id,
                definition.kind,
                definition.provider,
                definition.bindings,
            };

            for (const auto& binding : descriptor.bindings) {
                if (binding.empty()) {
                    throw std::runtime_error(
                        "Empty station component binding: " + definition.id);
                }
                const auto [bindingIterator, inserted] = bindings_.emplace(binding, definition.id);
                if (!inserted) {
                    throw std::runtime_error(
                        "Station component binding is ambiguous: " + binding
                        + " (" + bindingIterator->second + " and " + definition.id + ")");
                }
            }

            descriptors_.push_back(descriptor);
            instances_.emplace(definition.id,
                Instance{std::move(descriptor), std::move(component)});
        }
    } catch (...) {
        clear();
        throw;
    }
}

void ComponentRuntime::safeStopAll() noexcept
{
    for (auto& [id, instance] : instances_) {
        (void)id;
        try {
            if (instance.component) instance.component->safeStop();
        } catch (...) {
            // safe stop is best-effort; every remaining component must still
            // receive an attempt even if one implementation is defective.
        }
    }
}

void ComponentRuntime::clear() noexcept
{
    safeStopAll();
    bindings_.clear();
    descriptors_.clear();
    instances_.clear();
}

IStationComponent* ComponentRuntime::findById(const std::string& id) noexcept
{
    const auto iterator = instances_.find(id);
    return iterator == instances_.end() ? nullptr : iterator->second.component.get();
}

const IStationComponent* ComponentRuntime::findById(const std::string& id) const noexcept
{
    const auto iterator = instances_.find(id);
    return iterator == instances_.end() ? nullptr : iterator->second.component.get();
}

IStationComponent* ComponentRuntime::findByBinding(const std::string& binding) noexcept
{
    const auto bindingIterator = bindings_.find(binding);
    return bindingIterator == bindings_.end() ? nullptr : findById(bindingIterator->second);
}

const IStationComponent* ComponentRuntime::findByBinding(const std::string& binding) const noexcept
{
    const auto bindingIterator = bindings_.find(binding);
    return bindingIterator == bindings_.end() ? nullptr : findById(bindingIterator->second);
}

} // namespace orbita::stand
