#include "orbita_stand/project.h"
#include "orbita_stand/config.h"
#include "orbita_stand/yaml_lite.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>

namespace orbita::stand {
namespace {

bool boolean(std::string value, bool fallback, const std::string& field)
{
    if (value.empty()) return fallback;
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (value == "true" || value == "yes" || value == "1" || value == "on") return true;
    if (value == "false" || value == "no" || value == "0" || value == "off") return false;
    throw yaml::Error("Project field " + field + " must be boolean");
}

std::vector<std::string> stringSequence(const yaml::Node* node, const std::string& field)
{
    std::vector<std::string> result;
    if (!node) return result;
    if (!node->isSequence()) throw yaml::Error("Project field " + field + " must be a sequence");
    for (const auto& item : node->sequence) {
        if (!item.isScalar())
            throw yaml::Error("Project field " + field + " must contain scalar items");
        if (!item.scalar.empty()) result.push_back(item.scalar);
    }
    return result;
}

std::string resolvedPath(const std::filesystem::path& root, const std::string& value)
{
    if (value.empty()) return {};
    auto path = std::filesystem::u8path(value);
    if (path.is_relative()) path = root / path;
    return path.lexically_normal().generic_string();
}

bool regularFile(const std::string& path)
{
    if (path.empty()) return false;
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::u8path(path), error) && !error;
}

WorkflowDefinition loadWorkflow(
    const std::string& workflowFile,
    const std::filesystem::path& projectRoot)
{
    const auto root = yaml::parseFile(workflowFile);
    if (!root.isMap()) throw yaml::Error("Workflow root must be a mapping: " + workflowFile);
    if (root.value("schema") != "1")
        throw yaml::Error("Unsupported workflow schema: " + workflowFile);

    WorkflowDefinition workflow;
    workflow.sourcePath = workflowFile;
    workflow.id = root.value("id");
    workflow.title = root.value("title");
    workflow.kind = root.value("kind");
    workflow.scenarioPath = resolvedPath(projectRoot, root.value("scenario"));
    workflow.reportPath = resolvedPath(projectRoot, root.value("report"));
    workflow.environmentPath = resolvedPath(projectRoot, root.value("environment"));
    workflow.referenceWorkflow = root.value("reference_workflow");
    workflow.allowDynamicScenario = boolean(
        root.value("allow_dynamic_scenario"), false, "allow_dynamic_scenario");
    workflow.allowScenarioOverrides = boolean(
        root.value("allow_scenario_overrides"), false, "allow_scenario_overrides");

    if (const auto* registration = root.find("registration")) {
        if (!registration->isMap())
            throw yaml::Error("Workflow registration must be a mapping: " + workflowFile);
        workflow.registration.required = boolean(
            registration->value("required"), false, "registration.required");
        workflow.registration.attachIfFound = boolean(
            registration->value("attach_if_found"), false, "registration.attach_if_found");
    }

    return workflow;
}

void appendMissingFile(
    std::vector<std::string>& errors,
    const std::string& label,
    const std::string& path)
{
    if (!path.empty() && !regularFile(path))
        errors.push_back(label + " not found: " + path);
}

std::int64_t monotonicNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string bounded(std::string value, std::size_t limit = 4096)
{
    if (value.size() <= limit) return value;
    value.resize(limit);
    value += "...[truncated]";
    return value;
}

// Common equipment-boundary audit wrapper. It is independent of Project so
// delivery compatibility runners can already produce evidence while they are
// being migrated to immutable workflow definitions.
class EvidenceProvider final : public ICapabilityProvider {
public:
    EvidenceProvider(ICapabilityProvider& upstream, std::vector<EvidenceEvent>& evidence)
        : upstream_(upstream), evidence_(evidence)
    {
    }

    bool hasCapability(const std::string& capability) const override
    {
        return upstream_.hasCapability(capability);
    }

    bool resourceHasCapability(
        const std::string& resource,
        const std::string& capability) const override
    {
        return upstream_.resourceHasCapability(resource, capability);
    }

    std::string invoke(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override
    {
        recordCommand({}, capability, operation, arguments);
        try {
            auto response = upstream_.invoke(capability, operation, arguments);
            recordAck({}, capability, operation, response);
            return response;
        } catch (const std::exception& error) {
            recordError({}, capability, operation, error.what());
            throw;
        } catch (...) {
            recordError({}, capability, operation, "unknown equipment exception");
            throw;
        }
    }

    std::string invokeResource(
        const std::string& resource,
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override
    {
        recordCommand(resource, capability, operation, arguments);
        try {
            auto response = upstream_.invokeResource(resource, capability, operation, arguments);
            recordAck(resource, capability, operation, response);
            return response;
        } catch (const std::exception& error) {
            recordError(resource, capability, operation, error.what());
            throw;
        } catch (...) {
            recordError(resource, capability, operation, "unknown equipment exception");
            throw;
        }
    }

    void safeStopAll() noexcept override
    {
        try {
            EvidenceEvent request;
            stamp(request, "SAFETY");
            request.operation = "safe_stop_all";
            request.message = "Best-effort safe stop requested";
            request.data["phase"] = "request";
            evidence_.push_back(std::move(request));
        } catch (...) {
            // Evidence allocation must never prevent physical safe-stop.
        }

        upstream_.safeStopAll();

        try {
            EvidenceEvent complete;
            stamp(complete, "SAFETY");
            complete.operation = "safe_stop_all";
            complete.message = "Best-effort safe stop completed";
            complete.data["phase"] = "complete";
            evidence_.push_back(std::move(complete));
        } catch (...) {
        }
    }

private:
    void stamp(EvidenceEvent& event, const std::string& type)
    {
        event.sequence = ++sequence_;
        event.timestamp = std::chrono::system_clock::now();
        event.monotonicNs = monotonicNowNs();
        event.type = type;
    }

    void recordCommand(
        const std::string& resource,
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments)
    {
        EvidenceEvent event;
        stamp(event, "COMMAND");
        event.resource = resource;
        event.capability = capability;
        event.operation = operation;
        event.message = "Equipment command";
        for (const auto& [key, value] : arguments)
            event.data["arg." + key] = bounded(value);
        evidence_.push_back(std::move(event));
    }

    void recordAck(
        const std::string& resource,
        const std::string& capability,
        const std::string& operation,
        const std::string& response)
    {
        EvidenceEvent event;
        stamp(event, "COMMAND_ACK");
        event.resource = resource;
        event.capability = capability;
        event.operation = operation;
        event.message = "Equipment command completed";
        event.verdict = RunVerdict::Ok;
        event.data["response"] = bounded(response);
        evidence_.push_back(std::move(event));
    }

    void recordError(
        const std::string& resource,
        const std::string& capability,
        const std::string& operation,
        const std::string& error)
    {
        EvidenceEvent event;
        stamp(event, "ERROR");
        event.resource = resource;
        event.capability = capability;
        event.operation = operation;
        event.message = "Equipment command failed";
        event.verdict = RunVerdict::Error;
        event.data["error"] = bounded(error);
        evidence_.push_back(std::move(event));
    }

    ICapabilityProvider& upstream_;
    std::vector<EvidenceEvent>& evidence_;
    std::uint64_t sequence_ = 0;
};

} // namespace

ProjectDefinition loadProjectPackage(const std::string& projectFile)
{
    if (projectFile.empty()) throw std::invalid_argument("Project file path is empty");

    const auto source = std::filesystem::absolute(std::filesystem::u8path(projectFile))
                            .lexically_normal();
    const auto root = yaml::parseFile(source.generic_string());
    if (!root.isMap()) throw yaml::Error("Project root must be a mapping");
    if (root.value("schema") != "1") throw yaml::Error("Unsupported project schema");

    ProjectDefinition project;
    project.id = root.value("id");
    project.title = root.value("title");
    project.version = root.value("version");
    project.rootDirectory = source.parent_path().generic_string();
    project.equipmentProfilePath = resolvedPath(
        source.parent_path(), root.value("equipment_profile"));
    project.dutTypes = stringSequence(root.find("dut_types"), "dut_types");

    for (const auto& relative : stringSequence(root.find("workflows"), "workflows")) {
        const auto path = resolvedPath(source.parent_path(), relative);
        if (!regularFile(path)) throw yaml::Error("Workflow file not found: " + path);
        project.workflows.push_back(loadWorkflow(path, source.parent_path()));
    }

    for (const auto& relative : stringSequence(root.find("screens"), "screens"))
        project.screenPaths.push_back(resolvedPath(source.parent_path(), relative));
    for (const auto& relative : stringSequence(root.find("reports"), "reports"))
        project.reportPaths.push_back(resolvedPath(source.parent_path(), relative));
    for (const auto& relative : stringSequence(root.find("scripts"), "scripts"))
        project.scriptPaths.push_back(resolvedPath(source.parent_path(), relative));

    const auto errors = validateProjectPackage(project);
    if (!errors.empty()) {
        std::ostringstream message;
        message << "Invalid project package";
        for (const auto& error : errors) message << "\n- " << error;
        throw yaml::Error(message.str());
    }
    return project;
}

std::vector<std::string> validateProjectPackage(const ProjectDefinition& project)
{
    std::vector<std::string> errors;
    if (project.id.empty()) errors.emplace_back("project id is required");
    if (project.title.empty()) errors.emplace_back("project title is required");
    if (project.version.empty()) errors.emplace_back("project version is required");
    if (project.rootDirectory.empty()) errors.emplace_back("project root directory is required");
    if (project.equipmentProfilePath.empty())
        errors.emplace_back("equipment_profile is required");
    else
        appendMissingFile(errors, "equipment profile", project.equipmentProfilePath);
    if (project.workflows.empty()) errors.emplace_back("at least one workflow is required");

    std::set<std::string> ids;
    for (const auto& workflow : project.workflows) {
        if (workflow.id.empty()) errors.emplace_back("workflow id is required");
        else if (!ids.insert(workflow.id).second)
            errors.emplace_back("duplicate workflow id: " + workflow.id);
        if (workflow.title.empty())
            errors.emplace_back("workflow " + workflow.id + " title is required");
        if (workflow.kind.empty())
            errors.emplace_back("workflow " + workflow.id + " kind is required");
        if (!workflow.allowDynamicScenario && workflow.scenarioPath.empty()) {
            errors.emplace_back(
                "workflow " + workflow.id + " requires scenario or allow_dynamic_scenario");
        }
        appendMissingFile(errors, "workflow " + workflow.id + " scenario", workflow.scenarioPath);
        appendMissingFile(errors, "workflow " + workflow.id + " environment", workflow.environmentPath);
        appendMissingFile(errors, "workflow " + workflow.id + " report", workflow.reportPath);
    }

    for (const auto& workflow : project.workflows) {
        if (workflow.referenceWorkflow.empty()) continue;
        if (workflow.referenceWorkflow == workflow.id) {
            errors.emplace_back("workflow " + workflow.id + " cannot reference itself");
        } else if (!ids.count(workflow.referenceWorkflow)) {
            errors.emplace_back("workflow " + workflow.id + " references missing workflow "
                + workflow.referenceWorkflow);
        }
    }

    for (const auto& path : project.screenPaths) appendMissingFile(errors, "screen", path);
    for (const auto& path : project.reportPaths) appendMissingFile(errors, "report", path);
    for (const auto& path : project.scriptPaths) appendMissingFile(errors, "script", path);
    return errors;
}

const WorkflowDefinition* findWorkflow(
    const ProjectDefinition& project, const std::string& id) noexcept
{
    const auto iterator = std::find_if(project.workflows.begin(), project.workflows.end(),
        [&](const WorkflowDefinition& workflow) { return workflow.id == id; });
    return iterator == project.workflows.end() ? nullptr : &*iterator;
}

ScenarioRunResult runScenarioWithEvidence(
    ScenarioEngine& engine,
    ICapabilityProvider& equipment,
    const ScenarioDefinition& scenario,
    std::string profileVersion,
    std::string objectSerial,
    bool allowPartial,
    std::function<void(const RunEvent&)> progressSink)
{
    std::vector<EvidenceEvent> evidence;
    evidence.reserve(64);
    EvidenceProvider auditedEquipment(equipment, evidence);
    auto result = engine.run(
        scenario, auditedEquipment, std::move(profileVersion), std::move(objectSerial),
        allowPartial, std::move(progressSink));
    result.evidence = std::move(evidence);
    return result;
}

ScenarioRunResult runProjectWorkflow(
    const ProjectDefinition& project,
    const std::string& workflowId,
    ScenarioEngine& engine,
    ICapabilityProvider& equipment,
    std::string profileVersion,
    std::string objectSerial,
    bool allowPartial,
    ProjectRunContext context,
    const ScenarioDefinition* scenarioOverride,
    std::function<void(const RunEvent&)> progressSink)
{
    const auto* workflow = findWorkflow(project, workflowId);
    if (!workflow) throw std::invalid_argument("Project workflow not found: " + workflowId);

    if (workflow->registration.required && !context.dutRegistered) {
        throw std::runtime_error(
            "Workflow " + workflowId + " requires a registered DUT");
    }

    ScenarioDefinition loadedScenario;
    const ScenarioDefinition* scenario = scenarioOverride;
    if (scenarioOverride) {
        if (!workflow->allowDynamicScenario && !workflow->allowScenarioOverrides) {
            throw std::runtime_error(
                "Workflow " + workflowId + " does not allow a scenario override");
        }
    } else {
        if (workflow->scenarioPath.empty()) {
            if (workflow->allowDynamicScenario) {
                throw std::runtime_error(
                    "Workflow " + workflowId + " requires a caller-supplied dynamic scenario");
            }
            throw std::runtime_error("Workflow " + workflowId + " has no scenario");
        }
        loadedScenario = loadScenarioYaml(workflow->scenarioPath);
        scenario = &loadedScenario;
    }

    auto result = runScenarioWithEvidence(
        engine, equipment, *scenario, std::move(profileVersion), std::move(objectSerial),
        allowPartial, std::move(progressSink));
    result.projectId = project.id;
    result.projectVersion = project.version;
    result.workflowId = workflow->id;
    result.dutType = std::move(context.dutType);
    result.dutId = std::move(context.dutId);
    result.operatorName = std::move(context.operatorName);
    result.environmentProfile = workflow->environmentPath;
    result.contextAttributes = std::move(context.attributes);
    return result;
}

} // namespace orbita::stand
