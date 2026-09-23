#include "orbita_stand/project.h"
#include "orbita_stand/yaml_lite.h"

#include <algorithm>
#include <cctype>
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

} // namespace orbita::stand
