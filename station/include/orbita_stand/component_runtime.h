#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace orbita::stand {

struct ComponentProfile;
struct StandProfile;

// Общая минимальная граница любой составной части станции. Конкретные виды
// компонентов (equipment, sample_source, execution_runtime, transport, ...)
// добавляют собственные типизированные интерфейсы поверх этого контракта.
class IStationComponent {
public:
    virtual ~IStationComponent() = default;

    virtual std::string_view componentKind() const noexcept = 0;
    virtual void safeStop() noexcept = 0;
};

struct ComponentDescriptor {
    std::string id;
    std::string kind;
    std::string provider;
    std::vector<std::string> bindings;
};

// Runtime ничего не знает о конкретных E20, Lua, Python, SSH и т.п. Он знает
// только kind из профиля и делегирует создание зарегистрированной фабрике.
// Это позволяет поставке определять состав станции данными.
class ComponentRuntime final {
public:
    using KindFactory = std::function<std::unique_ptr<IStationComponent>(
        const ComponentProfile&)>;

    ComponentRuntime() = default;
    ~ComponentRuntime() { clear(); }
    ComponentRuntime(const ComponentRuntime&) = delete;
    ComponentRuntime& operator=(const ComponentRuntime&) = delete;

    void registerKindFactory(std::string kind, KindFactory factory);
    bool hasKindFactory(const std::string& kind) const noexcept;

    // Если kinds пуст — инстанцируются все включённые компоненты профиля.
    // Во время поэтапной миграции можно явно загрузить только нужные виды,
    // например {"sample_source"}, оставив equipment на старом runtime.
    void instantiate(
        const StandProfile& profile,
        const std::set<std::string>& kinds = {});

    void clear() noexcept;
    void safeStopAll() noexcept;

    IStationComponent* findById(const std::string& id) noexcept;
    const IStationComponent* findById(const std::string& id) const noexcept;
    IStationComponent* findByBinding(const std::string& binding) noexcept;
    const IStationComponent* findByBinding(const std::string& binding) const noexcept;

    template<typename T>
    T* findAs(const std::string& binding) noexcept
    {
        return dynamic_cast<T*>(findByBinding(binding));
    }

    template<typename T>
    const T* findAs(const std::string& binding) const noexcept
    {
        return dynamic_cast<const T*>(findByBinding(binding));
    }

    const std::vector<ComponentDescriptor>& components() const noexcept
    {
        return descriptors_;
    }

private:
    struct Instance {
        ComponentDescriptor descriptor;
        std::unique_ptr<IStationComponent> component;
    };

    std::map<std::string, KindFactory> kindFactories_;
    std::map<std::string, Instance> instances_;
    std::map<std::string, std::string> bindings_;
    std::vector<ComponentDescriptor> descriptors_;
};

} // namespace orbita::stand
