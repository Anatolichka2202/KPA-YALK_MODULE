#include "ktma_mainwindow.h"

#include "ktma/ubsi/production_ledger.h"
#include "ktma/ubsi/production_report.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>
#include <algorithm>
#include <array>
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
    setWindowTitle(QStringLiteral("MilTech Station · КТМА / УБСИ · 2.0"));
    auto* page = integrationTestPage();
    if (!page) return;
    integrationUseUbsiEngineering();

    page->registerEquipmentRow(QStringLiteral("RIGOL"),
        QStringLiteral("Rigol DG-1022Z / ДГ10.2"), QStringLiteral("USB / VISA"),
        QStringLiteral("Требуется для проверки ЯВП; состояние определяется профилем стенда"));

    integrationEnsureStandRuntime();
    loadProductionScenarios();
    connect(integrationRegistrarPage(), &RegistrarPage::productionRequested,this,[this]{
        integrationOpenTests();configureProductionSelector();
    });

    try {
        const QDir root(QCoreApplication::applicationDirPath());
        productionLedger_ = std::make_unique<ktma::ubsi::ProductionLedger>(
            root.filePath(QStringLiteral("registrar.db")).toStdString());
        if (auto* registrarPage = integrationRegistrarPage())
            registrarPage->setProductionLedger(productionLedger_.get());
    } catch (const std::exception& error) {
        integrationLog(QStringLiteral("ProductionLedger не готов: %1")
            .arg(QString::fromUtf8(error.what())));
    }

    // Replace only the UBSI run orchestration. The base window keeps Orbita
    // monitoring/engineering behaviour for future BSI/RPU product packages.
    integrationDisableBaseScenarioRunner();
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
                integrationOpenTests();
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

bool KtmaMainWindow::registerProductionProduct(const QString& serial)
{
    auto* registrar = integrationRegistrar();
    if (!registrar) {
        QMessageBox::warning(this, QStringLiteral("Регистрация изделия"),
            QStringLiteral("Регистратор недоступен."));
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Новое изделие УБСИ · %1").arg(serial));
    dialog.setModal(true);
    auto* layout = new QVBoxLayout(&dialog);
    auto* caption = new QLabel(QStringLiteral(
        "Изделие не найдено. Укажите серийные номера установленного состава; "
        "после сохранения вы вернётесь к запуску проверки."), &dialog);
    caption->setWordWrap(true);
    layout->addWidget(caption);
    auto* form = new QFormLayout;
    QLineEdit yalk;
    QLineEdit ytp;
    QLineEdit yvp;
    QLineEdit power;
    for (auto* edit : {&yalk, &ytp, &yvp, &power}) {
        edit->setMinimumWidth(280);
        edit->setPlaceholderText(QStringLiteral("Серийный номер"));
    }
    form->addRow(QStringLiteral("ЯЛК-96"), &yalk);
    form->addRow(QStringLiteral("ЯТП"), &ytp);
    form->addRow(QStringLiteral("ЯВП"), &yvp);
    form->addRow(QStringLiteral("ЯП-П"), &power);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Save, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("ЗАРЕГИСТРИРОВАТЬ И ПРОДОЛЖИТЬ"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("ОТМЕНА"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (yalk.text().trimmed().isEmpty() || ytp.text().trimmed().isEmpty()
            || yvp.text().trimmed().isEmpty() || power.text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("Состав изделия"),
                QStringLiteral("Укажите SN всех четырёх ячеек."));
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return false;

    try {
        const auto productId = registrar->createProduct("UBSI", serial.toStdString());
        const std::array<std::pair<const char*, QString>, 4> components = {{
            {"YALK-96", yalk.text().trimmed()}, {"YTP", ytp.text().trimmed()},
            {"YVP", yvp.text().trimmed()}, {"YP-P", power.text().trimmed()}}};
        for (const auto& [type, componentSerial] : components) {
            const auto componentId = registrar->createComponent(type, componentSerial.toStdString());
            registrar->installComponent(productId, componentId);
        }
        integrationLog(QStringLiteral("Production: зарегистрировано изделие %1 и его состав")
            .arg(serial));
        return true;
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Регистрация изделия"),
            QString::fromUtf8(error.what()));
        return false;
    }
}

KtmaMainWindow::~KtmaMainWindow() = default;

void KtmaMainWindow::loadProductionScenarios()
{
    auto* page = integrationTestPage();
    auto* engine = integrationScenarioEngine();
    if (!page) return;
    if (!engine || !integrationStandRuntimeReady()) {
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

            if (code == QStringLiteral("PROD_YVP")) {
                page->setScenarioInfo(code, false, false, {}, QStringLiteral(
                    "ЯВП временно недоступна: внешняя команда ROKT для переключения адаптера ещё не подтверждена."));
                loaded.insert(code);
                continue;
            }

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
    if (auto* scope = page->findChild<QComboBox*>(QStringLiteral("testScope"))) {
        if (scope->parentWidget()) scope->parentWidget()->setVisible(false);
    }
    if (auto* test = page->findChild<QComboBox*>(QStringLiteral("testType"))) {
        if (test->parentWidget()) test->parentWidget()->setVisible(false);
    }
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
            auto* registrar = integrationRegistrar();
            if (!registrar || !productionLedger_) {
                throw std::runtime_error("Production backend не инициализирован");
            }
            if (serial.empty()) {
                throw std::runtime_error("Введите заводской номер УБСИ");
            }
            auto product = registrar->findProductBySerial(serial);
            if (!product) {
                if (!registerProductionProduct(QString::fromUtf8(serial))) return;
                product = registrar->findProductBySerial(serial);
            }
            if (!product) throw std::runtime_error("Не удалось зарегистрировать изделие УБСИ");
            const auto package = ktma::ubsi::productionPackageFromCode(
                requestedCode.toStdString());
            const auto report = registrar->productReport(product->id);
            productionContext = ktma::ubsi::buildProductionRunContext(
                report, ktma::registrar::Stage::Primary, package);
            effectiveCode = QString::fromStdString(productionContext->scenarioCode);
            serial = productionContext->productSerial;
        } else if (integrationTuWorkflowActive()) {
            if (objectSerial.trimmed().isEmpty()) {
                throw std::runtime_error("Введите заводской номер проверяемого УБСИ");
            }
            if (auto* registrar = integrationRegistrar()) {
                const auto product = registrar->findProductBySerial(serial);
                pendingTuProductId_ = product ? product->id : std::string();
                integrationLog(product
                    ? QStringLiteral("ТУ: изделие найдено в Registrar; production lifecycle не изменяется")
                    : QStringLiteral("ТУ: изделие в Registrar не найдено; прогон продолжен без создания записи"));
            }
        }

        const auto iterator = integrationScenarios().constFind(effectiveCode);
        if (iterator == integrationScenarios().cend()) {
            throw std::runtime_error(
                QStringLiteral("Сценарий %1 не загружен").arg(effectiveCode)
                    .toUtf8().toStdString());
        }
        auto scenario = iterator.value();
        const bool production = integrationProductionWorkflowActive();
        const bool omitYvp = !page->includeYvp() && effectiveCode != QStringLiteral("PROD_YVP");
        const bool omitOverload = production && !page->includeProductionOverload();
        const bool omitSurvival = production && !page->includeProductionSurvival();
        bool yvpExcluded = false;
        if (omitYvp) {
            auto& steps = scenario.steps;
            const auto originalCount = steps.size();
            steps.erase(std::remove_if(steps.begin(), steps.end(), [](const auto& node) {
                return node.id.rfind("yvp_", 0) == 0;
            }), steps.end());
            yvpExcluded = steps.size() != originalCount;
            if (yvpExcluded) { scenario.title += " · без ЯВП"; scenario.version += "+without-yvp"; }
        }
        bool overloadExcluded = false;
        if (omitOverload) {
            auto& steps = scenario.steps;
            const auto originalCount = steps.size();
            steps.erase(std::remove_if(steps.begin(), steps.end(), [](const auto& node) {
                return node.id == "yalk_overload";
            }), steps.end());
            overloadExcluded = steps.size() != originalCount;
            if (overloadExcluded) { scenario.title += " · без перегрузки ЯЛК"; scenario.version += "+without-overload"; }
        }
        bool survivalExcluded = false;
        if (omitSurvival) {
            for (auto& node : scenario.steps) {
                if (node.id != "supply_range") continue;
                survivalExcluded = !node.arguments["survival_points_v"].empty();
                node.arguments.erase("survival_points_v");
                node.arguments.erase("survival_seconds");
            }
            if (survivalExcluded) { scenario.title += " · без выдержек 19/37 В"; scenario.version += "+without-survival"; }
        }

        if (productionContext) {
            pendingProductionContext_ = *productionContext;
            pendingProductionRunId_ = productionLedger_->begin(*productionContext);
        }

        engine->resetStop();
        page->setRunInProgress(true, QStringLiteral("Выполняется: %1")
            .arg(QString::fromStdString(scenario.title)));
        integrationLog(QStringLiteral("Запуск %1 · объект %2")
            .arg(QString::fromStdString(scenario.id), QString::fromUtf8(serial.c_str())));

        const std::string profileVersion = integrationStandProfile().version;
        watcher->setFuture(QtConcurrent::run(
            [this, engine, registry, scenario, profileVersion, serial, allowPartial, yvpExcluded, overloadExcluded, survivalExcluded]() {
                auto result = engine->run(scenario, *registry, profileVersion, serial, allowPartial,
                    [this](const orbita::stand::RunEvent& event) {
                        QMetaObject::invokeMethod(this, [this, event] {
                            if (auto* testPage = integrationTestPage())
                                testPage->setRunEvent(event);
                        }, Qt::QueuedConnection);
                    });
                if (yvpExcluded || overloadExcluded || survivalExcluded) result.events.insert(result.events.begin(), {
                    result.startedAt, "scope", "SCOPE", "Производственный результат относится к выбранному оператором объёму",
                    orbita::stand::RunVerdict::NotRun, {{"yvp_included", yvpExcluded ? "false" : "true"},
                                                        {"overload_included", overloadExcluded ? "false" : "true"},
                                                        {"survival_included", survivalExcluded ? "false" : "true"}}});
                return result;
            }));
    } catch (const std::exception& error) {
        if (!pendingProductionRunId_.empty() && productionLedger_) {
            try {
                productionLedger_->finish(pendingProductionRunId_,
                    ktma::ubsi::ProductionRunStatus::StandError);
            } catch (...) {
            }
        }
        pendingTuProductId_.clear();
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
    if (!pendingTuProductId_.empty()) {
        try {
            if (integrationResultSaved()) {
                const auto result=integrationScenarioWatcher()->result();
                integrationRegistrar()->attachTuRun(pendingTuProductId_,result.runId,orbita::stand::toString(result.verdict));
            }
        } catch(const std::exception& error) { integrationLog(QString::fromUtf8(error.what())); }
        pendingTuProductId_.clear();
    }
    if (pendingProductionRunId_.empty() || !pendingProductionContext_
        || !productionLedger_) return;
    auto* watcher = integrationScenarioWatcher();
    auto* page = integrationTestPage();
    if (!watcher || !page) return;

    const auto result = watcher->result();
    const auto status = integrationResultSaved() ? ktma::ubsi::productionStatusFromScenarioVerdict(result.verdict) : ktma::ubsi::ProductionRunStatus::StandError;

    if (integrationResultSaved() && !result.runId.empty()) {
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
