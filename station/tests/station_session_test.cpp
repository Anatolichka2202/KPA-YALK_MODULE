#include "orbita_stand/execution_runtime.h"
#include "orbita_stand/station_session.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <future>
#include <thread>
#include <iostream>
#include <stdexcept>

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir pluginDirectory;
        require(pluginDirectory.isValid(), "temporary plugin directory is unavailable");

        StandProfile profile;
        profile.id = "session-test";
        profile.version = "1";
        profile.title = "Station session contract";
        profile.components.push_back(ComponentProfile{
            "external-runtime",
            "execution_runtime",
            "miltech.exec.process",
            true,
            {"runtime.legacy_bench"},
            {{"program", "placeholder-program"}},
        });

        StationSession session;
        registerExecutionRuntimeComponents(session.components());
        session.configure(profile, pluginDirectory.path().toUtf8().toStdString(), {});

        require(session.configured(), "station session was not marked configured");
        require(session.profile().id == "session-test", "station session lost delivery profile");
        require(session.equipmentDevices().empty(), "unexpected equipment was instantiated");
        auto* runtime = session.components().findAs<IExecutionRuntime>("runtime.legacy_bench");
        require(runtime != nullptr, "execution runtime is not owned by station session");
        require(!runtime->isRunning(), "execution runtime must be idle after configuration");

        // Проверка проходит через сессию: оборудование принадлежит ей, а
        // вложенный запуск и ручная команда не могут вмешаться в активный шаг.
        bool invoked = false;
        bool stopped = false;
        session.equipment().bind("test.simulator",
            [&](const std::string& operation, const std::map<std::string, std::string>&) {
                if (operation != "measure") throw std::runtime_error("unexpected operation");
                invoked = true;
                return std::string("42");
            }, [&] { stopped = true; });
        ScenarioDefinition scenario;
        scenario.id = "session-check";
        scenario.title = "Проверка сессии";
        scenario.version = "1";
        scenario.catalogVersion = "1";
        scenario.publicationState = PublicationState::Published;
        ScenarioNode node;
        node.id = "measure";
        node.title = "Измерение";
        node.tuRequirement = "test.1";
        node.procedure = "test.measure";
        node.requiredCapabilities.insert("test.simulator");
        scenario.steps.push_back(node);
        bool rejectedRun = false;
        bool rejectedManual = false;
        session.scenarioEngine().registerProcedure("test.measure",
            [&](const ScenarioNode&, ProcedureContext& context) {
                const auto nested = session.runScenario(scenario, "profile", "nested", false);
                rejectedRun = nested.verdict == RunVerdict::Incomplete
                    && !nested.events.empty() && nested.events.front().stage == "STATION_BUSY"
                    && !nested.events.front().message.empty();
                try {
                    session.invokeManual("test.simulator", "measure", {});
                } catch (const std::exception&) {
                    rejectedManual = true;
                }
                const auto value = context.equipment.invoke("test.simulator", "measure", {});
                return ProcedureResult{value == "42" ? RunVerdict::Ok : RunVerdict::Fail,
                                       "Измерено", {}};
            });
        const auto result = session.runScenario(scenario, "profile", "SN", false);
        if (result.verdict != RunVerdict::Ok || !invoked || !stopped) {
            std::cerr << "verdict=" << toString(result.verdict) << " invoked=" << invoked
                      << " stopped=" << stopped << "\n";
            for (const auto& event : result.events) std::cerr << event.message << "\n";
        }
        require(result.verdict == RunVerdict::Ok && invoked && stopped,
                "station entry must execute and stop the simulated equipment");
        require(rejectedRun && rejectedManual,
                "station entry must reject conflicting runs and manual actions");
        require(session.invokeManual("test.simulator", "measure", {}) == "42",
                "manual action must be available again after the run");

        // Подготовка и запуск используют одно резервирование: второй вход и
        // ручная команда не могут изменить прибор между этими действиями.
        auto activity = session.beginActivity();
        require(bool(activity), "could not reserve station for readiness");
        require(!session.beginActivity(), "readiness reservation allowed a competing action");
        bool manualDuringReadinessRejected = false;
        try { session.invokeManual("test.simulator", "measure", {}); }
        catch (const std::exception&) { manualDuringReadinessRejected = true; }
        require(manualDuringReadinessRejected, "manual action bypassed readiness reservation");
        require(session.runScenario(scenario, "profile", "SN", false, {}, activity).verdict
                    == RunVerdict::Ok,
                "reserved scenario did not run after readiness");
        require(!session.beginActivity(), "reservation was released before its owner finished");
        activity.reset();
        require(bool(session.beginActivity()), "station stayed reserved after readiness finished");
        try {
            auto failedReadiness = session.beginActivity();
            if (!failedReadiness) throw std::logic_error("reservation unavailable");
            throw std::runtime_error("simulated readiness failure");
        } catch (const std::logic_error&) { throw; }
          catch (const std::runtime_error&) {}
        require(bool(session.beginActivity()), "failed readiness retained the station");
        StationSession otherSession;
        auto foreignActivity = otherSession.beginActivity();
        require(session.runScenario(scenario, "profile", "SN", false, {}, foreignActivity).verdict
                    == RunVerdict::Incomplete,
                "a reservation from another station was accepted");
        foreignActivity.reset();

        // Остановка вызывается из другого потока, пока процедура ожидает её;
        // ручная команда в этот момент не должна доходить до имитатора.
        std::promise<void> entered;
        session.scenarioEngine().registerProcedure("test.wait",
            [&](const ScenarioNode&, ProcedureContext& context) {
                entered.set_value();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!context.stopRequested.load() && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::yield();
                return ProcedureResult{context.stopRequested.load() ? RunVerdict::Aborted : RunVerdict::Error,
                                       "Остановлено", {}};
            });
        auto waitingScenario = scenario;
        waitingScenario.steps.front().procedure = "test.wait";
        auto running = std::async(std::launch::async, [&] {
            return session.runScenario(waitingScenario, "profile", "SN", false);
        });
        if (entered.get_future().wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            session.requestStop();
            running.wait();
            throw std::runtime_error("scenario did not enter the simulated procedure");
        }
        bool concurrentManualRejected = false;
        try {
            session.invokeManual("test.simulator", "measure", {});
        } catch (const std::exception&) {
            concurrentManualRejected = true;
        }
        session.requestStop();
        const auto stoppedRun = running.get();
        require(concurrentManualRejected, "manual action was not rejected during a run");
        require(stoppedRun.verdict == RunVerdict::Aborted,
                "stop request did not reach the active scenario");

        // Имитация остановки после постановки запуска в очередь, но до старта
        // рабочего потока: воздействие не должно начаться после команды Stop.
        invoked = false;
        session.requestStop();
        const auto earlyStop = session.runScenario(scenario, "profile", "SN", false);
        require(earlyStop.verdict == RunVerdict::Aborted && !invoked,
                "stop request was lost before the scenario started");
        require(session.runScenario(scenario, "profile", "SN", false).verdict == RunVerdict::Ok,
                "stop request leaked into the next scenario");

        session.clear();
        require(!session.configured(), "station session remained configured after clear");
        require(session.components().findByBinding("runtime.legacy_bench") == nullptr,
                "station component survived session clear");
        require(session.profile().id.empty(), "station profile survived session clear");

        auto deferredProfile = profile;
        deferredProfile.id = "deferred-session-test";
        deferredProfile.components.push_back(ComponentProfile{
            "bench-power",
            "equipment",
            "missing.equipment.provider",
            true,
            {"power.dut"},
            {},
            {"power.dc_supply"},
        });

        StationSession deferred;
        registerExecutionRuntimeComponents(deferred.components());
        deferred.configure(
            deferredProfile,
            pluginDirectory.path().toUtf8().toStdString(),
            {},
            EquipmentInstantiation::Deferred);
        require(deferred.configured(), "deferred station session was not configured");
        require(deferred.profile().components.size() == 2,
                "deferred station session lost equipment declaration");
        require(deferred.equipmentDevices().empty(),
                "deferred station session instantiated physical equipment");
        require(deferred.equipment().resources().empty(),
                "deferred station session exported equipment resources before readiness");
        require(deferred.components().findAs<IExecutionRuntime>("runtime.legacy_bench") != nullptr,
                "deferred station session did not instantiate non-equipment components");

        StationSession immediate;
        registerExecutionRuntimeComponents(immediate.components());
        bool immediateRejectedMissingProvider = false;
        try {
            immediate.configure(
                deferredProfile,
                pluginDirectory.path().toUtf8().toStdString(),
                {},
                EquipmentInstantiation::Immediate);
        } catch (const std::exception&) {
            immediateRejectedMissingProvider = true;
        }
        require(immediateRejectedMissingProvider,
                "immediate equipment mode accepted a missing equipment provider");
        require(!immediate.configured(),
                "failed immediate configuration left session marked configured");

        const QDir executableDirectory(QCoreApplication::applicationDirPath());
        const QString builtPluginDirectory = QDir(
            executableDirectory.filePath(QStringLiteral("../plugins"))).absolutePath();
        StandProfile readinessProfile;
        readinessProfile.id = "readiness-session-test";
        readinessProfile.version = "1";
        readinessProfile.routes["test.route"] = "configured";
        readinessProfile.components.push_back(ComponentProfile{
            "bench-power",
            "equipment",
            "orbita.tcp_scpi_bench",
            true,
            {"power.dut"},
            {{"host", "127.0.0.1"}, {"port", "9"}, {"timeout_ms", "1"}},
            {"power.dc_supply"},
        });

        StationSession readiness;
        readiness.configure(
            readinessProfile,
            builtPluginDirectory.toUtf8().toStdString(),
            {},
            EquipmentInstantiation::Deferred);
        auto device = readiness.createEquipmentComponent("bench-power");
        require(device && device->instanceId() == "bench-power",
                "deferred readiness did not create the requested equipment component");
        require(readiness.equipmentDevices().size() == 1,
                "deferred readiness did not retain the created equipment instance");
        require(readiness.equipment().resources().empty(),
                "unprobed deferred equipment was exported prematurely");

        readiness.bindEquipmentComponent("bench-power", device, false);
        require(readiness.equipment().hasResource("bench-power")
                    && readiness.equipment().hasResource("power.dut"),
                "deferred readiness did not export component id and delivery role");
        require(readiness.equipment().resourceHasCapability(
                    "power.dut", "power.dc_supply"),
                "delivery role did not expose the equipment capability");
        require(!readiness.equipment().resourceHasCapability(
                    "power.dut", "signal.generator"),
                "delivery role leaked a provider capability not declared by the component");
        require(!readiness.equipment().hasCapability("power.dc_supply"),
                "resource-only readiness unexpectedly enabled legacy default routing");
        const auto resources = readiness.equipment().resources();
        require(resources.size() == 2 && !resources.front().builtin && !resources.back().builtin,
                "physical deferred equipment was misclassified as a built-in resource");

        readiness.equipment().bind("test.scenario_service",
            [](const std::string& operation,
               const std::map<std::string, std::string>&) {
                return operation == "probe" ? std::string("status=ready\n")
                                            : std::string("status=unknown\n");
            });
        readiness.clearPhysicalEquipment();
        require(readiness.equipmentDevices().empty()
                    && readiness.equipment().resources().empty(),
                "physical equipment reset did not clear deferred devices/routes");
        require(readiness.equipment().hasCapability("test.scenario_service")
                    && readiness.equipment().invoke(
                        "test.scenario_service", "probe", {}) == "status=ready\n",
                "physical equipment reset removed an application built-in service");

        readiness.clearEquipment();
        require(!readiness.equipment().hasCapability("test.scenario_service"),
                "full equipment reset preserved a built-in service unexpectedly");

        auto persistentProfile = readinessProfile;
        persistentProfile.components.back().configuration["session_persistent"] = "true";
        StationSession persistent;
        persistent.configure(persistentProfile,
            builtPluginDirectory.toUtf8().toStdString(), {},
            EquipmentInstantiation::Deferred);
        const auto first = persistent.createEquipmentComponent("bench-power");
        persistent.bindEquipmentComponent("bench-power", first);
        persistent.clearPhysicalEquipment();
        require(persistent.equipmentDevices().size() == 1,
                "Session-persistent equipment was discarded between runs");
        const auto second = persistent.createEquipmentComponent("bench-power");
        require(first == second,
                "Session-persistent equipment was recreated after physical readiness reset");
        persistent.clearEquipment();
        require(persistent.equipmentDevices().empty(),
                "Full equipment clear retained a persistent device");

        std::cout << "station session contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "station session contract failed: " << error.what() << '\n';
        return 1;
    }
}
