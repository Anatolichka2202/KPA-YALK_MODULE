#include "ktma_mainwindow.h"

#include "ktma/ubsi/equipment_readiness.h"
#include "ktma/ubsi/production_ledger.h"
#include "ktma/ubsi/production_report.h"
#include "ktma/ubsi/procedures.h"
#include "orbita_stand/catalog.h"
#include "orbita_stand/config.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {

QString equipmentCode(const std::string& capability)
{
    static const QHash<QString, QString> roles = {
        {QStringLiteral("ulk.parameter_source"), QStringLiteral("RS485")},
        {QStringLiteral("stand.switch_matrix"), QStringLiteral("ISD")},
        {QStringLiteral("measure.reference_voltage"), QStringLiteral("V7")},
        {QStringLiteral("measure.dc_current"), QStringLiteral("V7")},
        {QStringLiteral("measure.reference_ac_voltage"), QStringLiteral("V7")},
        {QStringLiteral("measure.reference_frequency"), QStringLiteral("V7")},
        {QStringLiteral("power.dc_supply"), QStringLiteral("AKIP")},
        {QStringLiteral("signal.generator"), QStringLiteral("RIGOL")},
        {QStringLiteral("operator.manual_input"), QStringLiteral("R4831")}};
    return roles.value(QString::fromStdString(capability));
}

const std::vector<std::string>& componentCapabilities(
    const orbita::stand::ComponentProfile& component)
{
    return component.capabilities.empty() ? component.bindings : component.capabilities;
}

QStringList equipmentCodes(const orbita::stand::ComponentProfile& component)
{
    QSet<QString> result;
    for (const auto& capability : componentCapabilities(component)) {
        const QString code = equipmentCode(capability);
        if (!code.isEmpty()) result.insert(code);
    }
    QStringList list = result.values();
    list.sort();
    return list;
}

std::set<std::string> scenarioCapabilities(
    const orbita::stand::ScenarioDefinition& scenario)
{
    std::set<std::string> result;
    std::function<void(const orbita::stand::ScenarioNode&)> collect;
    collect = [&](const orbita::stand::ScenarioNode& node) {
        result.insert(node.requiredCapabilities.begin(), node.requiredCapabilities.end());
        for (const auto& requirement : node.requiredResources)
            result.insert(requirement.capability);
        for (const auto& child : node.children) collect(child);
    };
    for (const auto& node : scenario.steps) collect(node);
    return result;
}

QStringList equipmentRoles(const orbita::stand::ScenarioDefinition& scenario)
{
    QSet<QString> result;
    for (const auto& capability : scenarioCapabilities(scenario)) {
        const QString role = equipmentCode(capability);
        if (!role.isEmpty()) result.insert(role);
    }
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
        QStringLiteral("Требуется только сценариям, где есть signal.generator"));

    integrationEnsureStandRuntime();
    loadTuScenarios();
    loadProductionScenarios();

    const auto loadRegisteredProducts = [this, page] {
        QStringList serials;
        if (auto* registrar = integrationRegistrar()) {
            try {
                for (const auto& product : registrar->listProducts()) {
                    if (product.productType == "UBSI")
                        serials << QString::fromStdString(product.serialNumber);
                }
            } catch (const std::exception& error) {
                integrationLog(QStringLiteral("Registrar: не удалось загрузить список УБСИ: %1")
                    .arg(QString::fromUtf8(error.what())));
            }
        }
        page->setAvailableProductionProducts(serials);
    };

    connect(integrationRegistrarPage(), &RegistrarPage::productionRequested, this,
            [this, loadRegisteredProducts] {
        integrationOpenTests();
        configureProductionSelector();
        loadRegisteredProducts();
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

    integrationDisableBaseScenarioRunner();
    connect(page, &TestPage::runRequested,
            this, &KtmaMainWindow::runScenario);

    // Physical readiness is delivery-owned: UBSI probes only devices required
    // by the selected scenario and preserves its confirmed power-up order.
    connect(page, &TestPage::equipmentCheckRequested,
            this, &KtmaMainWindow::checkSelectedEquipment);

    if (auto* watcher = integrationScenarioWatcher()) {
        connect(watcher, &QFutureWatcherBase::finished,
                this, &KtmaMainWindow::finalizeProductionRun);
    }

    if (auto* home = integrationHomePage()) {
        connect(home, &HomePage::productionRequested, this,
                [this, loadRegisteredProducts] {
            QTimer::singleShot(0, this, [this, loadRegisteredProducts] {
                configureProductionSelector();
                loadRegisteredProducts();
                integrationOpenTests();
            });
        });
        connect(home, &HomePage::tuRequested, this, [this] {
            QTimer::singleShot(0, this, [this] {
                restoreTuSelector();
                // Selection does not touch equipment. The check is performed
                // only from Preparation for the chosen TU scenario.
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

void KtmaMainWindow::loadTuScenarios()
{
    auto* page = integrationTestPage();
    auto* engine = integrationScenarioEngine();
    if (!page) return;

    const QList<QString> publishedCodes = {
        QStringLiteral("UBSI_NORMAL_5_6"),
        QStringLiteral("YALK_FULL_5_6"),
        QStringLiteral("YALK_CONTACT_THRESHOLDS"),
        QStringLiteral("YTP_FULL_5_6"),
        QStringLiteral("YTP_120_CHECK"),
        QStringLiteral("ULK_COMBINED_CHECK"),
        QStringLiteral("BSI_DIAGNOSTIC")};

    if (!engine || !integrationStandRuntimeReady()) {
        const QString detail = QStringLiteral(
            "Сценарии ТУ недоступны: общий runtime станции не инициализирован");
        for (const auto& code : publishedCodes)
            page->setScenarioInfo(code, false, code == QStringLiteral("YTP_120_CHECK")
                                      || code == QStringLiteral("BSI_DIAGNOSTIC"), {}, detail);
        return;
    }

    // Product-owned procedures are registered by the KTMA delivery, not by the
    // reusable Station/Orbita shell.
    orbita::stand::registerUbsiProcedures(*engine);

    const QHash<QString, QString> ids = {
        {QStringLiteral("ubsi.468157.002.tu5_6.normal"), QStringLiteral("UBSI_NORMAL_5_6")},
        {QStringLiteral("ubsi.468157.002.yalk.tu5_6"), QStringLiteral("YALK_FULL_5_6")},
        {QStringLiteral("ubsi.468157.002.yalk.contact_thresholds"), QStringLiteral("YALK_CONTACT_THRESHOLDS")},
        {QStringLiteral("ubsi.468157.002.ytp.tu5_6"), QStringLiteral("YTP_FULL_5_6")},
        {QStringLiteral("ubsi.468157.002.ytp.120ohm.check"), QStringLiteral("YTP_120_CHECK")},
        {QStringLiteral("ubsi.468157.002.ulk.combined.check"), QStringLiteral("ULK_COMBINED_CHECK")},
        {QStringLiteral("bsi.468157.003.telemetry.diagnostic"), QStringLiteral("BSI_DIAGNOSTIC")}};
    const QSet<QString> diagnosticCodes = {
        QStringLiteral("YTP_120_CHECK"), QStringLiteral("BSI_DIAGNOSTIC")};
    const QHash<QString, QString> equipmentForCapability = {
        {QStringLiteral("ulk.parameter_source"), QStringLiteral("RS485")},
        {QStringLiteral("stand.switch_matrix"), QStringLiteral("ISD")},
        {QStringLiteral("orbita.parameter_source"), QStringLiteral("E20")},
        {QStringLiteral("measure.reference_voltage"), QStringLiteral("V7")},
        {QStringLiteral("measure.dc_current"), QStringLiteral("V7")},
        {QStringLiteral("measure.reference_ac_voltage"), QStringLiteral("V7")},
        {QStringLiteral("measure.reference_frequency"), QStringLiteral("V7")},
        {QStringLiteral("power.dc_supply"), QStringLiteral("AKIP")},
        {QStringLiteral("signal.generator"), QStringLiteral("RIGOL")},
        {QStringLiteral("operator.manual_input"), QStringLiteral("R4831")},
        {QStringLiteral("measure.waveform"), QStringLiteral("SCOPE")}};

    const QDir root(QCoreApplication::applicationDirPath());
    const auto catalog = orbita::stand::importCatalogYaml(
        root.filePath(QStringLiteral("catalog/catalog.yaml")).toStdString(),
        root.filePath(QStringLiteral("parameters.db")).toStdString());
    const QDir scenarioDirectory(root.filePath(QStringLiteral("scenarios")));
    QSet<QString> loaded;

    for (const auto& file : scenarioDirectory.entryInfoList(
             {QStringLiteral("*.yaml")}, QDir::Files, QDir::Name)) {
        try {
            const auto scenario = orbita::stand::loadScenarioYaml(
                file.absoluteFilePath().toUtf8().toStdString());
            const QString code = ids.value(QString::fromStdString(scenario.id));
            if (code.isEmpty()) continue;

            QStringList errors;
            if (catalog.version != scenario.catalogVersion) {
                errors << QStringLiteral("Версия каталога %1 не совпадает со сценарием %2")
                    .arg(QString::fromStdString(catalog.version),
                         QString::fromStdString(scenario.catalogVersion));
            }
            for (const auto& error : engine->validate(scenario))
                errors << QString::fromStdString(error);

            QSet<QString> requiredRoles;
            if (!diagnosticCodes.contains(code)) requiredRoles.insert(QStringLiteral("SCHEME"));
            std::function<void(const orbita::stand::ScenarioNode&)> collectRequired;
            collectRequired = [&](const orbita::stand::ScenarioNode& node) {
                for (const auto& capability : node.requiredCapabilities) {
                    const QString role = equipmentForCapability.value(
                        QString::fromStdString(capability));
                    if (!role.isEmpty()) requiredRoles.insert(role);
                }
                for (const auto& requirement : node.requiredResources) {
                    const QString role = equipmentForCapability.value(
                        QString::fromStdString(requirement.capability));
                    if (!role.isEmpty()) requiredRoles.insert(role);
                }
                for (const auto& child : node.children) collectRequired(child);
            };
            for (const auto& step : scenario.steps) collectRequired(step);
            QStringList required = requiredRoles.values();
            required.sort();

            const bool available = errors.isEmpty();
            const QString detail = available
                ? QStringLiteral("Загружен сценарий «%1», версия %2; профиль %3")
                    .arg(QString::fromStdString(scenario.title),
                         QString::fromStdString(scenario.version),
                         QString::fromStdString(integrationStandProfile().version))
                : errors.join(QStringLiteral("; "));
            if (available) {
                integrationScenarios().insert(code, scenario);
                integrationScenarioPaths().insert(code, file.absoluteFilePath());
                loaded.insert(code);
            }
            page->setScenarioInfo(code, available, diagnosticCodes.contains(code), required, detail);
        } catch (const std::exception& error) {
            integrationLog(QStringLiteral("TU scenario %1: %2")
                .arg(file.fileName(), QString::fromUtf8(error.what())));
        }
    }

    for (const auto& code : publishedCodes) {
        if (!loaded.contains(code)) {
            page->setScenarioInfo(code, false, diagnosticCodes.contains(code), {},
                QStringLiteral("Сценарий %1 не загружен").arg(code));
        }
    }
}

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
                                 QStringLiteral("PROD_YVP")}) {
            page->setScenarioInfo(code, false, false, {}, detail);
        }
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
    if (code == QStringLiteral("PROD_FULL"))
        title = QStringLiteral("Полная производственная проверка УБСИ");
    else if (code == QStringLiteral("PROD_YALK"))
        title = QStringLiteral("Полная ЯЛК-96");
    else if (code == QStringLiteral("PROD_YTP"))
        title = QStringLiteral("Полная ЯТП · 0 / 120 / 240 Ом");
    else if (code == QStringLiteral("PROD_YVP"))
        title = QStringLiteral("Полная ЯВП-8 · ROKT");

    const QSignalBlocker blocker(test);
    test->clear();
    test->addItem(title, code);
    test->setCurrentIndex(0);
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

void KtmaMainWindow::checkSelectedEquipment()
{
    auto* page = integrationTestPage();
    auto* registry = integrationEquipmentRegistry();
    if (!page || !registry) return;
    integrationEnsureStandRuntime();
    if (!integrationStandRuntimeReady()) return;

    const QString scenarioCode = page->currentScenarioCode();
    const auto scenario = integrationScenarios().constFind(scenarioCode);
    if (scenario == integrationScenarios().cend()) {
        integrationLog(QStringLiteral("Проверка оборудования: сценарий %1 не загружен")
            .arg(scenarioCode));
        return;
    }

    auto& session = integrationStationSession();
    auto& profile = integrationStandProfile();

    // Only physical routes are reset. Built-ins already installed by the shell
    // (notably orbita.parameter_source) survive this readiness pass.
    session.clearPhysicalEquipment();

    const std::string catalogDatabase = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("parameters.db")).toUtf8().toStdString();
    registry->bind("catalog.parameter_resolver",
        [catalogDatabase](const std::string& operation,
                          const std::map<std::string, std::string>& arguments) {
            if (operation != "resolve")
                throw std::invalid_argument("Неизвестная операция каталога: " + operation);
            const auto required = [&arguments](const char* key) -> const std::string& {
                const auto value = arguments.find(key);
                if (value == arguments.end() || value->second.empty())
                    throw std::invalid_argument(std::string("Каталогу требуется ") + key);
                return value->second;
            };
            const auto binding = orbita::stand::resolveCatalogParameterBinding(
                catalogDatabase, required("block_type"), required("parameter_group"),
                static_cast<unsigned>(std::stoul(required("channel_index"))));
            std::ostringstream response;
            response << "source=" << binding.source << '\n'
                     << "locator_type=" << binding.locatorType << '\n'
                     << "locator=" << binding.locator << '\n'
                     << "stream_id=" << binding.streamId << '\n'
                     << "word_index=" << binding.wordIndex << '\n'
                     << "mask=" << binding.mask << '\n'
                     << "shift=" << binding.shift << '\n'
                     << "mode=" << binding.mode << '\n'
                     << "conversion_id=" << binding.conversionId << '\n'
                     << "stimulus_route=" << binding.stimulusRoute << '\n'
                     << "stimulus_offset=" << binding.stimulusOffset << '\n'
                     << "confirmed=" << (binding.confirmed ? "true" : "false") << '\n';
            return response.str();
        });
    registry->bind("operator.manual_input",
        [this](const std::string& operation,
               const std::map<std::string, std::string>& arguments) {
            if (operation != "confirm_value" && operation != "confirm_text")
                throw std::invalid_argument("Неизвестная ручная операция: " + operation);
            if (operation == "confirm_text") {
                bool accepted = false;
                QString value;
                const QString title = arguments.count("title")
                    ? QString::fromStdString(arguments.at("title"))
                    : QStringLiteral("Подтверждающий документ");
                const QString prompt = arguments.count("prompt")
                    ? QString::fromStdString(arguments.at("prompt"))
                    : QStringLiteral("Введите номер и дату документа:");
                QMetaObject::invokeMethod(this, [&] {
                    value = QInputDialog::getText(this, title,
                        prompt + QStringLiteral(
                            "\nЕсли документа нет, оставьте поле пустым — пункт будет отмечен как НЕ ПРОВЕРЕНО."),
                        QLineEdit::Normal, {}, &accepted).trimmed();
                }, Qt::BlockingQueuedConnection);
                const QString operatorName = qEnvironmentVariable("USERNAME",
                    qEnvironmentVariable("USER", QStringLiteral("неизвестен")));
                std::ostringstream response;
                response << "status="
                         << (accepted && !value.isEmpty() ? "confirmed" : "not_confirmed")
                         << "\nvalue=" << value.toStdString()
                         << "\noperator=" << operatorName.toStdString()
                         << "\ntimestamp="
                         << QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString()
                         << "\n";
                return response.str();
            }

            bool accepted = false;
            double actual = 0.0;
            QString title = QStringLiteral("Ручная операция");
            QString prompt = QStringLiteral("Введите фактическое значение");
            if (const auto found = arguments.find("title"); found != arguments.end())
                title = QString::fromStdString(found->second);
            if (const auto target = arguments.find("target_value"); target != arguments.end()) {
                const QString unit = arguments.count("unit")
                    ? QString::fromStdString(arguments.at("unit")) : QString();
                prompt = QStringLiteral(
                    "Требуется: %1 %2\nВведите фактически установленное значение:")
                    .arg(QString::fromStdString(target->second), unit);
                actual = QString::fromStdString(target->second).toDouble();
            }
            QMetaObject::invokeMethod(this, [&] {
                actual = QInputDialog::getDouble(this, title, prompt, actual,
                    -1000000.0, 1000000.0, 6, &accepted);
            }, Qt::BlockingQueuedConnection);
            if (!accepted) throw std::runtime_error("Ручная операция отменена оператором");
            const QString operatorName = qEnvironmentVariable("USERNAME",
                qEnvironmentVariable("USER", QStringLiteral("неизвестен")));
            std::ostringstream response;
            response << std::setprecision(15) << "status=confirmed\nvalue=" << actual
                     << "\noperator=" << operatorName.toStdString()
                     << "\ntimestamp="
                     << QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString()
                     << "\n";
            return response.str();
        });

    const auto plan = ktma::ubsi::buildEquipmentReadinessPlan(
        profile, scenarioCapabilities(scenario.value()));
    for (const auto& item : plan.items) {
        const auto* component = orbita::stand::findComponentById(profile, item.componentId);
        if (!component) continue;
        for (const auto& code : equipmentCodes(*component))
            page->setEquipmentChecking(code, QStringLiteral("Ожидание проверки…"));
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        ktma::ubsi::executeEquipmentReadinessPlan(profile, plan, {
            [this, page, &session, &profile](
                const orbita::stand::ComponentProfile& component, bool armSupply) {
                const QStringList codes = equipmentCodes(component);
                const auto setStatus = [page, &codes](bool ready, const QString& detail) {
                    for (const auto& code : codes)
                        page->setEquipmentStatus(code, ready, detail);
                };
                if (!component.enabled) {
                    const auto reason = component.configuration.find("disabled_reason");
                    const QString detail = reason == component.configuration.end()
                        ? QStringLiteral("Отключено в профиле стенда")
                        : QString::fromStdString(reason->second);
                    setStatus(false, detail);
                    integrationLog(QStringLiteral("%1: %2")
                        .arg(QString::fromStdString(component.id), detail));
                    return;
                }

                for (const auto& code : codes) {
                    page->setEquipmentChecking(code,
                        armSupply && code == QStringLiteral("AKIP")
                            ? QStringLiteral("Подключение АКИП и включение питания УБСИ 27 В…")
                            : QStringLiteral("Загрузка DLL и проверка связи…"));
                }

                std::shared_ptr<orbita::stand::EquipmentDevice> device;
                try {
                    device = session.createEquipmentComponent(component.id);
                    const auto& capabilities = componentCapabilities(component);
                    if (capabilities.empty())
                        throw std::runtime_error("В профиле не указана возможность устройства");
                    const std::string probeCapability = capabilities.front();
                    const std::string response = device->invoke(probeCapability, "probe", {});
                    const bool passiveReady = response.find("alive=0") == std::string::npos
                        && response.find("alive=false") == std::string::npos;

                    const bool activeComponent = armSupply
                        || std::find(capabilities.begin(), capabilities.end(), "stand.switch_matrix")
                            != capabilities.end()
                        || std::find(capabilities.begin(), capabilities.end(), "signal.generator")
                            != capabilities.end();
                    const auto confirmation = component.configuration.find(
                        "device.active_commands_confirmed");
                    const bool explicitlyBlocked = confirmation != component.configuration.end()
                        && !enabledFlag(component.configuration, "device.active_commands_confirmed");
                    const bool deviceConfirmed = enabledFlag(
                        component.configuration, "device.active_commands_confirmed");
                    const bool activeAllowed = !activeComponent || (!explicitlyBlocked
                        && (profile.activeOutputsConfirmed || deviceConfirmed));
                    if (!activeAllowed) {
                        device->safeStop();
                        const QString detail = QStringLiteral(
                            "Связь есть, но активные воздействия заблокированы профилем стенда");
                        setStatus(false, detail);
                        integrationLog(QStringLiteral("%1: %2")
                            .arg(QString::fromStdString(component.id), detail));
                        return;
                    }

                    std::string finalResponse = response;
                    if (armSupply) {
                        device->invoke("power.dc_supply", "set_current_limit",
                            {{"amperes", "0.6"}});
                        device->invoke("power.dc_supply", "set_voltage",
                            {{"volts", "27.0"}});
                        device->invoke("power.dc_supply", "output",
                            {{"enabled", "true"}});
                        finalResponse = device->invoke(
                            "power.dc_supply", "read_state", {});
                        if (finalResponse.find("output_enabled=true") == std::string::npos)
                            throw std::runtime_error(
                                "АКИП не подтвердил включение питания УБСИ");
                    }

                    session.bindEquipmentComponent(component.id, device, true);
                    QString detail = QString::fromStdString(finalResponse).trimmed();
                    setStatus(passiveReady, detail);
                    integrationLog(QStringLiteral("%1: %2")
                        .arg(QString::fromStdString(component.id), detail));
                    if (!passiveReady && armSupply)
                        throw std::runtime_error("Источник питания не подтвердил готовность");
                } catch (const std::exception& error) {
                    if (device) device->safeStop();
                    const QString detail = QString::fromUtf8(error.what());
                    setStatus(false, detail);
                    integrationLog(QStringLiteral("%1 не готов: %2")
                        .arg(QString::fromStdString(component.id), detail));
                    if (armSupply) throw;
                }
            },
            [this, page](unsigned milliseconds) {
                page->setEquipmentChecking(QStringLiteral("RS485"),
                    QStringLiteral("Питание включено; ожидание запуска адаптера %1 с…")
                        .arg(milliseconds / 1000.0, 0, 'f', 1));
                integrationLog(QStringLiteral(
                    "Питание УБСИ включено; выдержка %1 мс перед проверкой адаптера")
                    .arg(milliseconds));
                QEventLoop delay;
                QTimer::singleShot(static_cast<int>(milliseconds), &delay, &QEventLoop::quit);
                delay.exec(QEventLoop::ExcludeUserInputEvents);
            },
        });
    } catch (const std::exception& error) {
        session.clearPhysicalEquipment();
        integrationLog(QStringLiteral("Подготовка оборудования остановлена: %1")
            .arg(QString::fromUtf8(error.what())));
    }
    QApplication::restoreOverrideCursor();
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
            if (!registrar || !productionLedger_)
                throw std::runtime_error("Production backend не инициализирован");
            if (serial.empty())
                throw std::runtime_error("Выберите зарегистрированное УБСИ");

            const auto product = registrar->findProductBySerial(serial);
            if (!product) {
                throw std::runtime_error(
                    "Изделие отсутствует в registrar.db. Регистрация выполняется только в разделе Администрирование");
            }

            const auto package = ktma::ubsi::productionPackageFromCode(
                requestedCode.toStdString());
            const auto report = registrar->productReport(product->id);
            productionContext = ktma::ubsi::buildProductionRunContext(
                report, ktma::registrar::Stage::Primary, package);
            effectiveCode = QString::fromStdString(productionContext->scenarioCode);
            serial = productionContext->productSerial;
        } else if (integrationTuWorkflowActive()) {
            if (objectSerial.trimmed().isEmpty())
                throw std::runtime_error("Введите заводской номер проверяемого УБСИ");
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
            if (yvpExcluded) {
                scenario.title += " · без ЯВП";
                scenario.version += "+without-yvp";
            }
        }

        bool overloadExcluded = false;
        if (omitOverload) {
            auto& steps = scenario.steps;
            const auto originalCount = steps.size();
            steps.erase(std::remove_if(steps.begin(), steps.end(), [](const auto& node) {
                return node.id == "yalk_overload";
            }), steps.end());
            overloadExcluded = steps.size() != originalCount;
            if (overloadExcluded) {
                scenario.title += " · без перегрузки ЯЛК";
                scenario.version += "+without-overload";
            }
        }

        bool survivalExcluded = false;
        if (omitSurvival) {
            for (auto& node : scenario.steps) {
                if (node.id != "supply_range") continue;
                survivalExcluded = !node.arguments["survival_points_v"].empty();
                node.arguments.erase("survival_points_v");
                node.arguments.erase("survival_seconds");
            }
            if (survivalExcluded) {
                scenario.title += " · без выдержек 19/37 В";
                scenario.version += "+without-survival";
            }
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
            [this, engine, registry, scenario, profileVersion, serial, allowPartial,
             yvpExcluded, overloadExcluded, survivalExcluded]() {
                auto result = engine->run(
                    scenario, *registry, profileVersion, serial, allowPartial,
                    [this](const orbita::stand::RunEvent& event) {
                        QMetaObject::invokeMethod(this, [this, event] {
                            if (auto* testPage = integrationTestPage())
                                testPage->setRunEvent(event);
                        }, Qt::QueuedConnection);
                    });
                if (yvpExcluded || overloadExcluded || survivalExcluded) {
                    result.events.insert(result.events.begin(), {
                        result.startedAt, "scope", "SCOPE",
                        "Производственный результат относится к выбранному оператором объёму",
                        orbita::stand::RunVerdict::NotRun,
                        {{"yvp_included", yvpExcluded ? "false" : "true"},
                         {"overload_included", overloadExcluded ? "false" : "true"},
                         {"survival_included", survivalExcluded ? "false" : "true"}}});
                }
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
                const auto result = integrationScenarioWatcher()->result();
                integrationRegistrar()->attachTuRun(
                    pendingTuProductId_, result.runId, orbita::stand::toString(result.verdict));
            }
        } catch (const std::exception& error) {
            integrationLog(QString::fromUtf8(error.what()));
        }
        pendingTuProductId_.clear();
    }

    if (pendingProductionRunId_.empty() || !pendingProductionContext_
        || !productionLedger_) return;
    auto* watcher = integrationScenarioWatcher();
    auto* page = integrationTestPage();
    if (!watcher || !page) return;

    const auto result = watcher->result();
    const auto status = integrationResultSaved()
        ? ktma::ubsi::productionStatusFromScenarioVerdict(result.verdict)
        : ktma::ubsi::ProductionRunStatus::StandError;

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

void KtmaMainWindow::clearPendingProduction() noexcept
{
    pendingProductionRunId_.clear();
    pendingProductionContext_.reset();
}
