#include "universal_mainwindow.h"
#include "generic_check_dialog.h"
#include "orbita_stand/run_store.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class SimulatorWindow final : public UniversalMainWindow {
public:
    explicit SimulatorWindow(QString mode) : mode_(std::move(mode))
    {
        orbita::stand::ProjectDefinition project;
        project.id = "gui-test";
        project.version = "1";
        orbita::stand::WorkflowDefinition workflow;
        workflow.id = "free";
        workflow.title = "Имитатор";
        workflow.allowDynamicScenario = true;
        project.workflows.push_back(workflow);
        integrationConfigureProject(std::move(project));

        orbita::stand::StandProfile profile;
        profile.id = "simulator";
        profile.version = "1";
        integrationConfigureStandProfile(profile, {});
        integrationEnsureStandRuntime();
        if (!integrationStandRuntimeReady()) {
            for (const auto* log : findChildren<QTextEdit*>())
                std::cerr << log->toPlainText().toUtf8().constData() << '\n';
        }
        require(integrationStandRuntimeReady(), "simulator runtime not ready");
        orbita::stand::RunStore existing(QDir(QCoreApplication::applicationDirPath())
            .filePath("runs/runs.db").toStdString());
        for (const auto& run : existing.unfinishedRuns()) previousRunIds_.insert(run.runId);
        integrationScenarioEngine()->registerProcedure("test.simulator", [this](
            const orbita::stand::ScenarioNode& node, orbita::stand::ProcedureContext& context) {
            QFile commands(QDir(QCoreApplication::applicationDirPath()).filePath("commands.log"));
            if (!commands.open(QIODevice::Append)) throw std::runtime_error("command log error");
            commands.write(node.id.data(), static_cast<qint64>(node.id.size()));
            commands.write("\n");
            commands.close();
            if (mode_ == "--interrupt" && node.id == "second") std::_Exit(23);
            (void)context.equipment.invoke("test.simulator", "measure", {});
            orbita::stand::MeasurementResult measurement;
            measurement.parameterKey = "sim.value";
            measurement.title = "Имитируемое значение";
            measurement.measured = 42;
            measurement.verdict = orbita::stand::RunVerdict::Ok;
            return orbita::stand::ProcedureResult{orbita::stand::RunVerdict::Ok, "Измерено", {measurement}};
        });
    }

    const std::string& runId() const { return runId_; }

protected:
    void integrationPrepareEquipmentForScenario(const orbita::stand::ScenarioDefinition&,
        const std::shared_ptr<orbita::stand::StationSession::Activity>& activity) override
    {
        const auto path = QDir(QCoreApplication::applicationDirPath()).filePath("runs/runs.db");
        orbita::stand::RunStore store(path.toStdString());
        const auto runs = store.unfinishedRuns();
        const auto current = std::find_if(runs.begin(), runs.end(), [this](const auto& run) {
            return !previousRunIds_.count(run.runId);
        });
        require(runs.size() == previousRunIds_.size() + 1 && current != runs.end()
                    && current->steps.empty() && current->contextAttributes.count("scenario_definition_json"),
                "initial snapshot missing before preparation");
        runId_ = current->runId;
        QFile idFile(QDir(QCoreApplication::applicationDirPath()).filePath("run" + mode_ + ".txt"));
        require(idFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot record run id");
        idFile.write(runId_.data(), static_cast<qint64>(runId_.size()));
        idFile.close();
        if (mode_ == "--interrupt-before") std::_Exit(22);
        integrationStationSession().prepare(activity, [this](orbita::stand::StationSession::Preparation&) {
            integrationEquipmentRegistry()->bind("test.simulator", [](const std::string&,
                const std::map<std::string, std::string>&) { return std::string("42"); });
        });
    }
private:
    QString mode_;
    std::set<std::string> previousRunIds_;
    std::string runId_;
};

int runChild(const QString& mode)
{
    SimulatorWindow window(mode);
    window.show();
    if (mode == "--recover" || mode == "--recover-initial") {
        QFile idFile(QDir(QCoreApplication::applicationDirPath()).filePath(
            mode == "--recover" ? "run--interrupt.txt" : "run--interrupt-before.txt"));
        require(idFile.open(QIODevice::ReadOnly), "missing interrupted run id");
        const auto interruptedId = QString::fromUtf8(idFile.readAll());
        auto* action = window.findChild<QAction*>(QStringLiteral("unfinishedRunsAction"));
        require(action, "recovery action not found");
        bool inspected = false;
        QTimer::singleShot(0, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) return;
            inspected = box->text().contains(interruptedId) && (mode == "--recover-initial"
                ? box->text().contains(QStringLiteral("GUI simulator"))
                    && !box->text().contains(QStringLiteral("Имитируемое значение"))
                : box->text().contains(QStringLiteral("Имитируемое значение"))
                    && box->text().contains(QStringLiteral("42"))
                    && box->text().contains(QStringLiteral("Первый шаг")));
            box->accept();
        });
        action->trigger();
        require(inspected, "recovery dialog did not show the saved measurement");
        return 0;
    }
    require(QMetaObject::invokeMethod(&window, "openGenericCheck", Qt::DirectConnection),
            "cannot open free scenario dialog");
    auto* dialog = window.findChild<GenericCheckDialog*>();
    require(dialog, "free scenario dialog not found");
    auto* scenarios = dialog->findChild<QComboBox*>(QStringLiteral("genericScenario"));
    auto* run = dialog->findChild<QPushButton*>(QStringLiteral("runGeneric"));
    require(scenarios && scenarios->count() == 1 && run && run->isEnabled(),
            "simulated scenario is unavailable in dialog");
    run->click();
    require(!run->isEnabled(), "scenario was not started from the dialog");
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, qApp, [] { qApp->exit(3); });
    timeout.start(10000);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, qApp, [run] {
        if (run->isEnabled()) qApp->quit();
    });
    poll.start(10);
    const int status = qApp->exec();
    require(status == 0, "free scenario did not finish");
    auto* report = dialog->findChild<QTextBrowser*>(QStringLiteral("genericReportPreview"));
    if (report && !report->toPlainText().contains(QStringLiteral("Имитируемое значение")))
        std::cerr << report->toPlainText().toUtf8().constData() << '\n';
    require(report && report->toPlainText().contains(QStringLiteral("Имитируемое значение"))
                && report->toPlainText().contains(QStringLiteral("42")),
            "completed GUI report did not show the simulator measurement");
    orbita::stand::RunStore store(QDir(QCoreApplication::applicationDirPath())
        .filePath("runs/runs.db").toStdString());
    require(store.unfinishedRuns().empty(), "completed GUI run was not finalized");
    const auto persisted = store.load(window.runId());
    require(persisted && persisted->verdict == orbita::stand::RunVerdict::Ok
                && persisted->steps.size() == 2 && persisted->steps.front().measurements.size() == 1
                && persisted->steps.front().measurements.front().measured == 42,
            "completed GUI run lost its persisted measurement");
    return 0;
}

int runParent()
{
    QTemporaryDir directory;
    require(directory.isValid(), "cannot create isolated GUI directory");
    const QDir root(directory.path());
    require(root.mkpath("catalog") && root.mkpath("scenarios"), "cannot create GUI fixtures");
    const QDir source(QStringLiteral(STATION_GUI_TEST_SOURCE_DIR));
    const QDir sourceCatalog(source.filePath("data/catalog"));
    QDirIterator files(sourceCatalog.path(), QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const auto from = files.next();
        const auto to = root.filePath("catalog/" + sourceCatalog.relativeFilePath(from));
        require(QDir().mkpath(QFileInfo(to).absolutePath()) && QFile::copy(from, to),
                "cannot copy test catalog");
    }
    require(QFile::copy(source.filePath("orbita/config/address/parameters.db"), root.filePath("parameters.db")),
            "cannot copy test parameters database");
    const auto executable = root.filePath("station_gui_test.exe");
    require(QFile::copy(QCoreApplication::applicationFilePath(), executable), "cannot isolate GUI executable");
    QFile scenario(root.filePath("scenarios/simulator.yaml"));
    require(scenario.open(QIODevice::WriteOnly), "cannot create simulator scenario");
    scenario.write("schema: 1\nid: gui.simulator\ntitle: GUI simulator\nversion: 1\n"
                   "catalog_version: 1\nobject_type: TEST\nstate: published\nsteps:\n"
                   "  - id: first\n    title: Первый шаг\n    tu: test.1\n    procedure: test.simulator\n"
                   "  - id: second\n    title: Второй шаг\n    tu: test.2\n    procedure: test.simulator\n");
    scenario.close();
    auto execute = [&](const QString& mode, int expected) {
        QProcess child;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "offscreen");
        env.insert("MILTECH_PROJECT", root.filePath("absent-project.yaml"));
        child.setProcessEnvironment(env);
        child.start(executable, {mode});
        require(child.waitForStarted(5000) && child.waitForFinished(15000), "GUI child timed out");
        if (child.exitCode() != expected) {
            std::cerr << child.readAllStandardError().constData();
            throw std::runtime_error("unexpected GUI child exit code");
        }
    };
    execute("--complete", 0);
    execute("--interrupt-before", 22);
    execute("--recover-initial", 0);
    execute("--interrupt", 23);
    QFile commands(root.filePath("commands.log"));
    require(commands.open(QIODevice::ReadOnly), "missing simulator command log");
    const auto beforeRecovery = commands.readAll();
    require(beforeRecovery == "first\nsecond\nfirst\nsecond\n",
            "unexpected simulator commands before recovery");
    commands.close();
    execute("--recover", 0);
    require(commands.open(QIODevice::ReadOnly) && commands.readAll() == beforeRecovery,
            "recovery started simulator commands");
    return 0;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try {
        return app.arguments().size() > 1 && app.arguments().at(1).startsWith("--")
            ? runChild(app.arguments().at(1)) : runParent();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
