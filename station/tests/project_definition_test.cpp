#include "orbita_stand/config.h"
#include "orbita_stand/project.h"
#include "orbita_stand/parameter_repository.h"
#include "orbita_stand/parameter_procedures.h"

#include <filesystem>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <thread>
#include <chrono>
#include <memory>

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string filename(const std::string& path)
{
    return std::filesystem::u8path(path).filename().string();
}

class AuditEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string& capability) const override
    {
        return capability == "test.echo";
    }

    std::string invoke(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override
    {
        if (capability != "test.echo" || operation != "ping")
            throw std::runtime_error("unexpected equipment invocation");
        const auto found = arguments.find("value");
        if (found == arguments.end()) throw std::runtime_error("missing value");
        invoked = true;
        return "echo=" + found->second + "\n";
    }

    void safeStopAll() noexcept override
    {
        stopped = true;
    }

    bool invoked = false;
    bool stopped = false;
};

class ReferenceVoltageSource final : public IParameterSource {
public:
    std::string sourceId() const override { return "ref-sim"; }
    std::string probe() override { return "ready"; }
    ParameterSample read(const std::string& key) override
    {
        ParameterSample sample;
        sample.parameterKey = key;
        if (key != "voltage") return sample;
        sample.timestamp = std::chrono::system_clock::now();
        sample.sequence = ++sequence_;
        sample.rawValue = 12.0;
        sample.physicalValue = 12.0;
        sample.unit = "В";
        sample.quality = SampleQuality::Good;
        return sample;
    }
    void safeStop() noexcept override {}
private:
    std::uint64_t sequence_ = 0;
};

ScenarioDefinition freeScenario()
{
    ScenarioDefinition scenario;
    scenario.id = "free.contract";
    scenario.title = "Free project contract";
    scenario.version = "1";
    scenario.catalogVersion = "test";
    scenario.objectType = "TEST";
    scenario.publicationState = PublicationState::Published;

    ScenarioNode step;
    step.id = "ok";
    step.title = "OK";
    step.tuRequirement = "free";
    step.procedure = "test.ok";
    step.requiredCapabilities.insert("test.echo");
    scenario.steps.push_back(std::move(step));
    return scenario;
}

} // namespace

int main()
{
    try {
        const std::filesystem::path sourceRoot = ORBITA_SOURCE_DIR;
        const auto project = loadProjectPackage(
            (sourceRoot / "projects/ktma/project.yaml").string());

        require(project.id == "ktma", "KTMA project id mismatch");
        require(project.version == "1.0.0", "KTMA project version mismatch");
        require(project.dutTypes.size() == 1
                && project.dutTypes.front() == "UBSI_468157_002",
            "KTMA project must expose UBSI as the current DUT type");
        require(project.workflows.size() == 11,
            "KTMA V1 package must expose free, normal/climate TU, five normal production packages and climate production workflows");

        const auto* free = findWorkflow(project, "free");
        require(free && free->allowDynamicScenario && free->scenarioPath.empty(),
            "Free workflow must permit a dynamic unregistered scenario");
        require(!free->registration.required && !free->registration.attachIfFound,
            "Free workflow must stay outside the production registrar lifecycle");
        require(free->operatorAction == "station.free" && free->operatorAvailable,
            "Free workflow must open the existing live operator workspace");

        const auto* tu = findWorkflow(project, "tu_normal");
        require(tu && filename(tu->scenarioPath) == "ubsi_ulk_combined_check.yaml",
            "TU normal workflow must select the canonical full UBSI TU scenario");
        require(!tu->registration.required && tu->registration.attachIfFound,
            "TU workflow must run without registration but attach an existing DUT when found");
        require(tu->operatorAction == "ktma.tu" && tu->operatorAvailable,
            "Normal TU workflow must declare an available delivery launch action");

        const auto* production = findWorkflow(project, "production");
        require(production && filename(production->scenarioPath) == "ubsi_production_full.yaml",
            "Production workflow must select the full production scenario");
        require(production->registration.required,
            "Production workflow must require a registered DUT");
        require(production->referenceWorkflow == "tu_normal",
            "Production workflow must retain TU normal as its formal reference");
        require(production->operatorAction == "ktma.production.full"
                && production->operatorAvailable,
            "Normal production workflow must declare an available delivery launch action");

        const std::vector<std::pair<const char*, const char*>> productionPackages{
            {"production_power", "ubsi_production_power.yaml"},
            {"production_yalk", "ubsi_production_yalk.yaml"},
            {"production_ytp", "ubsi_production_ytp.yaml"},
            {"production_yvp", "ubsi_production_yvp.yaml"}};
        for (const auto& [id, scenarioName] : productionPackages) {
            const auto* workflow = findWorkflow(project, id);
            require(workflow && workflow->registration.required
                    && filename(workflow->scenarioPath) == scenarioName
                    && workflow->operatorAvailable,
                (std::string("registered production package is missing or invalid: ") + id).c_str());
        }

        const auto* tuClimate = findWorkflow(project, "tu_climate");
        const auto* tuClimatePlus = findWorkflow(project, "tu_climate_plus");
        const auto* tuClimateMinus = findWorkflow(project, "tu_climate_minus");
        const auto* productionClimate = findWorkflow(project, "production_climate_plus");
        const auto* productionClimateMinus = findWorkflow(project, "production_climate_minus");
        require(!tuClimate && tuClimatePlus && tuClimateMinus
                && filename(tuClimatePlus->environmentPath) == "climate_plus.yaml"
                && filename(tuClimateMinus->environmentPath) == "climate_minus.yaml",
            "TU climate + and − must be separate workflows with separate descriptors");
        require(tuClimatePlus->operatorAction == "ktma.tu" && !tuClimatePlus->operatorAvailable
                && tuClimatePlus->environmentId == "climate_plus"
                && tuClimatePlus->environmentMode == "manual_or_controlled",
            "TU climate + must keep context but remain unavailable to the operator");
        require(tuClimateMinus->operatorAction == "ktma.tu" && !tuClimateMinus->operatorAvailable
                && tuClimateMinus->environmentId == "climate_minus",
            "TU climate − must keep context but remain unavailable to the operator");
        require(productionClimate
                && productionClimate->referenceWorkflow == "tu_climate_plus"
                && productionClimateMinus
                && productionClimateMinus->referenceWorkflow == "tu_climate_minus"
                && !productionClimate->operatorAvailable
                && !productionClimateMinus->operatorAvailable,
            "Climate production +/− workflows must remain unavailable until the physical function exists");

        const auto profile = loadStandProfile(project.equipmentProfilePath);
        require(profile.id == "ktma-main", "KTMA project must compose the verified stand profile");
        require(findComponentByBinding(profile, "power.dut") != nullptr,
            "KTMA project profile must bind power.dut");
        require(findComponentByBinding(profile, "measure.reference") != nullptr,
            "KTMA project profile must bind measure.reference");
        require(findComponentByBinding(profile, "switch_matrix.primary") != nullptr,
            "KTMA project profile must bind switch_matrix.primary");

        ScenarioEngine engine;
        engine.registerProcedure("test.ok", [](const ScenarioNode&, ProcedureContext& context) {
            const auto response = context.equipment.invoke(
                "test.echo", "ping", {{"value", "42"}});
            if (response.find("echo=42") == std::string::npos)
                return ProcedureResult{RunVerdict::Fail, "bad echo", {}};
            return ProcedureResult{RunVerdict::Ok, "ok", {}};
        });
        AuditEquipment equipment;
        auto dynamicScenario = freeScenario();
        ProjectRunContext freeContext;
        freeContext.dutType = "TEST_CELL";
        freeContext.operatorName = "operator";
        freeContext.attributes["mode"] = "live";
        const auto freeRun = runProjectWorkflow(
            project, "free", engine, equipment, profile.version, "SN-FREE", false,
            freeContext, &dynamicScenario);
        require(freeRun.verdict == RunVerdict::Ok,
            "Dynamic free workflow must execute through the common ScenarioEngine");
        require(freeRun.projectId == "ktma" && freeRun.projectVersion == "1.0.0"
                && freeRun.workflowId == "free",
            "Project/workflow identity must be attached to the run result");
        require(freeRun.dutType == "TEST_CELL" && freeRun.operatorName == "operator"
                && freeRun.contextAttributes.at("mode") == "live",
            "Project run context must survive scenario execution");
        require(filename(freeRun.environmentProfile) == "normal.yaml",
            "Workflow environment identity must be retained in the run result");
        require(freeRun.contextAttributes.at("environment_id") == "normal"
                && freeRun.evidence.front().type == "ENVIRONMENT",
            "Environment descriptor and environment evidence must be attached to the run");
        require(equipment.invoked && equipment.stopped,
            "Project workflow must invoke and safe-stop the underlying equipment");

        require(freeRun.evidence.size() >= 4,
            "Project workflow must record command, ack and safe-stop evidence");
        require(freeRun.evidence[1].type == "COMMAND"
                && freeRun.evidence[1].capability == "test.echo"
                && freeRun.evidence[1].operation == "ping"
                && freeRun.evidence[1].data.at("arg.value") == "42",
            "Equipment command evidence is incomplete");
        require(freeRun.evidence[2].type == "COMMAND_ACK"
                && freeRun.evidence[2].verdict == RunVerdict::Ok
                && freeRun.evidence[2].data.at("response").find("echo=42") != std::string::npos,
            "Equipment acknowledgement evidence is incomplete");
        for (std::size_t index = 1; index < freeRun.evidence.size(); ++index) {
            require(freeRun.evidence[index].sequence
                        == freeRun.evidence[index - 1].sequence + 1,
                "Evidence sequence must be contiguous inside one project run");
            require(freeRun.evidence[index].monotonicNs
                        >= freeRun.evidence[index - 1].monotonicNs,
                "Evidence monotonic clock must not move backwards");
        }
        require(freeRun.evidence[freeRun.evidence.size() - 2].type == "SAFETY"
                && freeRun.evidence.back().type == "SAFETY",
            "Scenario cleanup must be represented in technical evidence");

        auto climateProject = project;
        auto climateWorkflow = std::find_if(climateProject.workflows.begin(),
            climateProject.workflows.end(), [](const auto& workflow) {
                return workflow.id == "tu_climate_plus";
            });
        require(climateWorkflow != climateProject.workflows.end(),
            "Climate workflow must be present for confirmation contract test");
        climateWorkflow->allowScenarioOverrides = true;
        auto climateContext = ProjectRunContext{};
        climateContext.attributes["environment_confirmed"] = "true";
        climateContext.attributes["environment_confirmation_source"] = "operator_test";
        bool invokedWithoutConfirmation = false;
        try {
            (void)runProjectWorkflow(climateProject, "tu_climate_plus", engine, equipment,
                profile.version, "SN-CLIMATE", false, ProjectRunContext{}, &dynamicScenario);
        } catch (const std::runtime_error& error) {
            invokedWithoutConfirmation = std::string(error.what()).find("environment confirmation")
                != std::string::npos;
        }
        require(invokedWithoutConfirmation,
            "Manual climate workflow must reject a run without environment confirmation");
        const auto climateRun = runProjectWorkflow(climateProject, "tu_climate_plus", engine,
            equipment, profile.version, "SN-CLIMATE", false, climateContext, &dynamicScenario);
        require(climateRun.verdict == RunVerdict::Ok
                && climateRun.contextAttributes.at("environment_id") == "climate_plus"
                && climateRun.contextAttributes.at("environment_confirmation_source") == "operator_test"
                && climateRun.evidence.front().type == "ENVIRONMENT"
                && climateRun.evidence.front().data.at("environment_mode") == "manual_or_controlled",
            "Confirmed manual climate run must preserve descriptor and confirmation evidence");

        bool productionRejected = false;
        try {
            (void)runProjectWorkflow(
                project, "production", engine, equipment, profile.version, "SN-PROD", false);
        } catch (const std::runtime_error& error) {
            productionRejected = std::string(error.what()).find("registered DUT")
                != std::string::npos;
        }
        require(productionRejected,
            "A registration-required workflow must reject an unresolved DUT before execution");

        // Independent reference-device delivery: no UBSI procedures, registrar
        // or product-specific equipment plugin is involved in the execution.
        const auto reference = loadProjectPackage(
            (sourceRoot / "projects/reference_device/project.yaml").string());
        require(reference.id == "reference_device" && reference.dutTypes.size() == 1
                && reference.dutTypes.front() == "REFERENCE_DEVICE",
            "Independent reference-device project was not loaded");
        require(filename(reference.catalogPath) == "catalog_reference.yaml",
            "The reference project must select its own catalog");
        require(filename(reference.equipmentProfilePath) == "stand_reference_simulator.yaml",
            "The reference project must select its own station profile");
        const auto referenceProfile = loadStandProfile(reference.equipmentProfilePath);
        require(referenceProfile.id == "reference-simulator"
                && !referenceProfile.activeOutputsConfirmed
                && referenceProfile.components.size() == 1
                && referenceProfile.components.front().kind == "parameter_source"
                && referenceProfile.components.front().provider == "miltech.parameter.simulator",
            "The independent project must contain only an inert parameter simulator");
        const auto* referenceWorkflow = findWorkflow(reference, "generic");
        require(referenceWorkflow && referenceWorkflow->operatorAction == "station.generic"
                && referenceWorkflow->operatorAvailable && referenceWorkflow->allowDynamicScenario
                && !referenceWorkflow->registration.required,
            "The independent project must expose a generic operator workflow");
        const auto referenceScenario = loadScenarioYaml(referenceWorkflow->scenarioPath);
        require(referenceScenario.objectType == "REFERENCE_DEVICE"
                && referenceScenario.steps.size() == 1
                && referenceScenario.steps.front().procedure == "parameter.measure",
            "The reference workflow must use a universal parameter measurement");

        ParameterRepository referenceParameters;
        referenceParameters.add(std::make_unique<ReferenceVoltageSource>());
        ScenarioEngine referenceEngine;
        registerParameterProcedures(referenceEngine, referenceParameters);
        require(referenceEngine.validate(referenceScenario).empty(),
            "Independent reference scenario did not validate with generic procedures");
        AuditEquipment referenceEquipment;
        ProjectRunContext referenceContext;
        referenceContext.dutType = "REFERENCE_DEVICE";
        referenceContext.operatorName = "simulator";
        std::thread referencePoll([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            (void)referenceParameters.refresh("ref-sim", "voltage");
        });
        ScenarioRunResult referenceRun;
        try {
            referenceRun = runProjectWorkflow(
                reference, "generic", referenceEngine, referenceEquipment,
                referenceProfile.version, "SIM-001", false, referenceContext);
        } catch (...) {
            referencePoll.join();
            throw;
        }
        referencePoll.join();
        require(referenceRun.verdict == RunVerdict::Ok
                && referenceRun.projectId == "reference_device"
                && referenceRun.workflowId == "generic"
                && referenceRun.dutType == "REFERENCE_DEVICE"
                && referenceRun.steps.size() == 1
                && referenceRun.steps.front().measurements.size() == 1
                && referenceRun.steps.front().measurements.front().measured == 12.0
                && referenceRun.steps.front().measurements.front().unit == "В"
                && referenceEquipment.stopped,
            "Independent reference-device workflow did not execute and record evidence");

        std::cout << "Project package contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Project package contract failed: " << error.what() << '\n';
        return 1;
    }
}
