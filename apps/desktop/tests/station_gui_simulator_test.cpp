#include "universal_mainwindow.h"
#include "generic_check_dialog.h"
#include "scenario_visual_editor.h"
#include "main_page.h"
#include "metadata_service.h"
#include "address_sets_widget.h"
#include "table_widget.h"
#include "detail_view.h"
#include "orbita_stand/run_store.h"
#include "orbita_stand/sample_source.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class SilentSampleSimulator final : public orbita::stand::ISampleSource {
public:
    bool open() override { open_ = true; return true; }
    void close() noexcept override { stop(); open_ = false; }
    bool isOpen() const noexcept override { return open_; }
    bool start() override { running_ = open_; return running_; }
    void stop() noexcept override { running_ = false; }
    bool isRunning() const noexcept override { return running_; }
    void setSamplesCallback(SamplesCallback callback) override { samples_ = std::move(callback); }
    void setErrorCallback(ErrorCallback callback) override { error_ = std::move(callback); }
    void disconnect()
    {
        running_ = false;
        if (error_) error_("simulated disconnect");
    }
private:
    bool open_ = false;
    bool running_ = false;
    SamplesCallback samples_;
    ErrorCallback error_;
};

class MonitorWindow final : public UniversalMainWindow {
public:
    explicit MonitorWindow(bool silent)
    {
        if (silent) {
            integrationStationSession().components().registerKindFactory("sample_source", [this](
                const orbita::stand::ComponentProfile&) -> std::unique_ptr<orbita::stand::IStationComponent> {
                auto source = std::make_unique<SilentSampleSimulator>();
                source_ = source.get();
                return source;
            });
        }
        orbita::stand::StandProfile profile;
        profile.id = "monitor-simulator";
        profile.version = "1";
        profile.components.push_back({"fake-source", "sample_source",
                                      silent ? "test.silent" : "miltech.orbita.simulator", true,
                                      {"telemetry.orbita.sample_source"}, {}});
        integrationConfigureStandProfile(profile, {});
        integrationEnsureStandRuntime();
        require(integrationStandRuntimeReady() && (!silent || source_), "monitor simulator is unavailable");
    }
    SilentSampleSimulator* source() const { return source_; }
    void disconnectSource()
    {
        auto* source = integrationStationSession().components()
            .findAs<orbita::stand::ISampleSource>("telemetry.orbita.sample_source");
        require(source, "sample source disappeared");
        source->close();
    }
private:
    SilentSampleSimulator* source_ = nullptr;
};

class ProfileWindow final : public UniversalMainWindow {
public:
    bool simulatorSelected()
    {
        const auto* source = orbita::stand::findComponentByBinding(
            integrationStandProfile(), "telemetry.orbita.sample_source");
        return integrationProject() && integrationProject()->id == "gui-project"
            && integrationStandRuntimeReady() && source
            && source->provider == "miltech.orbita.simulator";
    }
};

int runMonitorChild(bool silent)
{
    MonitorWindow window(silent);
    window.show();
    auto* open = window.findChild<QPushButton*>(QStringLiteral("genericFreeAction"));
    auto* sets = window.findChild<AddressSetsWidget*>();
    auto* start = window.findChild<QPushButton*>(QStringLiteral("monitorStart"));
    auto* stop = window.findChild<QPushButton*>(QStringLiteral("monitorStop"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("monitorSourceStatus"));
    auto* tableWidget = window.findChild<TableWidget*>();
    require(open && sets && start && stop && status && tableWidget, "monitor UI unavailable");
    open->click();
    sets->setFromSpecs({{"M16P1A11B21T21", "Имитируемый параметр", "Тест"}});
    require(start->isEnabled(), "configured monitoring could not be started");
    auto* table = tableWidget->findChild<QTableWidget*>();
    require(table && table->rowCount() == 1 && table->item(0, 3)->text() == "---",
            "monitor showed a value before receiving samples");
    start->click();
    require(stop->isEnabled() && status->text().contains(QStringLiteral("Ожидание данных")),
            "monitor did not report waiting for the simulator");
    if (!silent) {
        auto* simulatorLabel = window.findChild<QLabel*>(QStringLiteral("monitorSimulatorLabel"));
        require(simulatorLabel && simulatorLabel->isVisible(), "simulator is not labelled in the UI");
        auto waitForValue = [&] {
            QEventLoop events;
            QTimer poll;
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(&poll, &QTimer::timeout, &events, [&] {
                if (table->item(0, 3)->text() == "15") events.quit();
            });
            QObject::connect(&timeout, &QTimer::timeout, &events, &QEventLoop::quit);
            poll.start(20);
            timeout.start(8000);
            events.exec();
            return table->item(0, 3)->text() == "15";
        };
        require(waitForValue(), "raw simulator samples did not appear in the monitor");
        require(QMetaObject::invokeMethod(&window, "onChannelDoubleClicked", Qt::DirectConnection,
                                          Q_ARG(int, 0)), "cannot open parameter details");
        auto* detail = window.findChild<DetailView*>();
        auto* detailLabel = window.findChild<QLabel*>(QStringLiteral("detailSimulatorLabel"));
        require(detail && detailLabel && detailLabel->isVisible(),
                "simulator label disappeared in parameter details");
        require(QMetaObject::invokeMethod(detail, "backToMain", Qt::DirectConnection),
                "cannot return from parameter details");
        stop->click();
        require(table->item(0, 3)->text() == "---", "old value remained after stop");
        start->click();
        require(table->item(0, 3)->text() == "---", "old value reappeared on restart");
        require(waitForValue(), "simulator did not resume decoded data after restart");
        window.disconnectSource();
    } else {
        QEventLoop idle;
        QTimer::singleShot(5300, &idle, &QEventLoop::quit);
        idle.exec();
        require(status->text().contains(QStringLiteral("Нет обновлений"))
                    && table->item(0, 3)->text() == "---",
                "silent source did not report missing updates");
        window.source()->disconnect();
    }
    QEventLoop events;
    QTimer::singleShot(400, &events, &QEventLoop::quit);
    events.exec();
    require(start->isEnabled() && !stop->isEnabled()
                && status->text().contains(QStringLiteral("отключён"))
                && table->item(0, 3)->text() == "---",
            "monitor failed to show simulator disconnect without fabricated values");
    return 0;
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
            if (mode_.startsWith("--lockstop") && node.id == "first") {
                firstEntered_.store(true);
                QElapsedTimer wait;
                wait.start();
                while (!allowMeasurement_.load() && wait.elapsed() < 5000) QThread::msleep(1);
                if (!allowMeasurement_.load()) throw std::runtime_error("lock setup timed out");
            }
            orbita::stand::MeasurementResult measurement;
            measurement.parameterKey = "sim.value";
            measurement.title = "Имитируемое значение";
            measurement.measured = 42;
            measurement.verdict = orbita::stand::RunVerdict::Ok;
            return orbita::stand::ProcedureResult{orbita::stand::RunVerdict::Ok, "Измерено", {measurement}};
        });
    }

    const std::string& runId() const { return runId_; }
    bool firstEntered() const { return firstEntered_.load(); }
    void allowMeasurement() { allowMeasurement_.store(true); }
    int safeStops() const { return safeStops_.load(); }

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
                const std::map<std::string, std::string>&) { return std::string("42"); },
                [this] { ++safeStops_; });
        });
    }
private:
    QString mode_;
    std::set<std::string> previousRunIds_;
    std::string runId_;
    std::atomic_bool firstEntered_{false}, allowMeasurement_{false};
    std::atomic_int safeStops_{0};
};

int runChild(const QString& mode)
{
    if (mode == "--profile-selection") {
        ProfileWindow window;
        require(window.simulatorSelected(), "simulator profile did not override project profile");
        return 0;
    }
    if (mode == "--monitor-disconnect") return runMonitorChild(true);
    if (mode == "--monitor-live") return runMonitorChild(false);
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
    auto* stop = dialog->findChild<QPushButton*>(QStringLiteral("stopGeneric"));
    auto* events = dialog->findChild<QTextBrowser*>(QStringLiteral("genericEventLog"));
    require(scenarios && scenarios->count() == 1 && run && run->isEnabled(),
            "simulated scenario is unavailable in dialog");
    if (mode == "--complete") {
        auto* edit = dialog->findChild<QPushButton*>(QStringLiteral("editGenericScenario"));
        require(edit && edit->isEnabled(), "scenario edit action unavailable");
        bool visualOpened = false;
        bool draftCreated = false;
        QTimer::singleShot(0, [&visualOpened, &draftCreated] {
            if (auto* editor = qobject_cast<ScenarioVisualEditor*>(QApplication::activeModalWidget())) {
                visualOpened = true;
                auto* copy = editor->findChild<QPushButton*>(QStringLiteral("createScenarioVisualDraft"));
                if (copy && copy->isVisible()) {
                    copy->click();
                    draftCreated = editor->currentPath().endsWith(QStringLiteral("_draft.yaml"));
                    auto* add = editor->findChild<QPushButton*>(QStringLiteral("addScenarioRootStep"));
                    auto* save = editor->findChild<QPushButton*>(QStringLiteral("saveScenarioVisual"));
                    if (add && save) { add->click(); save->click(); }
                    else draftCreated = false;
                }
                editor->accept();
            }
        });
        edit->click();
        auto* preview = dialog->findChild<QLabel*>(QStringLiteral("genericResourcePreview"));
        require(visualOpened && draftCreated && scenarios->count() == 2 && preview
                    && preview->text().contains(QStringLiteral("Требует настройки"))
                    && scenarios->currentText().contains(QStringLiteral("Черновик")) && !run->isEnabled(),
                "draft creation did not return to the new non-runnable scenario");
        const QString draftPath = scenarios->currentData().toString();
        for (int i = 0; i < scenarios->count(); ++i) {
            if (!scenarios->itemText(i).contains(QStringLiteral("Черновик"))) {
                scenarios->setCurrentIndex(i);
                break;
            }
        }
        require(run->isEnabled(), "published simulator scenario became unavailable");
        require(QFile::remove(draftPath), "cannot remove isolated test draft");
    }
    QString createdVersion;
    QString createdDraft;
    bool invalidRejected = false;
    bool conflictRejected = false;
    if (mode == "--new-version") {
        auto* edit = dialog->findChild<QPushButton*>(QStringLiteral("editGenericScenario"));
        bool created = false;
        QTimer::singleShot(0, [&] {
            auto* editor = qobject_cast<ScenarioVisualEditor*>(QApplication::activeModalWidget());
            if (!editor) return;
            auto* copy = editor->findChild<QPushButton*>(QStringLiteral("createScenarioVisualDraft"));
            auto* title = editor->findChild<QLineEdit*>(QStringLiteral("scenarioStepTitle"));
            auto* version = editor->findChild<QPushButton*>(QStringLiteral("createRunnableScenarioVersion"));
            auto* procedure = editor->findChild<QLineEdit*>(QStringLiteral("scenarioStepProcedure"));
            if (!copy || !title || !version || !procedure) return;
            copy->click();
            createdDraft = editor->currentPath();
            title->setText(QStringLiteral("Изменённый шаг"));
            // Версия не должна создаваться при неизвестном действии или при
            // совпадении с существующим номером исходной проверки.
            auto rejectVersion = [version] (const QString& number) {
                QTimer::singleShot(0, [number] {
                    if (auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
                        input->setTextValue(number);
                        QTimer::singleShot(0, [] {
                            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->accept();
                        });
                        input->accept();
                    }
                });
                version->click();
            };
            procedure->setText(QStringLiteral("test.missing"));
            rejectVersion(QStringLiteral("2"));
            invalidRejected = editor->currentPath() == createdDraft;
            procedure->setText(QStringLiteral("test.simulator"));
            rejectVersion(QStringLiteral("1"));
            conflictRejected = editor->currentPath() == createdDraft;
            QTimer::singleShot(0, [] {
                if (auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
                    input->setTextValue(QStringLiteral("2"));
                    input->accept();
                }
            });
            version->click();
            createdVersion = editor->currentPath();
            created = createdVersion != createdDraft && createdVersion.endsWith(QStringLiteral("_v2.yaml"));
            editor->accept();
        });
        require(edit, "scenario edit action unavailable");
        edit->click();
        require(invalidRejected && conflictRejected && created && run->isEnabled()
                    && scenarios->currentData().toString() == createdVersion,
                "version validation, conflict handling or selection failed");
    }
    run->click();
    require(!run->isEnabled(), "scenario was not started from the dialog");
    const QString lockConnection = QStringLiteral("gui_stop_lock");
    QSqlDatabase blocker;
    QSqlQuery lock;
    bool locked = false;
    bool released = false;
    bool stopClicked = false;
    QTimer lockPoll;
    if (mode.startsWith("--lockstop")) {
        require(stop && stop->isEnabled() && events, "stop action not available during scenario");
        blocker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), lockConnection);
        blocker.setDatabaseName(QDir(QCoreApplication::applicationDirPath()).filePath("runs/runs.db"));
        require(blocker.open(), "cannot open GUI lock connection");
        lock = QSqlQuery(blocker);
        QObject::connect(&lockPoll, &QTimer::timeout, qApp, [&] {
            if (!locked && window.firstEntered()) {
                locked = lock.exec(QStringLiteral("BEGIN EXCLUSIVE"));
                if (!locked) { window.allowMeasurement(); qApp->exit(4); return; }
                window.allowMeasurement();
            } else if (locked && !stopClicked
                       && events->toPlainText().contains(QStringLiteral("FINISH"))) {
                stopClicked = stop->isEnabled();
                stop->click();
                if (mode == "--lockstop-release")
                    released = lock.exec(QStringLiteral("ROLLBACK"));
            }
        });
        lockPoll.start(5);
    }
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
    if (mode.startsWith("--lockstop")) {
        lockPoll.stop();
        const bool unlocked = released || (locked && lock.exec(QStringLiteral("ROLLBACK")));
        lock = QSqlQuery();
        blocker.close();
        blocker = {};
        QSqlDatabase::removeDatabase(lockConnection);
        auto* summary = dialog->findChild<QLabel*>(QStringLiteral("genericRunStatus"));
        require(status == 0 && unlocked && stopClicked && window.safeStops() > 0 && summary,
                "GUI stop did not finish or did not report the database failure");
        orbita::stand::RunStore store(QDir(QCoreApplication::applicationDirPath())
            .filePath("runs/runs.db").toStdString());
        const auto saved = store.load(window.runId());
        if (mode == "--lockstop") {
            require(summary->text().contains(QStringLiteral("не сохранён"))
                        && saved && saved->steps.empty()
                        && saved->verdict == orbita::stand::RunVerdict::Incomplete,
                    "GUI presented an unconfirmed measurement as persisted");
        } else {
            require(!summary->text().contains(QStringLiteral("не сохранён"))
                        && saved && saved->verdict == orbita::stand::RunVerdict::Aborted
                        && !saved->steps.empty() && saved->steps.front().measurements.size() == 1,
                    "GUI stop did not stop the scenario after the database recovered");
        }
        return 0;
    }
    require(status == 0, "free scenario did not finish");
    auto* report = dialog->findChild<QTextBrowser*>(QStringLiteral("genericReportPreview"));
    if (report && !report->toPlainText().contains(QStringLiteral("Имитируемое значение")))
        std::cerr << report->toPlainText().toUtf8().constData() << '\n';
    require(report && report->toPlainText().contains(QStringLiteral("Имитируемое значение"))
                && report->toPlainText().contains(QStringLiteral("42")),
            "completed GUI report did not show the simulator measurement");
    orbita::stand::RunStore store(QDir(QCoreApplication::applicationDirPath())
        .filePath("runs/runs.db").toStdString());
    if (mode != "--new-version")
        require(store.unfinishedRuns().empty(), "completed GUI run was not finalized");
    const auto persisted = store.load(window.runId());
    require(persisted && (mode != "--new-version" || (persisted->scenarioVersion == "2"
                && persisted->steps.front().title == "Изменённый шаг"))
                && persisted->verdict == orbita::stand::RunVerdict::Ok
                && persisted->steps.size() == 2 && persisted->steps.front().measurements.size() == 1
                && persisted->steps.front().measurements.front().measured == 42,
            "completed GUI run lost its persisted measurement");
    if (mode == "--new-version") {
        require(QFile::remove(createdVersion) && QFile::remove(createdDraft), "cannot remove test versions");
    }
    return 0;
}

int runParent()
{
    // Виджеты не должны принимать исходное нулевое значение за измерение.
    {
        MainPage page;
        MetadataService metadata;
        page.setMetadataService(&metadata);
        const std::vector<orbita::ChannelSpec> channels{{"M16P1A70", "Параметр", "Тест"}};
        page.setChannels(channels);
        page.setSelectedChannel(0);
        auto* table = page.findChild<QTableWidget*>();
        auto* tree = page.findChild<QTreeWidget*>();
        auto* readout = page.findChild<QLabel*>(QStringLiteral("monitorReadoutValue"));
        auto* readoutStatus = page.findChild<QLabel*>(QStringLiteral("monitorReadoutStatus"));
        auto* categoryStatus = page.findChild<QLabel*>(QStringLiteral("monitorCategoryStatus"));
        require(table && table->rowCount() == 1 && tree && tree->topLevelItemCount() == 1,
                "monitoring parameter widgets are missing");
        orbita::Snapshot snapshot;
        snapshot.values.push_back({channels.front().address, 42, false});
        page.updateData(snapshot);
        require(table->item(0, 3)->text() == QStringLiteral("---")
                    && tree->topLevelItem(0)->child(0)->text(2) == QStringLiteral("---")
                    && readout && readout->text() == QStringLiteral("---")
                    && readoutStatus && readoutStatus->text() == QStringLiteral("нет данных")
                    && categoryStatus && !categoryStatus->text().contains(QStringLiteral("все в норме")),
                "invalid telemetry appeared as a measurement");
        snapshot.values.front().valid = true;
        page.updateData(snapshot);
        require(table->item(0, 3)->text() == QStringLiteral("42")
                    && tree->topLevelItem(0)->child(0)->text(2) == QStringLiteral("42")
                    && readout->text() == QStringLiteral("42"),
                "valid telemetry did not reach the monitoring widgets");
        snapshot.values.front().valid = false;
        page.updateData(snapshot);
        require(table->item(0, 3)->text() == QStringLiteral("---")
                    && tree->topLevelItem(0)->child(0)->text(2) == QStringLiteral("---")
                    && readout->text() == QStringLiteral("---"),
                "stale telemetry remained visible after source data became invalid");
    }
    QTemporaryDir directory;
    require(directory.isValid(), "cannot create isolated GUI directory");
    const QDir root(directory.path());
    require(root.mkpath("catalog") && root.mkpath("scenarios") && root.mkpath("profiles"),
            "cannot create GUI fixtures");
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
    require(QFile::copy(source.filePath("data/profiles/stand_ktma.yaml"), root.filePath("profiles/stand_ktma.yaml"))
                && QFile::copy(source.filePath("data/profiles/stand_ktma_simulator.yaml"),
                               root.filePath("profiles/stand_ktma_simulator.yaml")),
            "cannot copy test station profiles");
    QFile project(root.filePath("project.yaml"));
    require(project.open(QIODevice::WriteOnly), "cannot create test project");
    project.write("schema: 1\nid: gui-project\ntitle: GUI project\nversion: 1\n"
                  "equipment_profile: profiles/stand_ktma.yaml\nworkflows:\n  - workflow.yaml\n");
    project.close();
    QFile workflow(root.filePath("workflow.yaml"));
    require(workflow.open(QIODevice::WriteOnly), "cannot create test workflow");
    workflow.write("schema: 1\nid: free\ntitle: Free\nkind: free\nallow_dynamic_scenario: true\n");
    workflow.close();
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
        env.insert("MILTECH_PROJECT", root.filePath(
            mode == "--profile-selection" ? "project.yaml" : "absent-project.yaml"));
        env.insert("MILTECH_STAND_PROFILE",
            mode == "--profile-selection" ? "stand_ktma_simulator.yaml" : "absent-profile.yaml");
        child.setProcessEnvironment(env);
        child.start(executable, {mode});
        require(child.waitForStarted(5000), "GUI child did not start");
        if (!child.waitForFinished(15000)) {
            std::cerr << "GUI child timed out: " << mode.toStdString() << "\n"
                      << child.readAllStandardError().constData();
            child.kill();
            child.waitForFinished();
            throw std::runtime_error("GUI child timed out");
        }
        if (child.exitCode() != expected) {
            std::cerr << child.readAllStandardError().constData();
            throw std::runtime_error("unexpected GUI child exit code");
        }
    };
    execute("--profile-selection", 0);
    execute("--monitor-disconnect", 0);
    execute("--monitor-live", 0);
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
    commands.close();
    execute("--lockstop", 0);
    require(commands.open(QIODevice::ReadOnly)
                && commands.readAll() == beforeRecovery + "first\n",
            "GUI stop allowed another simulator action");
    commands.close();
    execute("--lockstop-release", 0);
    require(commands.open(QIODevice::ReadOnly)
                && commands.readAll() == beforeRecovery + "first\nfirst\n",
            "GUI stop did not prevent the next simulator action after recovery");
    execute("--new-version", 0);
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
