#include "orbita_stand/config.h"
#include "orbita_stand/resource_lease.h"
#include "orbita_stand/scenario.h"

#include <QCoreApplication>
#include <QDir>
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
              "    args:\n"
              "      technical_retries: 2\n"
              "    requires:\n"
              "      - power.dc_supply\n"
              "    resources:\n"
              "      - resource: supply.primary\n"
              "        capability: power.dc_supply\n";
    file.close();
    return path;
}

void verifyLeaseContract()
{
    ResourceLeaseManager leases;

    auto runA = leases.acquire("run-a", {"power.dut", "switch_matrix.primary"});
    require(static_cast<bool>(runA), "initial resource lease was not created");
    require(leases.ownerOf("power.dut") == std::optional<std::string>{"run-a"},
            "resource owner was not recorded");
    require(leases.busy("switch_matrix.primary"),
            "leased resource must report busy");

    auto disjoint = leases.acquire("run-b", {"measure.reference"});
    require(static_cast<bool>(disjoint), "disjoint resource lease must be allowed");

    bool conflict = false;
    try {
        (void)leases.acquire("run-b", {"power.dut", "signal.primary"});
    } catch (const ResourceBusyError& error) {
        conflict = error.resource() == "power.dut" && error.owner() == "run-a";
    }
    require(conflict, "resource conflict must identify the current owner");
    require(!leases.busy("signal.primary"),
            "failed multi-resource acquisition must be atomic");

    {
        auto nested = leases.acquire("run-a", {"power.dut"});
        require(static_cast<bool>(nested), "same owner must be able to re-enter a lease");
        nested.reset();
        require(leases.ownerOf("power.dut") == std::optional<std::string>{"run-a"},
                "nested lease release must not drop the outer ownership");
    }

    auto moved = std::move(disjoint);
    require(static_cast<bool>(moved) && !static_cast<bool>(disjoint),
            "resource lease must be safely movable");
    moved.reset();
    require(!leases.busy("measure.reference"),
            "released disjoint resource must become available");

    runA.reset();
    require(!leases.busy("power.dut") && !leases.busy("switch_matrix.primary"),
            "outer lease reset must release all resources");

    auto runB = leases.acquire("run-b", {"power.dut"});
    require(static_cast<bool>(runB),
            "resource must be acquirable after previous owner released it");
}

ScenarioDefinition singleStepScenario(
    const std::string& id,
    const std::string& procedure)
{
    ScenarioDefinition scenario;
    scenario.id = id;
    scenario.title = id;
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.objectType = "TEST";
    scenario.publicationState = PublicationState::Published;
    ScenarioNode node;
    node.id = id + ".step";
    node.title = "step";
    node.tuRequirement = "test";
    node.procedure = procedure;
    scenario.steps.push_back(std::move(node));
    return scenario;
}

void verifyScenarioEngineRejectsNestedRun()
{
    ScenarioEngine engine;
    FakeEquipment equipment;
    const auto inner = singleStepScenario("inner", "test.inner");
    const auto outer = singleStepScenario("outer", "test.outer");

    engine.registerProcedure("test.inner", [](const ScenarioNode&, ProcedureContext&) {
        return ProcedureResult{RunVerdict::Ok, "inner", {}};
    });
    engine.registerProcedure("test.outer", [&](const ScenarioNode&, ProcedureContext&) {
        bool rejected = false;
        try {
            (void)engine.run(inner, equipment, "profile", "SN", false);
        } catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find("active run") != std::string::npos;
        }
        return ProcedureResult{
            rejected ? RunVerdict::Ok : RunVerdict::Fail,
            rejected ? "nested run rejected" : "nested run unexpectedly executed",
            {}};
    });

    const auto result = engine.run(outer, equipment, "profile", "SN", false);
    require(result.verdict == RunVerdict::Ok,
            "ScenarioEngine must reject a second run while one run is active");
    require(!engine.running(), "ScenarioEngine running flag must clear after completion");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        // Keep the UTF-8 path contract observable even on a CI worker whose
        // TEMP directory itself contains ASCII only.
        QTemporaryDir directory(
            QDir::tempPath() + QStringLiteral("/orbita-сценарий-XXXXXX"));
        require(directory.isValid(), "temporary directory is unavailable");

        const auto scenario = loadScenarioYaml(
            writeScenario(directory).toUtf8().toStdString());
        require(scenario.steps.size() == 1, "scenario step was not parsed");
        const auto& node = scenario.steps.front();
        require(node.requiredCapabilities.size() == 1
                && node.requiredCapabilities.count("power.dc_supply"),
                "legacy compatibility requirement was not parsed");
        require(node.requiredResources.size() == 1,
                "resource requirement was not parsed");
        require(node.requiredResources.front().resource == "supply.primary",
                "resource id was not preserved");
        require(node.requiredResources.front().capability == "power.dc_supply",
                "resource capability was not preserved");
        require(node.policy.technicalRetries == 2,
                "technical retry policy was not parsed into typed scenario state");
        require(!node.arguments.count("technical_retries"),
                "technical retry policy leaked into procedure arguments");

        ScenarioEngine engine;
        engine.registerProcedure("test.resource_read",
            [](const ScenarioNode&, ProcedureContext& context) {
                // During migration an old procedure may still call the
                // capability-only API. A declared resource must take precedence
                // over the duplicate legacy `requires:` entry.
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
                "resource-selected capability must not require a global default binding");
        require(equipment.resourceInvoked,
                "legacy procedure invoke was not routed through the declared resource");
        require(!equipment.legacyInvoked,
                "declared resource must bypass ambiguous global capability routing");
        require(equipment.stopped,
                "successful run must still safe-stop equipment");

        verifyLeaseContract();
        verifyScenarioEngineRejectsNestedRun();

        std::cout << "scenario resource routing contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scenario resource routing contract failed: "
                  << error.what() << '\n';
        return 1;
    }
}
