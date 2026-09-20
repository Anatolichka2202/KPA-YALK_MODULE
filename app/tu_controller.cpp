#include "tu_controller.h"

#include "backend/scenario_yaml.h"
#include "hardware/stand_config.h"
#include "hardware/stand_hardware.h"
#include "procedures/power_procedures.h"
#include "ui/test_page.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

const QString kUiScenarioCode = QStringLiteral("ULK_COMBINED_CHECK");
const QString kBackendEquipmentCode = QStringLiteral("TU_BACKEND");
const QString kSupplyEquipmentCode = QStringLiteral("AKIP_1160");

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

    page_->setProductionMode(false);
    page_->registerEquipmentRow(
        kBackendEquipmentCode,
        QStringLiteral("Маршрут и процедуры ТУ"),
        QStringLiteral("локально"),
        QStringLiteral("Проверка регистрации процедур"));
    page_->registerEquipmentRow(
        kSupplyEquipmentCode,
        QStringLiteral("АКИП-1160/6 · питание УБСИ"),
        QStringLiteral("COM из профиля стенда"),
        QStringLiteral("Физический прибор ещё не проверен"));

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
        const QString connection = QStringLiteral("%1 @ %2 бод")
            .arg(QString::fromStdString(config.supply.portName))
            .arg(config.supply.baudRate);
        page_->setEquipmentConnection(kSupplyEquipmentCode, connection);
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
    // Эти callbacks намеренно не имитируют ЯЛК/ЯТП/ЯВП: пока соответствующий
    // вертикальный срез не перенесён, они только фиксируют INCOMPLETE.
    tu::procedures::registerUnavailableProcedures(engine_);
}

void TuController::refreshBackendReadiness()
{
    if (!scenarioLoaded_) {
        proceduresReady_ = false;
        page_->setScenarioInfo(kUiScenarioCode, false, false, {},
                               QStringLiteral("Не удалось загрузить маршрут ТУ: %1")
                                   .arg(scenarioError_));
        page_->setEquipmentStatus(kBackendEquipmentCode, false, scenarioError_);
        return;
    }

    if (!hardwareLoaded_) {
        proceduresReady_ = false;
        page_->setScenarioInfo(
            kUiScenarioCode, false, false, {},
            QStringLiteral("Не удалось загрузить профиль стенда: %1").arg(hardwareError_));
        page_->setEquipmentStatus(kBackendEquipmentCode, false, hardwareError_);
        page_->setEquipmentStatus(kSupplyEquipmentCode, false, hardwareError_);
        return;
    }

    const auto errors = engine_.validate(scenario_);
    proceduresReady_ = errors.empty();

    page_->setScenarioInfo(
        kUiScenarioCode,
        true,
        false,
        {kBackendEquipmentCode, kSupplyEquipmentCode},
        QStringLiteral("Маршрут %1 · версия %2")
            .arg(QString::fromStdString(scenario_.id),
                 QString::fromStdString(scenario_.version)));

    if (proceduresReady_) {
        page_->setEquipmentStatus(
            kBackendEquipmentCode, true,
            QStringLiteral(
                "Power-slice подключён к реальному АКИП/ROKT; неперенесённые ЯЛК/ЯТП/ЯВП возвращают НЕПОЛНАЯ без воздействия"));
    } else {
        page_->setEquipmentStatus(
            kBackendEquipmentCode, false,
            QStringLiteral("Backend ещё не готов:\n%1").arg(joinedErrors(errors)));
    }

    if (hardwareChecked_) {
        page_->setEquipmentStatus(
            kSupplyEquipmentCode, true,
            QStringLiteral("АКИП идентифицирован; выход подтверждён как OFF"));
    }
}

void TuController::checkBackendReadiness()
{
    page_->setEquipmentChecking(
        kBackendEquipmentCode, QStringLiteral("Проверка регистрации процедур"));
    page_->setEquipmentChecking(
        kSupplyEquipmentCode, QStringLiteral("*IDN? и подтверждение OUTP OFF"));
    refreshBackendReadiness();

    if (!proceduresReady_ || !hardware_) {
        if (!hardware_)
            page_->setEquipmentStatus(kSupplyEquipmentCode, false, hardwareError_);
        return;
    }

    try {
        const std::string identity = hardware_->probeSupplyCold();
        hardwareChecked_ = true;
        page_->setEquipmentStatus(
            kSupplyEquipmentCode, true,
            QStringLiteral("%1 · %2 · выход OFF")
                .arg(QString::fromStdString(hardware_->config().supply.portName),
                     QString::fromStdString(identity)));
    } catch (const std::exception& error) {
        hardwareChecked_ = false;
        if (hardware_) hardware_->safeStop();
        page_->setEquipmentStatus(
            kSupplyEquipmentCode, false, QString::fromUtf8(error.what()));
    }
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

    page_->setRunInProgress(true, QStringLiteral("Запуск маршрута ТУ"));

    runThread_ = QThread::create([this, serial] {
        auto result = engine_.run(
            scenario_, serial,
            [page = QPointer<TestPage>(page_)](const tu::RunEvent& event) {
                if (!page) return;
                QMetaObject::invokeMethod(
                    page,
                    [page, event] {
                        if (page) page->setRunEvent(event);
                    },
                    Qt::QueuedConnection);
            });

        // Независимый safety net: даже если сценарий закончился ERROR/ABORTED и
        // до power.off не дошёл, выход источника не остаётся включённым.
        if (hardware_) hardware_->safeStop();

        QPointer<TestPage> page(page_);
        QMetaObject::invokeMethod(
            page_,
            [page, result = std::move(result)]() mutable {
                if (!page) return;
                page->setRunInProgress(false);
                page->setRunResult(result);
            },
            Qt::QueuedConnection);
    });

    connect(runThread_, &QThread::finished, this, [this] {
        if (!runThread_) return;
        runThread_->deleteLater();
        runThread_ = nullptr;
    });
    runThread_->start();
}
