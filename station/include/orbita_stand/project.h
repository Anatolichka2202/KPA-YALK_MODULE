#pragma once

#include <string>
#include <vector>

namespace orbita::stand {

// Registration is a workflow policy, not a property of the ScenarioEngine.
// A free/TU workflow may run without a lifecycle record, while a production
// workflow can require one. The core deliberately does not interpret workflow
// `kind`; deliveries/projects own those semantics.
struct WorkflowRegistrationPolicy {
    bool required = false;
    bool attachIfFound = false;
};

struct WorkflowDefinition {
    std::string id;
    std::string title;
    std::string kind;

    // All resolved paths are absolute/normalized at the project-package
    // boundary. Empty scenarioPath is legal only for a dynamic workflow.
    std::string sourcePath;
    std::string scenarioPath;
    std::string reportPath;
    std::string environmentPath;

    // Optional link to another workflow, e.g. a production flow may declare
    // the TU workflow that is its formal reference without teaching station
    // core what "TU" means.
    std::string referenceWorkflow;

    bool allowDynamicScenario = false;
    bool allowScenarioOverrides = false;
    WorkflowRegistrationPolicy registration;
};

// Product-level project package. It composes existing station profile,
// workflows and optional UI/report/script assets without importing any
// KTMA/UBSI-specific concepts into the common runtime.
struct ProjectDefinition {
    std::string id;
    std::string title;
    std::string version;
    std::string rootDirectory;
    std::string equipmentProfilePath;

    std::vector<std::string> dutTypes;
    std::vector<WorkflowDefinition> workflows;
    std::vector<std::string> screenPaths;
    std::vector<std::string> reportPaths;
    std::vector<std::string> scriptPaths;
};

ProjectDefinition loadProjectPackage(const std::string& projectFile);
std::vector<std::string> validateProjectPackage(const ProjectDefinition& project);

const WorkflowDefinition* findWorkflow(
    const ProjectDefinition& project, const std::string& id) noexcept;

} // namespace orbita::stand
