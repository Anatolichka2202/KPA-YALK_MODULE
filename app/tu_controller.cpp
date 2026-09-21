#include "tu_controller.h"
#include "tu_report_writer.h"

#include "backend/scenario_yaml.h"
#include "hardware/stand_config.h"
#include "hardware/stand_hardware.h"
#include "procedures/power_procedures.h"
#include "procedures/yalk_procedures.h"
#include "procedures/yalk_verified_procedures.h"
#include "procedures/ytp_procedures.h"
#include "procedures/yvp_procedures.h"
#include "ui/run_journal_overlay.h"
#include "ui/test_page.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QWidget>

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

const QString kUiScenarioCode = QStringLiteral("ULK_COMBINED_CHECK");
const QString kBackendEquipmentCode = QStringLiteral("TU_BACKEND");
const QString kSupplyEquipmentCode = QStringLiteral("AKIP_1160");
const QString kIsdEquipmentCode = QStringLiteral("ISD");
const QString kV7EquipmentCode = QStringLiteral("V7_78");
const QString kGeneratorEquipmentCode = QStringLiteral("RIGOL_DG1022Z");

std::filesystem::path dataPath(const QString& fileName)
{
    const QString deployed = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("data/") + fileName);
    if (QFileInfo::exists(deployed))
        return std::filesystem::path(deployed.toStdWString());
#ifdef TU_SOURCE_DIR
    const QString source = QDir(QString::fromUtf8(TU_SOURCE_DIR))
        .filePath(QStringLiteral("data/") + fileName);
    if (QFileInfo::exists(source))
        return std::filesystem::path(source.toStdWString());
#endif
    throw std::runtime_error(
        QStringLiteral("Не найден data/%1").arg(fileName).toUtf8().toStdString());
}

QString joinedErrors(const std::vector<std::string>& errors)
{
    QStringList lines;
    for (const auto& error : errors) lines << QString::fromStdString(error);
    return lines.join(QLatin1Char('\n'));
}

std::optional<double> eventNumber(const tu::RunEvent& event, const char* key)
{
    const auto found = event.data.find(key);
    if (found == event.data.end()) return std::nullopt;
    try {
        std::size_t parsed = 0;
        const double value = std::stod(found->second, &parsed);
        if (parsed != found->second.size() || !std::isfinite(value)) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

// Keep the canonical scenario node IDs in reports/journal, but adapt the few
// runtime-only support nodes to the current HMI contract. In particular the
// route now calls the contact step yalk_signal_thresholds, while the UI model
// still recognizes the older yalk_contact* family.
std::string runtimeUiNodeId(const std::string& node)
{
    if (node == "yalk_signal_thresholds") return "yalk_contact_thresholds";

    // These are service steps, not acceptance rows. If they are passed through
    // as yalk_* the old sidebar fallback lights the analog row, producing the
    // visible analog -> initial -> analog jump. Keep their progress text, but
    // do not let them masquerade as an acceptance requirement.
    if (node == "yalk_stream") return "support.yalk_stream";
    if (node == "yalk_calibration") return "support.yalk_calibration";
    if (node == "yalk_cleanup") return "support.yalk_cleanup";
    return node;
}

void normalizeContactResultForUi(tu::StepRunResult& step)
{
    if (step.nodeId == "yalk_signal_thresholds")
        step.nodeId = "yalk_contact_thresholds";
    for (auto& child : step.children) normalizeContactResultForUi(child);
}

void normalizeContactResultForUi(tu::ScenarioRunResult& result)
{
    for (auto& step : result.steps) normalizeContactResultForUi(step);
}

// The runtime rail historically listed initial/open-circuit after analog and
// contact even though the scenario executes it first. Reorder the existing
// widgets once; this deliberately does not alter the accepted TU scope.
void reorderTuRequirementRail(TestPage* page)
{
    if (!page) return;
    const QStringList keys{
        QStringLiteral("readiness"),
        QStringLiteral("supply"),
        QStringLiteral("current"),
        QStringLiteral("yalk_initial"),
        QStringLiteral("yalk_analog"),
        QStringLiteral("yalk_accuracy"),
        QStringLiteral("yalk_contact"),
        QStringLiteral("yalk_overload"),
        QStringLiteral("yalk_reference"),
        QStringLiteral("ytp"),
        QStringLiteral("yvp_afc"),
        QStringLiteral("yvp_gain")
    };

    QVector<QWidget*> rows;
    rows.reserve(keys.size());
    for (const auto& key : keys) {
        auto* row = page->findChild<QWidget*>(QStringLiteral("tuRequirement_%1").arg(key));
        if (!row) return;
        rows.push_back(row);
    }

    QWidget* sidebar = rows.front()->parentWidget();
    if (!sidebar || !sidebar->layout()) return;

    QBoxLayout* rail = nullptr;
    auto* outer = sidebar->layout();
    for (int index = 0; index < outer->count(); ++index) {
        auto* item = outer->itemAt(index);
        auto* box = item && item->layout() ? dynamic_cast<QBoxLayout*>(item->layout()) : nullptr;
        if (box && box->indexOf(rows.front()) >= 0) {
            rail = box;
            break;
        }
    }
    if (!rail) return;

    for (auto* row : rows) rail->removeWidget(row);
    for (int index = 0; index < rows.size(); ++index)
        rail->insertWidget(index, rows[index]);
}

} // namespace

TuController::TuController(TestPage* page, QObject* parent)
    : QObject(parent), page_(page)
{
    if (!page_) throw std::invalid_argument("TestPage is required");
    journal_ = new RunJournalOverlay(page_);

    page_->setProductionMode(false);
    reorderTuRequirementRail(page_);
    page_->registerEquipmentRow(kBackendEquipmentCode,
        QStringLiteral("Маршрут и процедуры ТУ"), QStringLiteral("локально"),
        QStringLiteral("Проверка регистрации процедур"));
    page_->registerEquipmentRow(kSupplyEquipmentCode,
        QStringLiteral("АКИП-1160/6 · питание УБСИ"), QStringLiteral("COM из профиля"),
        QStringLiteral("Прибор ещё не проверен"));
    page_->registerEquipmentRow(kIsdEquipmentCode,
        QStringLiteral("ИСД · коммутация воздействий"), QStringLiteral("HTTP из профиля"),
        QStringLiteral("Связь ещё не проверена"));
    page_->registerEquipmentRow(kV7EquipmentCode,
        QStringLiteral("В7-78/1 · эталонное измерение"), QStringLiteral("VISA"),
        QStringLiteral("Прибор ещё не проверен"));
    page_->registerEquipmentRow(kGeneratorEquipmentCode,
        QStringLiteral("Rigol DG-1022Z · ЯВП"), QStringLiteral("VISA"),
        QStringLiteral("Прибор ещё не проверен"));

    connect(page_, &TestPage::equipmentCheckRequested,
            this, [this] { checkBackendReadiness(); });
    connect(page_, &TestPage::stopRequested,
            this, [this] { engine_.requestStop(); });
    connect(page_, &TestPage::skipCurrentStepRequested,
            this, [this] { engine_.requestSkipCurrent(); });
    connect(page_, &TestPage::runRequested,
            this, [this](const QString& scenarioCode, const QString& serial, bool allowPartial) {
                startRun(scenarioCode, serial, allowPartial);
            });

    loadScenario();
    loadHardware();
    registerBuiltInProcedures();
    refreshBackendReadiness();
}

TuController::~TuController()
{
    engine_.requestStop();
    if (hardware_) hardware_->yalk().setLiveYalkSink({});
    if (runThread_) {
        runThread_->wait();
        runThread_ = nullptr;
    }
    if (hardware_) hardware_->safeStop();
}

void TuController::registerProcedure(std::string id, tu::ProcedureFunction procedure)
{
    if (runThread_)
        throw std::logic_error("Нельзя регистрировать процедуру во время выполнения сценария");
    engine_.registerProcedure(std::move(id), std::move(procedure));
    refreshBackendReadiness();
}

void TuController::loadScenario()
{
    try {
        scenario_ = tu::loadScenarioYaml(dataPath(QStringLiteral("ubsi_tu.yaml")));
        scenarioLoaded_ = true;
        scenarioError_.clear();
    } catch (const std::exception& error) {
        scenario_ = {};
        scenarioLoaded_ = false;
        scenarioError_ = QString::fromUtf8(error.what());
    }
}

void TuController::loadHardware()
{
    try {
        const auto config = tu::hardware::loadStandConfig(
            dataPath(QStringLiteral("stand_ktma.yaml")));
        page_->setEquipmentConnection(kSupplyEquipmentCode,
            QStringLiteral("%1 @ %2 бод")
                .arg(QString::fromStdString(config.supply.portName)).arg(config.supply.baudRate));
        page_->setEquipmentConnection(kIsdEquipmentCode,
            QStringLiteral("http://%1:%2")
                .arg(QString::fromStdString(config.isd.host)).arg(config.isd.port));
        page_->setEquipmentConnection(kV7EquipmentCode,
            QString::fromStdString(config.v7.resourceExpressions.front()));
        page_->setEquipmentConnection(kGeneratorEquipmentCode,
            QString::fromStdString(config.generator.resourceExpressions.front()));
        hardware_ = std::make_shared<tu::hardware::StandHardware>(config);

        // reference204 is received continuously by YalkReferenceLink. The HMI
        // gets a throttled 20 Hz copy, while procedure snapshots use their own
        // freshness barriers and therefore cannot consume stale pre-switch UDP.
        const QPointer<TestPage> livePage(page_);
        hardware_->yalk().setLiveYalkSink(
            [this, livePage](const std::vector<tu::hardware::YalkChannelReading>& frame,
                             std::uint64_t sequence) {
                if (!livePage || frame.size() < 100) return;

                std::string node;
                double zero = 0.0, full = 0.0, fullVoltage = 0.0;
                {
                    std::lock_guard<std::mutex> lock(liveStateMutex_);
                    if (!yalkCalibrationValid_ || liveNode_.empty()) return;
                    node = liveNode_;
                    zero = yalkZeroCode_;
                    full = yalkFullCode_;
                    fullVoltage = yalkFullVoltage_;
                }
                if (!(full > zero) || !(fullVoltage > 0.0)) return;

                std::ostringstream values;
                values << std::setprecision(10);
                for (std::size_t index = 0; index < 100; ++index) {
                    if (index) values << ',';
                    values << (frame[index].codeMean - zero) * fullVoltage / (full - zero);
                }

                tu::RunEvent liveEvent{
                    std::chrono::system_clock::now(), node, "BACKGROUND",
                    "Живая телеметрия ЯЛК reference204", tu::RunVerdict::NotRun,
                    {{"section", "YALK"},
                     {"background_mean", values.str()},
                     {"fresh", "true"},
                     {"frame_sequence", std::to_string(sequence)}}};

                QMetaObject::invokeMethod(livePage, [livePage, liveEvent = std::move(liveEvent)] {
                    if (livePage) livePage->setRunEvent(liveEvent);
                }, Qt::QueuedConnection);
            });

        hardwareLoaded_ = true;
        hardwareError_.clear();
    } catch (const std::exception& error) {
        hardware_.reset();
        hardwareLoaded_ = false;
        hardwareError_ = QString::fromUtf8(error.what());
    }
}

void TuController::registerBuiltInProcedures()
{
    if (!hardware_) return;
    tu::procedures::registerPowerProcedures(engine_, hardware_);
    tu::procedures::registerYalkProcedures(engine_, hardware_);
    tu::procedures::registerVerifiedYalkProcedures(engine_, hardware_);

    tu::procedures::OperatorResistanceInput operatorInput =
        [this](const std::string& title, const std::string& prompt, double targetOhms)
            -> std::optional<double> {
        std::optional<double> result;
        const auto ask = [this, &result, title, prompt, targetOhms] {
            bool accepted = false;
            const double value = QInputDialog::getDouble(
                page_, QString::fromUtf8(title.c_str()), QString::fromUtf8(prompt.c_str()),
                targetOhms, 0.0, 10000.0, 3, &accepted, Qt::WindowFlags{}, 0.001);
            if (accepted) result = value;
        };
        if (QThread::currentThread() == page_->thread()) ask();
        else QMetaObject::invokeMethod(page_, ask, Qt::BlockingQueuedConnection);
        return result;
    };
    tu::procedures::registerYtpProcedures(engine_, hardware_, std::move(operatorInput));
    tu::procedures::registerYvpProcedures(engine_, hardware_);
}

void TuController::refreshBackendReadiness()
{
    const QStringList equipment{
        kBackendEquipmentCode, kSupplyEquipmentCode, kIsdEquipmentCode,
        kV7EquipmentCode, kGeneratorEquipmentCode};

    if (!scenarioLoaded_) {
        proceduresReady_ = false;
        page_->setScenarioInfo(kUiScenarioCode, false, false, {},
            QStringLiteral("Не удалось загрузить маршрут ТУ: %1").arg(scenarioError_));
        page_->setEquipmentStatus(kBackendEquipmentCode, false, scenarioError_);
        return;
    }
    if (!hardwareLoaded_) {
        proceduresReady_ = false;
        page_->setScenarioInfo(kUiScenarioCode, false, false, {},
            QStringLiteral("Не удалось загрузить профиль стенда: %1").arg(hardwareError_));
        page_->setEquipmentStatus(kBackendEquipmentCode, false, hardwareError_);
        return;
    }

    const auto errors = engine_.validate(scenario_);
    proceduresReady_ = errors.empty();
    page_->setScenarioInfo(kUiScenarioCode, true, false, equipment,
        QStringLiteral("Маршрут %1 · версия %2")
            .arg(QString::fromStdString(scenario_.id),
                 QString::fromStdString(scenario_.version)));

    page_->setEquipmentStatus(kBackendEquipmentCode, proceduresReady_,
        proceduresReady_
            ? QStringLiteral("Полный тракт зарегистрирован: питание · ЯЛК · ЯТП · ЯВП")
            : QStringLiteral("Backend не готов:\n%1").arg(joinedErrors(errors)));
}

void TuController::checkBackendReadiness()
{
    refreshBackendReadiness();
    if (!proceduresReady_ || !hardware_) return;

    hardwareChecked_ = false;
    bool supplyOk = false, isdOk = false, v7Ok = false, generatorOk = false;

    page_->setEquipmentChecking(kSupplyEquipmentCode,
        QStringLiteral("*IDN? и подтверждение OUTP OFF"));
    try {
        const auto id = hardware_->probeSupplyCold();
        supplyOk = true;
        page_->setEquipmentStatus(kSupplyEquipmentCode, true,
            QStringLiteral("%1 · %2 · выход OFF")
                .arg(QString::fromStdString(hardware_->config().supply.portName),
                     QString::fromStdString(id)));
    } catch (const std::exception& error) {
        page_->setEquipmentStatus(kSupplyEquipmentCode, false, QString::fromUtf8(error.what()));
    }

    page_->setEquipmentChecking(kIsdEquipmentCode, QStringLiteral("Пассивный HTTP probe"));
    try {
        const auto response = hardware_->probeIsd();
        isdOk = true;
        page_->setEquipmentStatus(kIsdEquipmentCode, true,
            QStringLiteral("%1 · HTTP отвечает")
                .arg(QString::fromStdString(hardware_->config().isd.host)));
        Q_UNUSED(response);
    } catch (const std::exception& error) {
        page_->setEquipmentStatus(kIsdEquipmentCode, false, QString::fromUtf8(error.what()));
    }

    page_->setEquipmentChecking(kV7EquipmentCode, QStringLiteral("VISA *IDN?"));
    try {
        const auto id = hardware_->probeV7();
        v7Ok = true;
        page_->setEquipmentStatus(kV7EquipmentCode, true,
            QStringLiteral("%1 · %2")
                .arg(QString::fromStdString(hardware_->v7().resourceName()),
                     QString::fromStdString(id)));
    } catch (const std::exception& error) {
        page_->setEquipmentStatus(kV7EquipmentCode, false, QString::fromUtf8(error.what()));
    }

    page_->setEquipmentChecking(kGeneratorEquipmentCode,
        QStringLiteral("VISA *IDN? и OUTPUT OFF"));
    try {
        const auto id = hardware_->probeGenerator();
        generatorOk = true;
        page_->setEquipmentStatus(kGeneratorEquipmentCode, true,
            QStringLiteral("%1 · %2 · выход OFF")
                .arg(QString::fromStdString(hardware_->generator().resourceName()),
                     QString::fromStdString(id)));
    } catch (const std::exception& error) {
        page_->setEquipmentStatus(kGeneratorEquipmentCode, false, QString::fromUtf8(error.what()));
    }

    hardwareChecked_ = supplyOk && isdOk && v7Ok && generatorOk;
    if (!hardwareChecked_) hardware_->safeStop();
}

void TuController::startRun(const QString& scenarioCode,
                            const QString& objectSerial,
                            bool allowPartial)
{
    Q_UNUSED(allowPartial);
    if (runThread_ || !scenarioLoaded_ || !proceduresReady_
        || !hardware_ || !hardwareChecked_) return;
    if (scenarioCode != kUiScenarioCode) return;
    const std::string serial = objectSerial.trimmed().toStdString();
    if (serial.empty()) return;

    {
        std::lock_guard<std::mutex> lock(liveStateMutex_);
        liveNode_.clear();
        yalkZeroCode_ = 0.0;
        yalkFullCode_ = 0.0;
        yalkFullVoltage_ = 6.2;
        yalkCalibrationValid_ = false;
    }

    page_->setRunInProgress(true, QStringLiteral("Запуск полной проверки УБСИ"));
    if (journal_) journal_->beginRun();

    runThread_ = QThread::create([this, serial] {
        auto result = engine_.run(
            scenario_, serial,
            [this, page = QPointer<TestPage>(page_),
             journal = QPointer<RunJournalOverlay>(journal_)](const tu::RunEvent& event) {
                const std::string uiNode = runtimeUiNodeId(event.nodeId);
                {
                    std::lock_guard<std::mutex> lock(liveStateMutex_);
                    if (event.stage == "START") liveNode_ = uiNode;

                    if (event.nodeId == "yalk_calibration" && event.stage == "MEASUREMENT") {
                        const auto zero = eventNumber(event, "zero_code");
                        const auto full = eventNumber(event, "full_code");
                        const auto scale = eventNumber(event, "scale_voltage_v");
                        if (zero && full && scale && *full > *zero && *scale > 0.0) {
                            yalkZeroCode_ = *zero;
                            yalkFullCode_ = *full;
                            yalkFullVoltage_ = *scale;
                            yalkCalibrationValid_ = true;
                        }
                    }
                }

                if (!page) return;
                tu::RunEvent uiEvent = event;
                uiEvent.nodeId = uiNode;
                QMetaObject::invokeMethod(page, [page, journal, uiEvent = std::move(uiEvent), event] {
                    if (page) page->setRunEvent(uiEvent);
                    if (journal) journal->appendRunEvent(event);
                }, Qt::QueuedConnection);
            });

        if (hardware_) hardware_->safeStop();
        {
            std::lock_guard<std::mutex> lock(liveStateMutex_);
            liveNode_.clear();
            yalkCalibrationValid_ = false;
        }

        QString reportPath;
        try {
            reportPath = writeTuReport(result);
        } catch (const std::exception& error) {
            result.verdict = tu::combineVerdicts(result.verdict, tu::RunVerdict::Error);
            result.events.push_back({std::chrono::system_clock::now(), "report", "ERROR",
                std::string("Не удалось сформировать отчёт: ") + error.what(),
                tu::RunVerdict::Error, {}});
        }

        // The scenario/report keeps canonical node IDs. Only the UI result copy
        // maps the renamed contact step to the existing contact requirement.
        auto uiResult = result;
        normalizeContactResultForUi(uiResult);

        QPointer<TestPage> page(page_);
        QPointer<RunJournalOverlay> journal(journal_);
        QMetaObject::invokeMethod(page_, [page, journal, result = std::move(uiResult), reportPath]() mutable {
            if (!page) return;
            if (journal) journal->finishRun();
            page->setRunInProgress(false);
            page->setRunResult(result, reportPath);
        }, Qt::QueuedConnection);
    });

    connect(runThread_, &QThread::finished, this, [this] {
        if (!runThread_) return;
        runThread_->deleteLater();
        runThread_ = nullptr;
    });
    runThread_->start();
}
