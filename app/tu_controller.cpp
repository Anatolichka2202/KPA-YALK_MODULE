#include "tu_controller.h"

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

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <filesystem>
#include <memory>
#include <optional>
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

} // namespace

TuController::TuController(TestPage* page, QObject* parent)
    : QObject(parent), page_(page)
{
    if (!page_) throw std::invalid_argument("TestPage is required");
    journal_ = new RunJournalOverlay(page_);

    page_->setProductionMode(false);
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

    page_->setRunInProgress(true, QStringLiteral("Запуск полной проверки УБСИ"));
    if (journal_) journal_->beginRun();

    runThread_ = QThread::create([this, serial] {
        auto result = engine_.run(
            scenario_, serial,
            [page = QPointer<TestPage>(page_),
             journal = QPointer<RunJournalOverlay>(journal_)](const tu::RunEvent& event) {
                if (!page) return;
                QMetaObject::invokeMethod(page, [page, journal, event] {
                    if (page) page->setRunEvent(event);
                    if (journal) journal->appendRunEvent(event);
                }, Qt::QueuedConnection);
            });

        if (hardware_) hardware_->safeStop();

        QPointer<TestPage> page(page_);
        QPointer<RunJournalOverlay> journal(journal_);
        QMetaObject::invokeMethod(page_, [page, journal, result = std::move(result)]() mutable {
            if (!page) return;
            if (journal) journal->finishRun();
            page->setRunInProgress(false);
            page->setRunResult(result);
        }, Qt::QueuedConnection);
    });

    connect(runThread_, &QThread::finished, this, [this] {
        if (!runThread_) return;
        runThread_->deleteLater();
        runThread_ = nullptr;
    });
    runThread_->start();
}
