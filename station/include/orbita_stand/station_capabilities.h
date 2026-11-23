#pragma once

#include "orbita_stand/desktop_core.h"
#include <QJsonObject>
#include <functional>
#include <atomic>
#include <mutex>
#include <map>
#include <string>
#include <vector>

namespace orbita::stand { class StationSession; }

namespace orbita::stand::desktop {

// Это описание интерфейса, а не разрешение на выполнение команды прибора.
enum class CapabilityAvailability { Available, Unavailable, Forbidden, NotRegistered, Absent };
enum class ApplicationScope { Engineering, Tu, Both };
enum class ServiceConnection { NotApplicable, Unknown, Connected, Disconnected };

struct CapabilityObservation {
    bool available = false;
    std::string reason;
    ServiceConnection connection = ServiceConnection::NotApplicable;
};

struct FeatureRegistration {
    std::string id;
    std::string title;
    ApplicationScope scope = ApplicationScope::Engineering;
    std::function<CapabilityObservation(const RuntimeObservation&)> observe;
};

struct ModuleRegistration {
    std::string id;
    std::string title;
    // Категория задаётся поставщиком (equipment, scenarios, results, tu_configurator,
    // administration, local_storage, enterprise или другая), а не ядром.
    std::string category;
    std::string implementation;
    ApplicationScope scope = ApplicationScope::Engineering;
    std::vector<FeatureRegistration> features;
    // true означает, что хост перечислил все функции этой реализации.
    bool featuresComplete = false;
    // Регистрирующий хост обязан связать наблюдение с реальной реализацией.
    // Наблюдатель не должен запускать операции или повторно захватывать резерв.
    // Он получает согласованное состояние вызова и не должен использовать busy()
    // сессии: снимок сам удерживает резерв. Устройства могут пересоздаваться;
    // захватывайте стабильные сервисы или weak_ptr, а не сырые указатели.
    // Регистрации не меняются параллельно с композицией.
    std::function<CapabilityObservation(const RuntimeObservation&)> observe;
};

struct CapabilityView {
    std::string id;
    std::string title;
    CapabilityAvailability state = CapabilityAvailability::Absent;
    std::string reason;
    ServiceConnection connection = ServiceConnection::NotApplicable;
};

struct ModuleView {
    std::string id;
    std::string title;
    std::string category;
    std::string implementation;
    CapabilityAvailability state = CapabilityAvailability::Absent;
    std::string reason;
    ServiceConnection connection = ServiceConnection::NotApplicable;
    std::vector<CapabilityView> features;
    bool featuresComplete = false;
};

struct DiscoveredMethodology {
    std::string workflowId;
    ScenarioDefinition scenario;
};

struct MethodologyView {
    std::string workflowId;
    std::string id;
    std::string title;
    std::string version;
    bool valid = false;
    bool published = false;
    std::string reason;
};

struct CompositionIdentity {
    std::string stationId;
    std::string deliveryId;
    std::string deliveryVersion;
};

struct ApplicationComposition {
    int schema = 1;
    CompositionIdentity identity;
    bool inventoryComplete = false;
    bool staleDuringActivity = false;
    // При занятой сессии тип операции может быть неизвестен; значение desktop.stopAvailable тогда не является подтверждением.
    bool stopAvailabilityKnown = true;
    DesktopSnapshot desktop;
    std::vector<ModuleView> modules;
    std::vector<ModuleView> requestedModules;
    std::vector<MethodologyView> methodologies;
    // ID процедур извлекаются из реального ScenarioEngine; в ТУ оставлены
    // только процедуры выбранного фиксированного сценария.
    std::vector<std::string> actionHandlers;
    std::string actionDiagnostic;
};

class StationCapabilityRegistry final {
public:
    // Реестр принадлежит конкретному неизменяемому составу и не должен
    // пережить переданные DesktopCore и ScenarioEngine. Для неблокирующей
    // проекции владелец обязан исключить параллельную замену движка.
    StationCapabilityRegistry(const DesktopCore& core, const ScenarioEngine& engine);
    // Явная привязка выполняется после настройки сессии и регистрации действий.
    // Повторная настройка/сброс сессии отменяет эту привязку.
    void bindSession(StationSession& session);
    void registerModule(ModuleRegistration module);
    // Вызывается только сборщиком, перечислившим все установленные модули.
    void markInventoryComplete();
    // Сборщик явно включает зарегистрированную процедуру в состав приложения.
    // Этот список не разрешает аппаратные команды.
    void exposeAction(std::string actionId, ApplicationScope scope);
    ApplicationComposition compose(const DesktopCore& core, const RuntimeObservation& runtime,
        CompositionIdentity identity,
        const std::vector<std::string>& requestedModules = {},
        const std::vector<DiscoveredMethodology>& discovered = {}) const;
    // Снимок требует свободного резерва сессии и не доступен во время запуска.
    // Прямую изменяемую ссылку на профиль/движок нельзя менять параллельно;
    // готовность оборудования не выводится из факта настройки сессии.
    ApplicationComposition compose(const DesktopCore& core, StationSession& session,
        CompositionIdentity identity, const std::vector<std::string>& requestedModules = {},
        const std::vector<DiscoveredMethodology>& discovered = {}) const;
    // Во время выполнения возвращается последний согласованный состав;
    // состояния модулей помечаются как не обновлённые, а запуск запрещается.
    ApplicationComposition composeDuringActivity(const DesktopCore& core, const StationSession& session) const;
private:
    ApplicationComposition composeSnapshot(const DesktopCore& core, const RuntimeObservation& runtime,
        CompositionIdentity identity, const std::vector<std::string>& requestedModules,
        const std::vector<DiscoveredMethodology>& discovered) const;
    const DesktopCore* core_;
    const DesktopCore coreSnapshot_;
    const ScenarioEngine* engine_;
    const StationSession* session_ = nullptr;
    std::uint64_t sessionGeneration_ = 0;
    std::string projectId_;
    std::string projectVersion_;
    std::string profileId_;
    std::string profileVersion_;
    std::map<std::string, ModuleRegistration> modules_;
    std::map<std::string, ApplicationScope> actions_;
    bool inventoryComplete_ = false;
    mutable std::mutex snapshotMutex_;
    mutable std::atomic_bool snapshotting_{false};
    mutable std::optional<ApplicationComposition> lastSessionSnapshot_;
};

const char* toString(CapabilityAvailability value) noexcept;
const char* toString(ServiceConnection value) noexcept;
QJsonObject compositionToJson(const ApplicationComposition& composition);

} // namespace orbita::stand::desktop
