#pragma once

#include "orbita_stand/equipment_runtime.h"
#include "orbita_stand/scenario.h"

#include <map>
#include <string>
#include <vector>

namespace orbita::stand {

// Универсальное описание составной части станции. Конкретная поставка
// определяет состав станции данными, а продуктовый runtime решает, какой
// provider обслуживает соответствующий kind.
//
// Текущие kind:
//   equipment          — синхронное оборудование через Equipment Plugin ABI;
//   sample_source      — поток сырых отсчётов (например, E20-10 для liborbita);
//   execution_runtime  — запуск внешнего/интерпретируемого стендового ПО.
//
// Модель намеренно строковая: будущие runtime/transport providers (Lua,
// Python, external process, SSH, serial и т.п.) не должны требовать изменения
// базовой схемы профиля только ради появления нового вида компонента.
struct ComponentProfile {
    std::string id;
    std::string kind;
    std::string provider;
    bool enabled = true;

    // Логические station bindings/roles. Для новых equipment declarations это
    // именно имена ресурсов (например, power.dut), а не capabilities плагина.
    std::vector<std::string> bindings;

    std::map<std::string, std::string> configuration;

    // Для kind=equipment — capabilities, которые поставка экспортирует через
    // compatibility/default routing. Поле добавлено в конец структуры, чтобы
    // существующие aggregate initializers ComponentProfile не меняли смысл.
    // Если поле пусто, старый профиль трактует `bind` как capability list.
    std::vector<std::string> capabilities;
};

// Совместимость с текущим Equipment runtime. `devices:` остаётся допустимым
// входным форматом на время миграции поставок на общую секцию `components:`.
struct DeviceProfile {
    std::string id;
    std::string pluginId;
    bool enabled = true;
    std::vector<std::string> bindCapabilities;
    std::map<std::string, std::string> configuration;
};

struct StandProfile {
    std::string id;
    std::string version;
    std::string title;
    bool activeOutputsConfirmed = false;

    // Каноническая модель состава станции. Сюда также зеркалируются legacy
    // `devices:` как kind=equipment, поэтому новый код может работать только
    // с components и не знать о старой схеме.
    std::vector<ComponentProfile> components;

    // Legacy view для существующего Equipment runtime. Удаляется после
    // миграции desktop и оставшихся capability-only consumers.
    std::vector<DeviceProfile> devices;

    std::map<std::string, std::string> routes;
    // Подтверждённая физическая топология стенда. Сценарии используют routes,
    // а connections нужны инженеру для проверки кабелей перед запуском.
    std::map<std::string, std::string> connections;
};

StandProfile loadStandProfile(const std::string& path);
ScenarioDefinition loadScenarioYaml(const std::string& path);

const ComponentProfile* findComponentById(
    const StandProfile& profile, const std::string& id) noexcept;
const ComponentProfile* findComponentByBinding(
    const StandProfile& profile, const std::string& binding) noexcept;

void instantiateProfile(
    const StandProfile& profile,
    EquipmentPluginManager& manager,
    EquipmentRegistry& registry,
    std::vector<std::shared_ptr<EquipmentDevice>>& devices);

} // namespace orbita::stand
