#include "orbita_stand/desktop_core.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace orbita::stand::desktop;

QString utf8(const std::string& value) { return QString::fromUtf8(value.c_str()); }

QJsonObject componentJson(const ComponentView& component)
{
    QJsonObject result{
        {"id", utf8(component.id)},
        {"kind", utf8(component.kind)},
        {"provider", utf8(component.provider)},
        {"state", toString(component.state)},
        {"diagnostic", utf8(component.diagnostic)}
    };
    if (component.observedAt) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            component.observedAt->time_since_epoch()).count();
        result.insert("observed_at_unix_ms", static_cast<qint64>(ms));
    }
    return result;
}

QJsonObject workflowJson(const WorkflowView& workflow)
{
    return {
        {"id", utf8(workflow.id)},
        {"title", utf8(workflow.title)},
        {"kind", utf8(workflow.kind)},
        {"operator_action", utf8(workflow.action)},
        {"launchable", workflow.launchable},
        {"draft_creation_available", workflow.draftCreationAvailable},
        {"unavailable_reason", utf8(workflow.unavailableReason)}
    };
}

void outputJson(const QJsonObject& result, std::ostream& output)
{
    output << QJsonDocument(result).toJson(QJsonDocument::Indented).constData();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "Usage: station_project_inspect PROJECT.yaml "
                  << "[--edition engineering|tu] [--workflow ID] "
                  << "[--handler ACTION] [--allow-edit]\n"
                  << "Inspects metadata only; never loads device plugins or touches equipment.\n";
        return 2;
    }

    try {
        DesktopPolicy policy;
        const std::string projectFile = argv[1];
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--edition" && i + 1 < argc) {
                const std::string value = argv[++i];
                if (value == "tu") policy.edition = Edition::Tu;
                else if (value == "engineering") policy.edition = Edition::Engineering;
                else throw std::invalid_argument("Unknown desktop edition: " + value);
            } else if (option == "--workflow" && i + 1 < argc) {
                policy.tuWorkflowId = argv[++i];
            } else if (option == "--handler" && i + 1 < argc) {
                policy.supportedActions.insert(argv[++i]);
            } else if (option == "--allow-edit") {
                policy.allowScenarioEditing = true;
            } else {
                throw std::invalid_argument("Unknown or incomplete option: " + option);
            }
        }

        const auto core = DesktopCore::load(projectFile, std::move(policy));
        // A project file is not evidence of a configured or ready station.
        const RuntimeObservation runtime;
        const auto view = core.snapshot(runtime);
        QJsonArray workflows;
        for (const auto& workflow : view.workflows)
            workflows.append(workflowJson(workflow));
        QJsonArray components;
        for (const auto& component : view.components)
            components.append(componentJson(component));
        QJsonArray dutTypes;
        for (const auto& type : view.dutTypes) dutTypes.append(utf8(type));

        outputJson({
            {"contract_schema", view.schema},
            {"inspection_only", true},
            {"hardware_access", false},
            {"edition", toString(view.edition)},
            {"project", QJsonObject{
                {"id", utf8(view.projectId)},
                {"title", utf8(view.projectTitle)},
                {"version", utf8(view.projectVersion)},
                {"dut_types", dutTypes},
                {"catalog_path", utf8(view.catalogPath)}
            }},
            {"profile", QJsonObject{
                {"id", utf8(view.profileId)},
                {"version", utf8(view.profileVersion)},
                {"active_outputs_declared_confirmed", view.activeOutputsDeclaredConfirmed}
            }},
            {"runtime_state", toString(view.runtimeState)},
            {"workflows", workflows},
            {"components", components}
        }, std::cout);
        return 0;
    } catch (const std::exception& error) {
        outputJson({
            {"contract_schema", 1},
            {"inspection_only", true},
            {"hardware_access", false},
            {"error", QString::fromUtf8(error.what())}
        }, std::cerr);
        return 1;
    }
}
