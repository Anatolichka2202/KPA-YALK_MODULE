#include "orbita_stand/execution_runtime.h"
#include "orbita_stand/station_session.h"

#include <QCoreApplication>
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

        // Empty kind selection must instantiate all registered non-equipment
        // kinds, while never trying to send equipment through ComponentRuntime.
        session.configure(
            profile,
            pluginDirectory.path().toUtf8().toStdString(),
            {});

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

        std::cout << "station session contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "station session contract failed: " << error.what() << '\n';
        return 1;
    }
}
