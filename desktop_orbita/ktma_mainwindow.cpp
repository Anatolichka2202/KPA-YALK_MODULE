#include "ktma_mainwindow.h"

#include "ktma/ubsi/production_ledger.h"
#include "ktma/ubsi/production_report.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>
#include <stdexcept>

namespace {

QStringList equipmentRoles(const orbita::stand::ScenarioDefinition& scenario)
{
    const QHash<QString, QString> roles = {
        {QStringLiteral("ulk.parameter_source"), QStringLiteral("RS485")},
        {QStringLiteral("stand.switch_matrix"), QStringLiteral("ISD")},
        {QStringLiteral("measure.reference_voltage"), QStringLiteral("V7")},
        {QStringLiteral("measure.dc_current"), QStringLiteral("V7")},
        {QStringLiteral("measure.reference_ac_voltage"), QStringLiteral("V7")},
        {QStringLiteral("measure.reference_frequency"), QStringLiteral("V7")},
        {QStringLiteral("power.dc_supply"), QStringLiteral("AKIP")},
        {QStringLiteral("signal.generator"), QStringLiteral("RIGOL")},
        {QStringLiteral("operator.manual_input"), QStringLiteral("R4831")}};

    QSet<QString> result;
    std::function<void(const orbita::stand::ScenarioNode&)> collect;
    collect = [&](const orbita::stand::ScenarioNode& node) {
        for (const auto& capability : node.requiredCapabilities) {
            const QString role = roles.value(QString::fromStdString(capability));
            if (!role.isEmpty()) result.insert(role);
        }
        for (const auto& child : node.children) collect(child);
    };
    for (const auto& node : scenario.steps) collect(node);
    QStringList list = result.values();
    list.sort();
    return list;
}

bool enabledFlag(const std::map<std::string, std::string>& config,
                 const std::string& key)
{
    const auto found = config.find(key);
    if (found == config.end()) return false;
    return found->second == "true" || found->second == "1"
        || found->second == "yes" || found->second == "on";
}

} // namespace

KtmaMainWindow::KtmaMainWindow(QWidget* parent)
    : MainWindow(parent)
{
    auto* page = integrationTestPage();
    if (!page) return;

    page->registerEquipmentRow(QStringLiteral("RIGOL"),
        QStringLiteral("Rigol DG-1022Z / ДГ10.2"), QStringLiteral("USB / VISA"),
        QStringLiteral("Требуется для проверки ЯВП; состояние определяется профилем стенда"));

    integrationEnsureStandRuntime();
    loadProductionScenarios();

    try {
        const QDir root(QCoreApplication::applicationDirPath());
        productionLedger_ = std::make_unique<ktma::ubsi::ProductionLedger>(
            root.filePath(QStringLiteral("registrar.db")).toStdString());
    } catch (const std::exception& error) {
        integrationLog(QStringLiteral("ProductionLedger не готов: %1")
            .arg(QString::fromUtf8(error.what())));
    }

    // Replace only the UBSI run orchestration. The base window keeps Orbita
    // monitoring/engineering behaviour for future BSI/RPU product packages.
    QObject::disconnect(page, SIGNAL(runRequested(QString,QString,bool)),
                        this, SLOT(onRunScenario(QString,QString,bool)));
    connect(page, &TestPage::runRequested,
            this, &KtmaMainWindow::runScenario);
    connect(page, &TestPage::equipmentCheckRequested,
            this, &KtmaMainWindow::checkRigolGenerator);

    if (auto* watcher = integrationScenarioWatcher()) {
        connect(watcher, &QFutureWatcherBase::finished,
                this, &KtmaMainWindow::finalizeProductionRun);
    }

    if (auto* home = integrationHomePage()) {
        connect(home, &HomePage::productionRequested, this, [this] {
            QTimer::singleShot(0, this, [this] {
                configureProductionSelector();
                checkRigolGenerator();
            });
        });
        connect(home, &HomePage::tuRequested, this, [this] {
            QTimer::singleShot(0, this, [this] {
                restoreTuSelector();
                checkRigolGenerator();
            });
        });
    }

    if (auto* scope = page->findChild<QComboBox*>(QStringLiteral("testScope"))) {
        connect(scope, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
            if (!integrationProductionWorkflowActive()) return;
            QTimer::singleShot(0, this, &KtmaMainWindow::applyProductionScenario);
        });
    }
}

KtmaMainWindow::~KtmaMainWindow() = default;

void KtmaMainWindow::loadProductionScenarios()
{
    auto* page = integrationTestPage();
    auto* engine = integrationScenarioEngine();
    if (!page || !engine || !integrationStandRuntimeReady()) {
        const QString detail = QStringLiteral(
            "Production scenarios недоступны: стендовый runtime не инициализирован");
        for (const auto& code : {QStringLiteral("PROD_FULL"), QStringLiteral("PROD_POWER"),
                                 QStringLiteral("PROD_YALK"), QStringLiteral("PROD_YTP"),
                                 QStringLiteral("PROD_YVP")})
            page->setScenarioInfo(code, false, false, {}, detail);
        return;
    }

    const QHash<QString, QString> ids = {
        {QStringLiteral("ktma.ubsi.production.full"), QStringLiteral("PROD_FULL")},
        {QStringLiteral("ktma.ubsi.production.power"), QStringLiteral("PROD_POWER")},
        {QStringLiteral("ktma.ubsi.production.yalk"), QStringLiteral("PROD_YALK")},
        {QStringLiteral("ktma.ubsi.production.ytp"), QStringLiteral("PROD_YTP")},
        {QStringLiteral("ktma.ubsi.production.yvp"), QStringLiteral("PROD_YVP")}};

    QSet<QString> loaded;
    const QDir scenarioDir(QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("scenarios")));
    for (const auto& file : scenarioDir.entryInfoList(
             {QStringLiteral("*.yaml")}, QDir::Files, QDir::Name)) {
        try {
            const auto scenario = orbita::stand::loadScenarioYaml(
                file.absoluteFilePath().toUtf8().toStdString());
            const QString code = ids.value(QString::fromStdString(scenario.id));
            if (code.isEmpty()) continue;

            QStringList errors;
            for (const auto& error : engine->validate(scenario))
                errors << QString::fromStdString(error);
            const bool available = errors.isEmpty();
            const QString detail = available
                ? QStringLiteral("Загружен Production scenario «%1», версия %2")
                    .arg(QString::fromStdString(scenario.title),
                         QString::fromStdString(scenario.version))
                : errors.join(QStringLiteral("; "));
            if (available) {
                integrationScenarios().insert(code, scenario);
                integrationScenarioPaths().insert(code, file.absoluteFilePath());
                loaded.insert(code);
            }
            page->setScenarioInfo(code, available, false,
                                  equipmentRoles(scenario), detail);
        } catch (const std::exception& error) {
            integrationLog(QStringLiteral("Production scenario %1: %2")
                .arg(file.fileName(), QString::fromUtf8(error.what())));
        }
    }

    for (const auto& code : {QStringLiteral("PROD_FULL"), QStringLiteral("PROD_POWER"),
                             QStringLiteral("PROD_YALK"), QStringLiteral("PROD_YTP"),
                             QStringLiteral("PROD_YVP")}) {
        if (!loaded.contains(code)) {
            page->setScenarioInfo(code, false, false, {},
                QStringLiteral("Production scenario %1 не загружен").arg(code));
        }
    }
}

QString KtmaMainWindow::productionCodeForScope(const QString& scope) const
{
    if (scope == QStringLiteral("УБСИ ПО ТУ")) return QStringLiteral("PROD_FULL");
    if (scope == QStringLiteral("ПИТАНИЕ")) return QStringLiteral("PROD_POWER");
    if (scope == QStringLiteral("ЯЛК-96")) return QStringLiteral("PROD_YALK");
    if (scope == QStringLiteral("ЯТП")) return QStringLiteral("PROD_YTP");
    if (scope == QStringLiteral("ЯВП-8")) return QStringLiteral("PROD_YVP");
    return {};
}

void KtmaMainWindow::configureProductionSelector()
{
    auto* page = integrationTestPage();
    if (!page || !integrationProductionWorkflowActive()) return;
    auto* scope = page->findChild<QComboBox*>(QStringLiteral("testScope"));
    auto* test = page->findChild<QComboBox*>(QStringLiteral("testType"));
    if (!scope || !test) return;

    const QString previous = scope->currentData().toString();
    const QSignalBlocker blocker(scope);
    scope->clear();
    scope->addItem(QStringLiteral("УБСИ · полная"), QStringLiteral("УБСИ ПО ТУ"));
    scope->addItem(QStringLiteral("Питание / потребление"), QStringLiteral("ПИТАНИЕ"));
    scope->addItem(QStringLiteral("ЯЛК-96"), QStringLiteral("ЯЛК-96"));
    scope->addItem(QStringLiteral("ЯТП"), QStringLiteral("ЯТП"));
    scope->addItem(QStringLiteral("ЯВП-8"), QStringLiteral("ЯВП-8"));
    const int previousIndex = scope->findData(previous);
    scope->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
    if (scope->parentWidget()) scope->parentWidget()->setVisible(true);
    if (test->parentWidget()) test->parentWidget()->setVisible(false);
    applyProductionScenario();
}

void KtmaMainWindow::applyProductionScenario()
{
    auto* page = integrationTestPage();
    if (!page || !integrationProductionWorkflowActive()) return;
    auto* scope = page->findChild<QComboBox*>(QStringLiteral("testScope"));
    auto* test = page->findChild<QComboBox*>(QStringLiteral("testType"));
    if (!scope || !test) return;

    const QString code = productionCodeForScope(scope->currentData().toString());
    if (code.isEmpty()) return;
    QString title;
    if (code == QStringLiteral("PROD_FULL")) title = QStringLiteral("Полная производственная проверка УБСИ");
    else if (code == QStringLiteral("PROD_POWER")) title = QStringLiteral("Питание / потребление");
    else if (code == QStringLiteral("PROD_YALK")) title = QStringLiteral("Полная ЯЛК-96");
    else if (code == QStringLiteral("PROD_YTP")) title = QStringLiteral("Полная ЯТП · 0 / 120 / 240 Ом");
    else if (code == QStringLiteral("PROD_YVP")) title = QStringLiteral("Полная ЯВП-8 · ЯЛК 88–96");

    {
        const QSignalBlocker blocker(test);
        test->clear();
        test->addItem(title, code);
        test->setCurrentIndex(0);
    }
    QMetaObject::invokeMethod(page, "updateSelectionSummary", Qt::DirectConnection);
}

void KtmaMainWindow::restoreTuSelector()
{
    auto* page = integrationTestPage();
    if (!page || !integrationTuWorkflowActive()) return;
    QMetaObject::invokeMethod(page, "rebuildScopes", Qt::DirectConnection);
    if (auto* scope = page->findChild<QComboBox*>(QStringLiteral("testScope")))
        if (scope->parentWidget()) scope->parentWidget()->setVisible(false);
    if (auto* test = page->findChild<QComboBox*>(QStringLiteral("testType")))
        if (test->parentWidget()) test->parentWidget()->setVisible(false);
}

void KtmaMainWindow::runScenario(
    const QString& requestedCode, const QString& objectSerial, bool allowPartial)
{
    integrationEnsureStandRuntime();
    auto* page = integrationTestPage();
    auto* engine = integrationScenarioEngine();
    auto* registry = integrationEquipmentRegistry();
    auto* watcher = integrationScenarioWatcher();
    if (!page || !engine || !registry || !watcher || !integrationStandRuntimeReady()
        || watcher->isRunning()) {
        return;
    }

    QString effectiveCode = requestedCode;
    std::string serial = objectSerial.trimmed().toUtf8().toStdString();
    std::optional<ktma::ubsi::ProductionRunContext> productionContext;

    try {
        if (integrationProductionWorkflowActive()) {
            auto* registrarPage = integrationRegistrarPage();
            auto* registrar = integrationRegistrar();
            if (!registrarPage || !registrar || !productionLedger_) {
                throw std::runtime_error("Production backend не инициализирован");
            }
            const auto selection = registrarPage->selectedProductionProduct();
            if (!selection) {
                throw std::runtime_error(
                    "Выберите зарегистрированное изделие и этап производства в Администрировании");
            }
            const auto package = ktma::ubsi::productionPackageFromCode(
                requestedCode.toStdString());
            const auto report = registrar->productReport(selection->productId.toStdString());
            productionContext = ktma::ubsi::buildProductionRunContext(
                report, selection->stage, package);
            effectiveCode = QString::fromStdString(productionContext->scenarioCode);
            serial = productionContext->productSerial;
        } else if (integrationTuWorkflowActive()) {
            if (objectSerial.trimmed().isEmpty()) {
                throw std::runtime_error("Введите заводской номер проверяемого УБСИ");
            }
            if (auto* registrar = integrationRegistrar()) {
                const auto product = registrar->findProductBySerial(serial);
                integrationLog(product
                    ? QStringLiteral("ТУ: изделие найдено в Registrar; production lifecycle не изменяется")
                    : QStringLiteral("ТУ: изделие в Registrar не найдено; прогон продолжен без создания записи"));
            }
        }

        const auto iterator = integrationScenarios().constFind(effectiveCode);
        if (iterator == integrationScenarios().cend())
            throw std::runtime_error(
                QStringLiteral("Сценарий %1 не загружен").arg(effectiveCode)
                    .toUtf8().toStdString());
        const auto scenario = iterator.value();

        if (productionContext) {
            pendingProductionContext_ = *productionContext;
            pendingProductionRunId_ = productionLedger_->begin(*productionContext);
        }

        engine->resetStop();
        page->setRunInProgress(true, QStringLiteral("Выполняется: %1")
            .arg(QString::fromStdString(scenario.title)));
        integrationLog(QStringLiteral("Запуск %1 · объект %2")
            .arg(QString::fromStdString(scenario.id), QString::fromUtf8(serial)));

        const std::string profileVersion = integrationStandProfile().version;
        watcher->setFuture(QtConcurrent::run(
            [this, engine, registry, scenario, profileVersion, serial, allowPartial]() {
                return engine->run(scenario, *registry, profileVersion, serial, allowPartial,
                    [this](const orbita::stand::RunEvent& event) {
                        QMetaObject::invokeMethod(this, [this, event] {
                            if (auto* testPage = integrationTestPage())
                                testPage->setRunEvent(event);
                        }, Qt::QueuedConnection);
                    });
            }));
    } catch (const std::exception& error) {
        if (!pendingProductionRunId_.empty() && productionLedger_) {
            try {
                productionLedger_->finish(pendingProductionRunId_,
                    ktma::ubsi::ProductionRunStatus::StandError);
            } catch (...) {
            }
        }
        clearPendingProduction();
        page->setRunInProgress(false);
        QMessageBox::warning(this,
            integrationProductionWorkflowActive()
                ? QStringLiteral("Производственный запуск")
                : QStringLiteral("Проверка по ТУ"),
            QString::fromUtf8(error.what()));
        integrationLog(QStringLiteral("Запуск отклонён: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

void KtmaMainWindow::finalizeProductionRun()
{
    if (pendingProductionRunId_.empty() || !pendingProductionContext_
        || !productionLedger_) return;
    auto* watcher = integrationScenarioWatcher();
    auto* page = integrationTestPage();
    if (!watcher || !page) return;

    const auto result = watcher->result();
    const auto status = ktma::ubsi::productionStatusFromScenarioVerdict(result.verdict);

    if (!result.runId.empty()) {
        try {
            productionLedger_->attachRun(pendingProductionRunId_, result.runId);
        } catch (const std::exception& error) {
            integrationLog(QStringLiteral("ProductionLedger: run_id не привязан: %1")
                .arg(QString::fromUtf8(error.what())));
        }
    }
    try {
        productionLedger_->finish(pendingProductionRunId_, status);
    } catch (const std::exception& error) {
        integrationLog(QStringLiteral("ProductionLedger: итог не сохранён: %1")
            .arg(QString::fromUtf8(error.what())));
    }

    QString productionReportPath;
    if (!result.runId.empty()) {
        try {
            const QDir root(QCoreApplication::applicationDirPath());
            const QString reportDirectory = root.filePath(
                QStringLiteral("runs/") + QString::fromStdString(result.runId));
            const auto report = ktma::ubsi::writeProductionReport(
                result, *pendingProductionContext_, reportDirectory.toStdString());
            productionReportPath = QString::fromStdString(report.html);

            // Compatibility cleanup: the legacy technical writer used by the
            // base shell must never expose a TU protocol for a Production run.
            QFile::remove(QDir(reportDirectory).filePath(
                QStringLiteral("Протокол_ТУ_%1.html")
                    .arg(QString::fromStdString(result.runId))));
        } catch (const std::exception& error) {
            integrationLog(QStringLiteral("Production report не сформирован: %1")
                .arg(QString::fromUtf8(error.what())));
        }
    }

    page->setRunResult(result, {}, productionReportPath);
    integrationLog(QStringLiteral("Production завершён: %1 · status=%2 · report=%3")
        .arg(QString::fromStdString(result.runId),
             QString::fromUtf8(ktma::ubsi::toString(status)),
             productionReportPath.isEmpty() ? QStringLiteral("не сформирован")
                                            : productionReportPath));
    clearPendingProduction();
}

void KtmaMainWindow::checkRigolGenerator()
{
    auto* page = integrationTestPage();
    auto* plugins = integrationEquipmentPlugins();
    auto* registry = integrationEquipmentRegistry();
    if (!page || !plugins || !registry || !integrationStandRuntimeReady()) return;

    const auto& profile = integrationStandProfile();
    const orbita::stand::DeviceProfile* definition = nullptr;
    for (const auto& device : profile.devices) {
        if (device.pluginId == "orbita.rigol_generator") {
            definition = &device;
            break;
        }
    }
    if (!definition) {
        page->setEquipmentStatus(QStringLiteral("RIGOL"), false,
            QStringLiteral("В профиле стенда нет генератора Rigol"));
        return;
    }
    if (!definition->enabled) {
        const auto reason = definition->configuration.find("disabled_reason");
        page->setEquipmentStatus(QStringLiteral("RIGOL"), false,
            reason == definition->configuration.end()
                ? QStringLiteral("Отключён профилем стенда")
                : QString::fromStdString(reason->second));
        return;
    }

    try {
        auto config = definition->configuration;
        config["record_root"] = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("runs")).toStdString();
        config["profile.active_outputs_confirmed"] =
            profile.activeOutputsConfirmed ? "true" : "false";
        for (const auto& [key, value] : profile.routes)
            config["route." + key] = value;

        if (definition->bindCapabilities.empty())
            throw std::runtime_error("Для Rigol не указана capability");
        auto device = plugins->createDevice(
            definition->pluginId, definition->id, config);
        const std::string capability = definition->bindCapabilities.front();
        const std::string response = device->invoke(capability, "probe", {});

        const auto confirmation = config.find("device.active_commands_confirmed");
        const bool explicitlyBlocked = confirmation != config.end()
            && !enabledFlag(config, "device.active_commands_confirmed");
        const bool deviceConfirmed = enabledFlag(config, "device.active_commands_confirmed");
        const bool activeAllowed = !explicitlyBlocked
            && (profile.activeOutputsConfirmed || deviceConfirmed);

        integrationEquipmentDevices().push_back(device);
        if (!activeAllowed) {
            page->setEquipmentStatus(QStringLiteral("RIGOL"), false,
                QStringLiteral("Связь есть, но активный выход заблокирован профилем стенда"));
            return;
        }
        for (const auto& bound : definition->bindCapabilities)
            registry->bind(bound, device);
        page->setEquipmentStatus(QStringLiteral("RIGOL"), true,
            QString::fromStdString(response).trimmed());
    } catch (const std::exception& error) {
        page->setEquipmentStatus(QStringLiteral("RIGOL"), false,
            QString::fromUtf8(error.what()));
        integrationLog(QStringLiteral("Rigol не готов: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

void KtmaMainWindow::clearPendingProduction() noexcept
{
    pendingProductionRunId_.clear();
    pendingProductionContext_.reset();
}
