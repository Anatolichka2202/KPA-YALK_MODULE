#include "orbita_stand/config.h"
#include "orbita_stand/scenario.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class FakeEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string&) const override
    {
        return false;
    }

    std::string invoke(
        const std::string&,
        const std::string&,
        const std::map<std::string, std::string>&) override
    {
        legacyInvoked = true;
        throw std::runtime_error("legacy capability routing must not be used");
    }

    bool resourceHasCapability(
        const std::string& resource,
        const std::string& capability) const override
    {
        return resourceReady
            && resource == "supply.primary"
            && capability == "power.dc_supply";
    }

    std::string invokeResource(
        const std::string& resource,
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>&) override
    {
        if (!resourceHasCapability(resource, capability)) {
            throw std::runtime_error("unexpected resource/capability");
        }
        if (operation != "read_state") {
            throw std::runtime_error("unexpected resource operation");
        }
        resourceInvoked = true;
        return "status=ready\nvolts=27.0\n";
    }

    void safeStopAll() noexcept override
    {
        stopped = true;
    }

    bool resourceReady = false;
    bool resourceInvoked = false;
    bool legacyInvoked = false;
    bool stopped = false;
};

QString writeScenario(QTemporaryDir& directory)
{
    const QString path = directory.filePath(QStringLiteral("resource.yaml"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        throw std::runtime_error("cannot create temporary scenario");
    }
    QTextStream stream(&file);
    stream << "schema: 1\n"
              "id: test.resource\n"
              "title: Resource routing contract\n"
              "version: 1\n"
              "catalog_version: 1\n"
              "object_type: TEST\n"
              "state: published\n"
              "steps:\n"
              "  - id: read-primary\n"
              "    title: Read primary supply\n"
              "    tu: 1.1\n"
              "    procedure: test.resource_read\n"
              "    resources:\n"
              "      - resource: supply.primary\n"
              "        capability: power.dc_supply\n";
    file.close();
    return path;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory is unavailable");

        const auto scenario = loadScenarioYaml(
            writeScenario(directory).toUtf8().toStdString());
        require(scenario.steps.size() == 1, "scenario step was not parsed");
        const auto& node = scenario.steps.front();
        require(node.requiredCapabilities.empty(),
                "resource requirement must not become legacy capability routing");
        require(node.requiredResources.size() == 1,
                "resource requirement was not parsed");
        require(node.requiredResources.front().resource == "supply.primary",
                "resource id was not preserved");
        require(node.requiredResources.front().capability == "power.dc_supply",
                "resource capability was not preserved");

        ScenarioEngine engine;
        engine.registerProcedure("test.resource_read",
            [](const ScenarioNode&, ProcedureContext& context) {
                // Existing procedures still call the capability-only API. The
                // engine must transparently route this call through the unique
                // logical resource declared by the current scenario node.
                const auto response = context.equipment.invoke(
                    "power.dc_supply", "read_state", {});
                if (response.find("status=ready") == std::string::npos) {
                    throw std::runtime_error("resource response is not ready");
                }
                return ProcedureResult{RunVerdict::Ok, "resource routed", {}};
            });

        FakeEquipment equipment;
        auto missing = engine.run(scenario, equipment, "profile-1", "SN-1", false);
        require(missing.verdict == RunVerdict::Incomplete,
                "missing logical resource must make the run incomplete");
        require(!equipment.resourceInvoked,
                "procedure must not run when required resource is unavailable");
        require(!equipment.legacyInvoked,
                "missing-resource preflight must happen before any legacy invoke");

        equipment.resourceReady = true;
        equipment.stopped = false;
        auto ready = engine.run(scenario, equipment, "profile-1", "SN-1", false);
        require(ready.verdict == RunVerdict::Ok,
                "available resource/capability must permit the procedure");
        require(equipment.resourceInvoked,
                "legacy procedure invoke was not routed through the declared resource");
        require(!equipment.legacyInvoked,
                "declared resource must bypass ambiguous global capability routing");
        require(equipment.stopped,
                "successful run must still safe-stop equipment");

        std::cout << "scenario resource routing contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scenario resource routing contract failed: "
                  << error.what() << '\n';
        return 1;
    }
}
