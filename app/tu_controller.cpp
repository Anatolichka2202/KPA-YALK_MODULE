#include "tu_controller.h"

#include "backend/scenario_yaml.h"
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

std::filesystem::path scenarioPath()
{
    const QString deployed = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("data/ubsi_tu.yaml"));
    if (QFileInfo::exists(deployed))
        return std::filesystem::path(deployed.toStdWString());

#ifdef TU_SOURCE_DIR
    const QString source = QDir(QString::fromUtf8(TU_SOURCE_DIR))
        .filePath(QStringLiteral("data/ubsi_tu.yaml"));
    if (QFileInfo::exists(source))
        return std::filesystem::path(source.toStdWString());
#endif

    throw std::runtime_error("Не найден data/ubsi_tu.yaml");
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
        QStringLiteral("Backend процедур ТУ"),
        QStringLiteral("локально"),
        QStringLiteral("Процедуры оборудования ещё не подключены"));

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
}

TuController::~TuController()
{
    engine_.requestStop();
    if (runThread_) {
        runThread_->wait();
        runThread_ = nullptr;
    }
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
        scenario_ = tu::loadScenarioYaml(scenarioPath());
        scenarioLoaded_ = true;
        scenarioError_.clear();
    } catch (const std::exception& error) {
        scenario_ = {};
        scenarioLoaded_ = false;
        scenarioError_ = QString::fromUtf8(error.what());
    }
    refreshBackendReadiness();
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

    const auto errors = engine_.validate(scenario_);
    proceduresReady_ = errors.empty();

    page_->setScenarioInfo(
        kUiScenarioCode,
        true,
        false,
        {kBackendEquipmentCode},
        QStringLiteral("Маршрут %1 · версия %2")
            .arg(QString::fromStdString(scenario_.id),
                 QString::fromStdString(scenario_.version)));

    if (proceduresReady_) {
        page_->setEquipmentStatus(
            kBackendEquipmentCode, true,
            QStringLiteral("Все процедуры маршрута зарегистрированы"));
    } else {
        page_->setEquipmentStatus(
            kBackendEquipmentCode, false,
            QStringLiteral("Backend ещё не готов:\n%1").arg(joinedErrors(errors)));
    }
}

void TuController::checkBackendReadiness()
{
    page_->setEquipmentChecking(kBackendEquipmentCode,
                                QStringLiteral("Проверка регистрации процедур"));
    refreshBackendReadiness();
}

void TuController::startRun(const QString& scenarioCode,
                            const QString& objectSerial,
                            bool allowPartial)
{
    Q_UNUSED(allowPartial);

    if (runThread_ || !scenarioLoaded_ || !proceduresReady_) return;
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
