#include "package_smoke.h"
#include "home_page.h"

#include "orbita_stand/config.h"
#include "orbita_stand/project.h"
#include "orbita_stand/run_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QPushButton>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace {

class SmokeEquipment final : public orbita::stand::ICapabilityProvider {
public:
    bool hasCapability(const std::string& capability) const override
    {
        return capability == "package.smoke";
    }

    std::string invoke(const std::string& capability, const std::string& operation,
                       const std::map<std::string, std::string>&) override
    {
        if (capability != "package.smoke" || operation != "verify")
            throw std::runtime_error("Unexpected installed-package smoke invocation");
        return "installed-package-contract=ok\n";
    }

    void safeStopAll() noexcept override {}
};

} // namespace

int runInstalledPackageSmoke()
{
    try {
        // Свободный контур принадлежит MilTechStation, поэтому его вход
        // проверяется до чтения project package КТМА. Так package smoke ловит
        // ошибку, при которой поставка случайно становится его зависимостью.
        HomePage freeHome;
        auto* freeAction = freeHome.findChild<QPushButton*>(
            QStringLiteral("genericFreeAction"));
        if (!freeAction || !freeAction->isEnabled())
            throw std::runtime_error(
                "Free Workspace is unavailable before loading a delivery project");
        bool freeWorkspaceRequested = false;
        QObject::connect(&freeHome, &HomePage::freeWorkspaceRequested, &freeHome,
            [&freeWorkspaceRequested] { freeWorkspaceRequested = true; });
        freeAction->click();
        if (!freeWorkspaceRequested)
            throw std::runtime_error(
                "Free Workspace did not dispatch its independent launch request");

        const QDir applicationDirectory(QCoreApplication::applicationDirPath());
        const auto projectPath = qEnvironmentVariable(
            "MILTECH_PROJECT",
            applicationDirectory.filePath(QStringLiteral("projects/ktma/project.yaml")));
        const auto project = orbita::stand::loadProjectPackage(
            projectPath.toUtf8().toStdString());
        const auto packageErrors = orbita::stand::validateProjectPackage(project);
        if (!packageErrors.empty())
            throw std::runtime_error("Project package validation failed: " + packageErrors.front());

        const auto profile = orbita::stand::loadStandProfile(project.equipmentProfilePath);
        if (profile.id.empty()) throw std::runtime_error("Installed stand profile has no identity");
        const auto* freeWorkflow = orbita::stand::findWorkflow(project, "free");
        if (!freeWorkflow || !freeWorkflow->allowDynamicScenario)
            throw std::runtime_error("Installed project does not expose its dynamic Free workflow");

        orbita::stand::ScenarioDefinition scenario;
        scenario.id = "installed.package.smoke";
        scenario.title = "Installed package smoke run";
        scenario.version = "1";
        scenario.catalogVersion = "package-smoke";
        scenario.objectType = "SMOKE";
        scenario.publicationState = orbita::stand::PublicationState::Published;
        orbita::stand::ScenarioNode step;
        step.id = "package-smoke";
        step.title = "Verify installed runtime";
        step.tuRequirement = "package smoke contract";
        step.procedure = "package.smoke.verify";
        step.requiredCapabilities.insert("package.smoke");
        scenario.steps.push_back(std::move(step));

        orbita::stand::ScenarioEngine engine;
        engine.registerProcedure("package.smoke.verify",
            [](const orbita::stand::ScenarioNode&, orbita::stand::ProcedureContext& context) {
                const auto response = context.equipment.invoke(
                    "package.smoke", "verify", {});
                if (response.find("installed-package-contract=ok") == std::string::npos)
                    return orbita::stand::ProcedureResult{
                        orbita::stand::RunVerdict::Error, "Smoke provider response mismatch", {}};
                return orbita::stand::ProcedureResult{
                    orbita::stand::RunVerdict::Ok, "Installed runtime contract OK", {}};
            });
        SmokeEquipment equipment;
        orbita::stand::ProjectRunContext context;
        context.dutType = "SMOKE";
        context.operatorName = "installed-package-smoke";
        context.attributes["smoke_test"] = "true";
        const auto run = orbita::stand::runProjectWorkflow(
            project, "free", engine, equipment, profile.version,
            "PACKAGE-SMOKE", false, std::move(context), &scenario);
        if (run.verdict != orbita::stand::RunVerdict::Ok)
        {
            const auto detail = !run.steps.empty() ? run.steps.front().message
                : !run.events.empty() ? run.events.front().message : std::string();
            throw std::runtime_error(std::string("Installed package smoke scenario did not pass: ")
                + orbita::stand::toString(run.verdict)
                + (detail.empty() ? std::string() : " · " + detail));
        }

        if (!QDir().mkpath(applicationDirectory.filePath(QStringLiteral("runs"))))
            throw std::runtime_error("Cannot create RunStore parent directory");
        QTemporaryDir runDirectory(applicationDirectory.filePath(
            QStringLiteral("runs/package-smoke-XXXXXX")));
        if (!runDirectory.isValid())
            throw std::runtime_error("Cannot create temporary RunStore directory");
        const auto storePath = QDir(runDirectory.path()).filePath(QStringLiteral("runs.db"));
        {
            orbita::stand::RunStore store(storePath.toUtf8().toStdString());
            store.save(run);
        }
        orbita::stand::RunStore store(storePath.toUtf8().toStdString());
        const auto restored = store.load(run.runId);
        if (!restored || restored->verdict != orbita::stand::RunVerdict::Ok
            || restored->projectId != project.id || restored->workflowId != "free"
            || restored->contextAttributes.find("smoke_test") == restored->contextAttributes.end()
            || restored->evidence.empty())
            throw std::runtime_error("Installed package run did not round-trip through RunStore");

        std::cout << "PACKAGE_SMOKE_OK project=" << project.id
                  << " workflow=free verdict=" << orbita::stand::toString(restored->verdict)
                  << " saved_run=" << restored->runId << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PACKAGE_SMOKE_FAILED: " << error.what() << '\n';
        return 1;
    }
}
