# Контракт интеграции с действующей MilTech Station (C++ / Qt)

## Абсолютные ограничения

1. **Core — единственный владелец состояния испытания и hardware.** Enterprise, Download Agent и UpdateTool не выполняют команд на КПА и не подключаются к COM/VISA/USB для проверки «готов ли стенд».
2. ТУ EXE — компонуемые приложения ограниченного HMI со своим `component_id`, ABI/API requirement и capability permissions. Их запуск выполняет контролируемый локальный Core runtime, не сетевой сервер.
3. Если сценарий выполняется или RunStore/Registrar в транзакции записи — обновление заблокировано. **В пилоте реальный mutex Core не подключён.**
4. Комплект поставки не должен содержать mutable `runs.db`, `registrar.db`, `parameters.db` или `station.db`. Default seed для чистой установки размещается как read-only ресурс, мигрируется локальным сервисом отдельной транзакцией, не копируется поверх existing DB.

## Интерфейс supervisor/Core, который предстоит реализовать

```cpp
// Contract sketch — NOT wired to existing Qt application.
struct MaintenanceDecision {
    bool allowed;             // Core affirmed no active runs, resources released
    QString reason;
    QString coreBuildIdentity;
};

struct ReleaseCandidate {
    QString releaseId;
    QString releaseRoot;
    bool pendingBoot;
};

class IStationUpdateSupervisor {
public:
    virtual ~IStationUpdateSupervisor() = default;
    virtual MaintenanceDecision requestMaintenance() = 0;
    virtual void stopCoreGracefully() = 0;
    virtual void startCandidate(const ReleaseCandidate&) = 0;
    virtual bool waitUntilReady(const QString& releaseId) = 0;
    virtual void recoverPreviousRelease() = 0;
};
```

После получения `maintenance.allowed` агент обязан завершить все run-сессии, дождаться закрытия всех устройств, flush/run commit в RunStore/registrar и проверить достаточность места. Реализация протокола остановки должна иметь timeouts и явно определённый `abort` при неисправности аппаратуры. Нельзя принудительно завершать Core пока от него зависит контролируемое воздействие на изделие.

На Windows **отдельный** StationBootstrap.exe должен выбирать версию из `state.json`, запускать приложение, ждать `ready` и подтверждать либо откатывать. Обновлять сам bootstrap рекомендуем только отдельным MSI/Qt IFW flow; станционный апдейтер пилота не перезаписывает загрузчик.

## Согласованность реестров

- `delivery.releases`: выпуск программного обеспечения, не запись о корректности испытания.
- `station.db`: должен получить read-only или подтверждённую запись ревизии установленной поставки при подключении реального Core. Пока источник состояния апдейтера — `delivery/state.json`.
- `runs.db`: любой run привязывается к `release_id`, `scenario SHA`, `catalog SHA`, plugin build version и составу измерительных средств. **Сейчас автоматической записи этих полей в действующий RunStore нет.**
- `parameters.db`: каталог метаданных; восстановить значения со старого стенда при обновлении, не заменять seed.
- `registrar.db`: история изделия и замены ячеек; не менять schema под видом software update.
- `outbox.db`: смена software release никак не перезапускает sequence локальных событий и не стирает подтверждения Enterprise.

## Перед первым полевым rollout

Проверить действительный HEAD MilTech, порядок включения интерфейсов Qt, фактические имена и версии DLL, зависимость Python reference agent от Windows Runtime, ABI plugin manager, ТУ packaging/export, дополнительные лицензии и верифицированный сценарий `stop → install → self-test → release`. Нужны испытания на стенде с реальным прибором и имитацией отказа сети/питания.
