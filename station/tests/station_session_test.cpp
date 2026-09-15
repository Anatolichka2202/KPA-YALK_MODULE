#include "orbita_stand/execution_runtime.h"
#include "orbita_stand/station_session.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

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

        readiness.clearEquipment();
        require(readiness.equipmentDevices().empty()
                    && readiness.equipment().resources().empty(),
                "equipment reset did not clear deferred readiness state");

        std::cout << "station session contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "station session contract failed: " << error.what() << '\n';
        return 1;
    }
}
