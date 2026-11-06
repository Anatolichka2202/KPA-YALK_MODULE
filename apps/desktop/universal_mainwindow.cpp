#include "universal_mainwindow.h"

#include "generic_check_dialog.h"
#include "home_page.h"
#include "scenario_visual_editor.h"
#include "station_admin_dialog.h"
#include "orbita_stand/yaml_lite.h"

#include <QCoreApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUuid>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QMessageBox>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace {

QString verdictText(orbita::stand::RunVerdict verdict)
{
    using V = orbita::stand::RunVerdict;
    switch (verdict) {
    case V::Ok: return QStringLiteral("НОРМА");
    case V::Fail: return QStringLiteral("НЕ НОРМА");
    case V::Incomplete: return QStringLiteral("НЕПОЛНО");
    case V::Error: return QStringLiteral("ОШИБКА");
    case V::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case V::NotRun: return QStringLiteral("НЕ ВЫПОЛНЕНО");
    }
    return QStringLiteral("НЕИЗВЕСТНО");
}

QString htmlCell(const std::string& text)
{
    return QString::fromStdString(text).toHtmlEscaped();
}

bool componentProvides(const orbita::stand::ComponentProfile& component,
                       const std::string& capability)
{
    const auto& declared = component.capabilities.empty()
        ? component.bindings : component.capabilities;
    return std::find(declared.begin(), declared.end(), capability) != declared.end();
}

} // namespace

UniversalMainWindow::UniversalMainWindow(QWidget* parent)
    : KtmaMainWindow(parent)
    , genericWatcher_(new QFutureWatcher<orbita::stand::ScenarioRunResult>(this))
{
    connect(this, &MainWindow::freeCheckRequested, this, &UniversalMainWindow::openGenericCheck);
    if (auto* home = integrationHomePage()) {
        connect(home, &HomePage::genericCheckRequested,
                this, &UniversalMainWindow::openGenericCheck);
        connect(home, &HomePage::stationAdminRequested, this, [this] {
            integrationEnsureStandRuntime();
            const auto* project = integrationProject();
            if (!project) {
                QMessageBox::warning(this, QStringLiteral("Администрирование станции"),
                    QStringLiteral("Пакет проекта не загружен."));
                return;
            }
            StationAdminDialog dialog(*project, integrationStationSession(), this);
            dialog.exec();
        });
    }

    connect(genericWatcher_, &QFutureWatcherBase::finished,
            this, &UniversalMainWindow::finishGenericScenario);
}

UniversalMainWindow::~UniversalMainWindow()
{
    if (genericWatcher_ && genericWatcher_->isRunning()) {
        integrationStationSession().requestStop();
        genericWatcher_->waitForFinished();
    }
}

void UniversalMainWindow::openGenericCheck()
{
    if (!genericDialog_) {
        genericDialog_ = new GenericCheckDialog(this);
        genericDialog_->setAttribute(Qt::WA_DeleteOnClose, false);

        connect(genericDialog_, &GenericCheckDialog::createScenarioRequested,
                this, &UniversalMainWindow::createGenericScenario);
        connect(genericDialog_, &GenericCheckDialog::editScenarioRequested,
                this, &UniversalMainWindow::editGenericScenario);
        connect(genericDialog_, &GenericCheckDialog::runRequested,
                this, &UniversalMainWindow::runGenericScenario);
        connect(genericDialog_, &GenericCheckDialog::stopRequested,
                this, &UniversalMainWindow::stopGenericScenario);
    }

    reloadGenericScenarios();
    genericDialog_->show();
    genericDialog_->raise();
    genericDialog_->activateWindow();
}

void UniversalMainWindow::reloadGenericScenarios()
{
    if (!genericDialog_) return;
    integrationEnsureStandRuntime();

    QVector<GenericScenarioEntry> entries;
    genericDialog_->setCreationAvailable(false);
    const auto* project = integrationProject();
    if (!project) {
        genericDialog_->setScenarios(entries);
        return;
    }

    QVector<const orbita::stand::WorkflowDefinition*> dynamicWorkflows;
    for (const auto& workflow : project->workflows) {
        if (workflow.allowDynamicScenario || workflow.allowScenarioOverrides)
            dynamicWorkflows.push_back(&workflow);
    }
    if (dynamicWorkflows.isEmpty()) {
        genericDialog_->setScenarios(entries);
        return;
    }

    auto* engine = integrationScenarioEngine();
    if (!engine || !integrationStandRuntimeReady()) {
        genericDialog_->setScenarios(entries);
        return;
    }

    genericDialog_->setCreationAvailable(true);
    const QString userRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QList<QDir> directories = {
        QDir(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("scenarios")))};
    if (!userRoot.isEmpty()) directories.push_back(QDir(QDir(userRoot).filePath(QStringLiteral("scenarios"))));
    for (const auto& directory : directories) {
        for (const auto& file : directory.entryInfoList(
                 {QStringLiteral("*.yaml")}, QDir::Files, QDir::Name)) {
            try {
                const auto scenario = orbita::stand::loadScenarioYaml(
                    file.absoluteFilePath().toUtf8().toStdString());
                const auto errors = engine->validate(scenario);
                QMap<QString, bool> requirements;
                std::function<void(const orbita::stand::ScenarioNode&)> collectRequirements;
                collectRequirements = [&](const orbita::stand::ScenarioNode& node) {
                    for (const auto& capability : node.requiredCapabilities) {
                        const auto* orbitaSource = capability == "orbita.parameter_source"
                            ? orbita::stand::findComponentByBinding(integrationStandProfile(),
                                  "telemetry.orbita.sample_source") : nullptr;
                        // Встроенный сервис Орбиты использует источник, уже
                        // принадлежащий сессии, а не отдельный equipment-плагин.
                        const bool configured = (orbitaSource && orbitaSource->enabled
                            && integrationHasOrbitaSource() && integrationEquipmentRegistry()
                            && integrationEquipmentRegistry()->hasCapability(capability))
                            || std::any_of(integrationStandProfile().components.begin(),
                                integrationStandProfile().components.end(),
                                [&](const auto& component) {
                                    return component.enabled && component.kind == "equipment"
                                        && componentProvides(component, capability);
                                });
                        requirements.insert(QString::fromStdString(capability), configured);
                    }
                    for (const auto& resource : node.requiredResources) {
                        const auto* component = orbita::stand::findComponentById(
                            integrationStandProfile(), resource.resource);
                        if (!component) component = orbita::stand::findComponentByBinding(
                            integrationStandProfile(), resource.resource);
                        const bool configured = component && component->enabled
                            && component->kind == "equipment"
                            && componentProvides(*component, resource.capability);
                        const QString key = QStringLiteral("%1:%2")
                            .arg(QString::fromStdString(resource.resource),
                                 QString::fromStdString(resource.capability));
                        requirements.insert(key, configured);
                    }
                    for (const auto& child : node.children) collectRequirements(child);
                };
                for (const auto& node : scenario.steps) collectRequirements(node);
                QStringList required;
                bool resourcesConfigured = true;
                for (auto it = requirements.cbegin(); it != requirements.cend(); ++it) {
                    required.push_back(QStringLiteral("%1 %2")
                        .arg(it.value() ? QStringLiteral("✓") : QStringLiteral("✗"), it.key()));
                    resourcesConfigured = resourcesConfigured && it.value();
                }
                required.sort();

                for (const auto* workflow : dynamicWorkflows) {
                    entries.push_back({
                        QString::fromStdString(workflow->id),
                        QString::fromStdString(workflow->title),
                        QString::fromStdString(scenario.title),
                        QString::fromStdString(scenario.id),
                        QString::fromStdString(scenario.version),
                        file.absoluteFilePath(),
                        required,
                        resourcesConfigured,
                        scenario.publicationState == orbita::stand::PublicationState::Published,
                        errors.empty() ? QString() : QString::fromStdString(errors.front())});
                }
            } catch (const std::exception& error) {
                integrationLog(QStringLiteral("Свободный сценарий %1 не загружен: %2")
                    .arg(file.fileName(), QString::fromUtf8(error.what())));
            }
        }
    }
    genericDialog_->setScenarios(entries);
}

void UniversalMainWindow::createGenericScenario()
{
    if (genericWatcher_->isRunning() || !genericDialog_) return;
    const auto* project = integrationProject();
    if (!project || std::none_of(project->workflows.begin(), project->workflows.end(),
        [](const auto& workflow) { return workflow.allowDynamicScenario || workflow.allowScenarioOverrides; })) return;
    bool accepted = false;
    const QString title = QInputDialog::getText(this, QStringLiteral("Новая проверка"),
        QStringLiteral("Название проверки:"), QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || title.isEmpty()) return;
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty() || !QDir().mkpath(QDir(root).filePath(QStringLiteral("scenarios")))) {
        QMessageBox::warning(this, QStringLiteral("Новая проверка"), QStringLiteral("Каталог пользовательских проверок недоступен."));
        return;
    }
    const QDir directory(QDir(root).filePath(QStringLiteral("scenarios")));
    try {
        const QString catalogPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("catalog/catalog.yaml"));
        const auto catalog = orbita::stand::yaml::parseFile(catalogPath.toUtf8().toStdString());
        const QString catalogVersion = QString::fromStdString(catalog.value("version"));
        if (catalogVersion.isEmpty()) throw std::runtime_error("Версия каталога не определена");
        const auto scalar = [](const QString& value) {
            const QByteArray encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
            return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
        };
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QByteArray contents = QStringLiteral("schema: 1\nid: %1\ntitle: %2\nversion: \"1\"\ncatalog_version: %3\nstate: draft\nsteps: []\n")
            .arg(scalar(id), scalar(title), scalar(catalogVersion)).toUtf8();
        const QString path = directory.filePath(id + QStringLiteral(".yaml"));
        QTemporaryFile candidate(directory.filePath(QStringLiteral(".scenario-new-XXXXXX")));
        if (!candidate.open() || candidate.write(contents) != contents.size() || !candidate.flush())
            throw std::runtime_error("Не удалось записать новую проверку");
        (void)orbita::stand::loadScenarioYaml(candidate.fileName().toUtf8().toStdString());
        candidate.close();
        if (!candidate.rename(path)) throw std::runtime_error("Не удалось сохранить новую проверку");
        candidate.setAutoRemove(false);
        editGenericScenario(path);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Новая проверка"), QString::fromUtf8(error.what()));
    }
}

void UniversalMainWindow::editGenericScenario(const QString& path)
{
    if (genericWatcher_->isRunning() || !QFileInfo::exists(path)) return;
    const QString workflowId = genericDialog_ ? genericDialog_->currentWorkflowId() : QString();
    ScenarioVisualEditor editor(path, this);
    std::vector<ScenarioParameterOption> parameters;
    for (const auto& binding : integrationParameterBindings())
        if (!binding.parameterKey.isEmpty()) parameters.push_back({binding.sourceId, binding.parameterKey,
            binding.sourceTitle + QStringLiteral(" · ") + binding.parameterTitle, binding.unit});
    editor.setAvailableParameters(std::move(parameters));
    QStringList switchResources;
    for (const auto& component : integrationStandProfile().components) {
        if (!component.enabled || component.kind != "equipment"
            || !componentProvides(component, "stand.switch_matrix")) continue;
        // Идентификатор компонента всегда регистрируется как ресурс. В старом
        // формате bind содержит возможности, а не пользовательские роли.
        switchResources.push_back(QString::fromStdString(component.id));
        if (!component.capabilities.empty())
            for (const auto& binding : component.bindings)
                switchResources.push_back(QString::fromStdString(binding));
    }
    switchResources.removeDuplicates();
    editor.setSwitchResources(switchResources);
    const QString userRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString userScenarios = userRoot.isEmpty() ? QString()
        : QDir(userRoot).filePath(QStringLiteral("scenarios"));
    QStringList versionDirectories{QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("scenarios"))};
    if (!userScenarios.isEmpty()) versionDirectories.push_back(userScenarios);
    editor.setScenarioStorage(userScenarios, versionDirectories);
    if (auto* engine = integrationScenarioEngine()) {
        editor.setAvailableProcedures(engine->registeredProcedures());
        editor.setScenarioValidator([engine](const auto& scenario) { return engine->validate(scenario); });
    }
    editor.exec();
    const QString currentPath = editor.currentPath();
    reloadGenericScenarios();
    if (genericDialog_) genericDialog_->selectScenario(currentPath, workflowId);
}

void UniversalMainWindow::runGenericScenario(
    const QString& workflowId,
    const QString& path,
    const QString& objectSerial,
    const QString& description,
    const QString& engineeringOverride,
    const QString& reportTemplate)
{
    if (genericWatcher_->isRunning()) return;
    if (auto* baseWatcher = integrationScenarioWatcher(); baseWatcher && baseWatcher->isRunning()) {
        QMessageBox::information(this, QStringLiteral("Проверка"),
            QStringLiteral("Сейчас выполняется специализированная проверка КТМА. Дождитесь её завершения."));
        return;
    }

    integrationEnsureStandRuntime();
    auto* engine = integrationScenarioEngine();
    auto* equipment = integrationEquipmentRegistry();
    if (!engine || !equipment || !integrationStandRuntimeReady()) {
        QMessageBox::warning(this, QStringLiteral("Проверка"),
            QStringLiteral("Стендовый runtime не готов."));
        return;
    }

    auto activity = integrationStationSession().beginActivity();
    if (!activity) {
        QMessageBox::warning(this, QStringLiteral("Запуск отклонён"),
            QStringLiteral("Станция занята: выполняется другая активная операция"));
        return;
    }
    engine->resetStop();

    orbita::stand::ScenarioDefinition scenario;
    orbita::stand::ScenarioRunResult initialRun;
    QByteArray scenarioConfigSnapshot;
    const std::string profileVersion = integrationStandProfile().version;
    const std::string serial = objectSerial.toStdString();
    try {
        const auto* project = integrationProject();
        const auto* workflow = project
            ? orbita::stand::findWorkflow(*project, workflowId.toStdString())
            : nullptr;
        if (!project || !workflow
            || (!workflow->allowDynamicScenario && !workflow->allowScenarioOverrides)) {
            throw std::runtime_error("Проект не разрешает выбранный динамический запуск");
        }
        scenario = orbita::stand::loadScenarioYaml(path.toUtf8().toStdString());
        QFile scenarioConfig(path);
        if (!scenarioConfig.open(QIODevice::ReadOnly))
            throw std::runtime_error("Не удалось прочитать снимок конфигурации сценария");
        scenarioConfigSnapshot = scenarioConfig.readAll();
        const auto errors = engine->validate(scenario);
        if (!errors.empty()) throw std::runtime_error(errors.front());
        if (scenario.publicationState != orbita::stand::PublicationState::Published)
            throw std::runtime_error("Черновой сценарий нельзя использовать для приёмочного результата");
        if (workflow->registration.required || workflow->environmentMode == "manual_or_controlled")
            throw std::runtime_error("Для выбранной проверки требуется регистрация изделия или подтверждение условий");

        // Защищаем общий поток от изменения оператором до окончания проверки,
        // включая подготовку оборудования с вложенным циклом событий.
        std::function<bool(const orbita::stand::ScenarioNode&)> usesOrbita;
        usesOrbita = [&](const orbita::stand::ScenarioNode& node) {
            if (node.procedure == "telemetry.health"
                || (node.procedure == "parameter.measure" && node.arguments.count("source")
                    && node.arguments.at("source") == "orbita")
                || std::find(node.requiredCapabilities.begin(), node.requiredCapabilities.end(),
                             "orbita.parameter_source") != node.requiredCapabilities.end()) return true;
            for (const auto& resource : node.requiredResources)
                if (resource.capability == "orbita.parameter_source") return true;
            return std::any_of(node.children.begin(), node.children.end(), usesOrbita);
        };
        if (std::any_of(scenario.steps.begin(), scenario.steps.end(), usesOrbita))
            integrationSetMonitorLocked(true);
        QMap<QString, QStringList> parameterSubscriptions;
        std::function<void(const orbita::stand::ScenarioNode&)> collectParameters;
        collectParameters = [&](const orbita::stand::ScenarioNode& node) {
            if (node.procedure == "parameter.measure" && node.arguments.count("source")
                && node.arguments.count("parameter")) {
                const auto source = QString::fromStdString(node.arguments.at("source"));
                const auto key = QString::fromStdString(node.arguments.at("parameter"));
                if (!parameterSubscriptions[source].contains(key)) parameterSubscriptions[source].push_back(key);
            }
            for (const auto& child : node.children) collectParameters(child);
        };
        for (const auto& node : scenario.steps) collectParameters(node);
        integrationSetScenarioParameterSubscriptions(std::move(parameterSubscriptions));

        // Запись создаётся до подготовки: при аварийном завершении остаётся
        // тот же идентификатор, который затем использует сценарный движок.
        initialRun = orbita::stand::makeInitialRun(scenario, profileVersion, serial);
        initialRun.projectId = project->id;
        initialRun.projectVersion = project->version;
        initialRun.workflowId = workflow->id;
        initialRun.dutType = scenario.objectType;
        initialRun.operatorName = qEnvironmentVariable(
            "USERNAME", qEnvironmentVariable("USER")).toStdString();
        initialRun.environmentProfile = workflow->environmentPath;
        initialRun.contextAttributes["environment_id"] = workflow->environmentId;
        initialRun.contextAttributes["environment_title"] = workflow->environmentTitle;
        initialRun.contextAttributes["environment_mode"] = workflow->environmentMode;
        initialRun.contextAttributes["engineering_description"] = description.toUtf8().toStdString();
        initialRun.contextAttributes["engineering_override_note"] = engineeringOverride.toUtf8().toStdString();
        initialRun.contextAttributes["scenario_config_sha256"] = QCryptographicHash::hash(
            scenarioConfigSnapshot, QCryptographicHash::Sha256).toHex().toStdString();
        initialRun.contextAttributes["scenario_config_source"] = path.toUtf8().toStdString();
        initialRun.contextAttributes["scenario_definition_json"] =
            orbita::stand::scenarioDefinitionSnapshot(scenario);
        if (!integrationPersistRun(initialRun))
            throw std::runtime_error("Не удалось записать начало проверки; подготовка не выполнена");
        initialRun.persistenceStarted = true;

        // Physical preparation belongs to the selected delivery/profile. For
        // KTMA/UBSI this preserves its confirmed power-up order; for another
        // object type only requested devices are probed/bound and no product-
        // specific voltage/current is invented by the universal runner.
        integrationPrepareEquipmentForScenario(scenario, activity);

        QSet<QString> missing;
        std::function<void(const orbita::stand::ScenarioNode&)> collect;
        collect = [&](const orbita::stand::ScenarioNode& node) {
            for (const auto& capability : node.requiredCapabilities) {
                if (!equipment->hasCapability(capability))
                    missing.insert(QString::fromStdString(capability));
            }
            for (const auto& requirement : node.requiredResources) {
                if (!equipment->resourceHasCapability(
                        requirement.resource, requirement.capability)) {
                    missing.insert(QStringLiteral("%1:%2")
                        .arg(QString::fromStdString(requirement.resource),
                             QString::fromStdString(requirement.capability)));
                }
            }
            for (const auto& child : node.children) collect(child);
        };
        for (const auto& node : scenario.steps) collect(node);
        if (!missing.isEmpty()) {
            QStringList missingList;
            for (const auto& capability : missing) missingList.push_back(capability);
            missingList.sort();
            throw std::runtime_error(QStringLiteral(
                "После подготовки стенда отсутствуют требуемые возможности/ресурсы: %1")
                .arg(missingList.join(QStringLiteral(", ")))
                .toUtf8().toStdString());
        }
    } catch (const std::exception& error) {
        integrationSetScenarioParameterSubscriptions({});
        integrationSetMonitorLocked(false);
        integrationStationSession().safeStopAll(activity);
        if (initialRun.persistenceStarted) {
            initialRun.verdict = orbita::stand::RunVerdict::Error;
            initialRun.finishedAt = std::chrono::system_clock::now();
            initialRun.events.push_back({initialRun.finishedAt, {}, "PREPARATION_ERROR",
                error.what(), orbita::stand::RunVerdict::Error});
            if (!integrationPersistRun(initialRun))
                integrationLog(QStringLiteral("Ошибка подготовки не записана; начальный снимок остаётся незавершённым"));
        }
        QMessageBox::warning(this, QStringLiteral("Сценарий / оборудование"),
                             QString::fromUtf8(error.what()));
        return;
    }

    genericDescription_ = description;
    genericEngineeringOverride_ = engineeringOverride;
    genericTemplate_ = reportTemplate;
    genericDialog_->setRunning(true, QStringLiteral("Выполняется: %1")
        .arg(QString::fromStdString(scenario.title)));
    integrationLog(QStringLiteral("Свободная проверка: %1; регистрация результата отключена")
        .arg(QString::fromStdString(scenario.id)));

    genericWatcher_->setFuture(QtConcurrent::run(
        [this, workflowId, path, scenario, profileVersion, serial, activity,
         initialRun, scenarioConfigSnapshot, description, engineeringOverride]() mutable {
            auto reservation = std::move(activity);
            auto checkpoints = orbita::stand::makeIntermediateCheckpointSink(
                QDir(QCoreApplication::applicationDirPath()).filePath("runs/runs.db").toStdString(), true);
            const auto progress = [this](const orbita::stand::RunEvent& event) {
                QMetaObject::invokeMethod(this, [this, event] {
                    if (genericDialog_) genericDialog_->appendEvent(event);
                }, Qt::QueuedConnection);
            };

            try {
                if (const auto* project = integrationProject()) {
                    orbita::stand::ProjectRunContext context;
                    context.dutType = scenario.objectType;
                    context.operatorName = qEnvironmentVariable(
                        "USERNAME", qEnvironmentVariable("USER")).toStdString();
                    context.attributes["engineering_description"] = description.toUtf8().toStdString();
                    context.attributes["engineering_override_note"] = engineeringOverride.toUtf8().toStdString();
                    context.attributes["scenario_config_sha256"] = QCryptographicHash::hash(
                        scenarioConfigSnapshot, QCryptographicHash::Sha256).toHex().toStdString();
                    context.attributes["scenario_config_source"] = path.toUtf8().toStdString();
                    auto result = integrationStationSession().runWorkflow(
                        *project, workflowId.toStdString(), profileVersion, serial, false,
                        std::move(context), &scenario, progress, reservation, checkpoints, &initialRun);
                    result.contextAttributes["engineering_description"] = description.toUtf8().toStdString();
                    result.contextAttributes["engineering_override_note"] = engineeringOverride.toUtf8().toStdString();
                    result.contextAttributes["scenario_config_sha256"] = QCryptographicHash::hash(
                        scenarioConfigSnapshot, QCryptographicHash::Sha256).toHex().toStdString();
                    result.contextAttributes["scenario_config_source"] = path.toUtf8().toStdString();
                    for (auto& event : result.evidence) ++event.sequence;
                    orbita::stand::EvidenceEvent operatorAction;
                    operatorAction.sequence = 1;
                    operatorAction.timestamp = result.startedAt;
                    operatorAction.monotonicNs = 0;
                    operatorAction.type = "OPERATOR_ACTION";
                    operatorAction.nodeId = "free-run";
                    operatorAction.message = "Инженер подтвердил параметры свободного запуска";
                    operatorAction.data = {
                        {"workflow_id", workflowId.toUtf8().toStdString()},
                        {"engineering_override_note", engineeringOverride.toUtf8().toStdString()},
                        {"scenario_config_sha256", result.contextAttributes["scenario_config_sha256"]}};
                    result.evidence.insert(result.evidence.begin(), std::move(operatorAction));

                    try {
                        orbita::stand::RunArtifacts artifacts(
                            QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("runs")).toUtf8().toStdString(), result.runId);
                        QSaveFile snapshot(QDir(QString::fromUtf8(artifacts.directory().c_str()))
                            .filePath(QStringLiteral("scenario-config.yaml")));
                        if (!snapshot.open(QIODevice::WriteOnly)
                            || snapshot.write(scenarioConfigSnapshot) != scenarioConfigSnapshot.size()
                            || !snapshot.commit())
                            throw std::runtime_error("Не удалось сохранить неизменяемый снимок YAML сценария");
                        artifacts.attachFileTo(result, "scenario-config", "scenario-config.yaml",
                                                "application/yaml");
                    } catch (const std::exception& error) {
                        result.verdict = orbita::stand::RunVerdict::Error;
                        orbita::stand::EvidenceEvent failure;
                        failure.sequence = result.evidence.size() + 1;
                        failure.timestamp = std::chrono::system_clock::now();
                        failure.type = "ERROR";
                        failure.nodeId = "free-run";
                        failure.message = std::string("Не удалось сохранить снимок сценария: ") + error.what();
                        failure.verdict = orbita::stand::RunVerdict::Error;
                        result.evidence.push_back(std::move(failure));
                    }
                    return result;
                }

                throw std::runtime_error("Проект проверки больше недоступен");
            } catch (const std::exception& error) {
                // При исключении копия не содержит все записанные шаги.
                // Оставляем последний подтверждённый снимок незавершённым.
                integrationStationSession().safeStopAll(reservation);
                auto failed = initialRun;
                failed.verdict = orbita::stand::RunVerdict::Error;
                failed.preserveCheckpoint = true;
                failed.finishedAt = std::chrono::system_clock::now();
                failed.events.push_back({failed.finishedAt, {}, "ENGINE", error.what(),
                                         orbita::stand::RunVerdict::Error});
                return failed;
            }
        }));
}

void UniversalMainWindow::stopGenericScenario()
{
    if (!genericWatcher_->isRunning()) return;
    integrationStationSession().requestStop();
    if (genericDialog_)
        genericDialog_->setRunning(true, QStringLiteral("Остановка и безопасный сброс оборудования…"));
}

void UniversalMainWindow::finishGenericScenario()
{
    integrationSetScenarioParameterSubscriptions({});
    integrationSetMonitorLocked(false);
    if (!genericDialog_) return;
    const auto result = genericWatcher_->result();
    if (!result.events.empty() && result.events.front().stage == "STATION_BUSY") {
        const auto reason = QString::fromUtf8(result.events.front().message.c_str());
        genericDialog_->setRunning(false, reason);
        integrationLog(reason);
        QMessageBox::warning(this, QStringLiteral("Запуск отклонён"), reason);
        return;
    }
    const bool saved = integrationPersistRun(result);
    const QString html = renderGenericReport(result);
    const QString summary = QStringLiteral(
        "Свободная проверка завершена: %1. Run %2; в реестр поставки результат не записан.")
        .arg(verdictText(result.verdict), saved ? QStringLiteral("сохранён")
                                                : QStringLiteral("не сохранён"));
    genericDialog_->setReportHtml(html, summary);
    integrationLog(summary);
}

QString UniversalMainWindow::renderGenericReport(
    const orbita::stand::ScenarioRunResult& result) const
{
    QString html = genericTemplate_;
    if (html.trimmed().isEmpty()) {
        html = QStringLiteral(
            "<html><body><h1>Отчёт проверки</h1><p>{{description}}</p>"
            "<p>Инженерное изменение: {{engineering_override}}</p>"
            "<p>Сценарий SHA-256: {{scenario_config_sha256}}</p>"
            "<p>{{project_id}} · {{workflow_id}}</p>"
            "<p>{{scenario_title}} · {{object_serial}}</p><h2>{{verdict}}</h2>"
            "{{steps}}{{events}}</body></html>");
    }

    const QHash<QString, QString> replacements = {
        {QStringLiteral("{{description}}"), genericDescription_.toHtmlEscaped()},
        {QStringLiteral("{{engineering_override}}"), genericEngineeringOverride_.toHtmlEscaped()},
        {QStringLiteral("{{scenario_config_sha256}}"), QString::fromStdString(
            result.contextAttributes.count("scenario_config_sha256")
                ? result.contextAttributes.at("scenario_config_sha256") : std::string()).toHtmlEscaped()},
        {QStringLiteral("{{project_id}}"), QString::fromStdString(result.projectId).toHtmlEscaped()},
        {QStringLiteral("{{project_version}}"), QString::fromStdString(result.projectVersion).toHtmlEscaped()},
        {QStringLiteral("{{workflow_id}}"), QString::fromStdString(result.workflowId).toHtmlEscaped()},
        {QStringLiteral("{{object_serial}}"), QString::fromStdString(result.objectSerial).toHtmlEscaped()},
        {QStringLiteral("{{scenario_id}}"), QString::fromStdString(result.scenarioId).toHtmlEscaped()},
        {QStringLiteral("{{scenario_title}}"), QString::fromStdString(result.scenarioTitle).toHtmlEscaped()},
        {QStringLiteral("{{scenario_version}}"), QString::fromStdString(result.scenarioVersion).toHtmlEscaped()},
        {QStringLiteral("{{profile_version}}"), QString::fromStdString(result.profileVersion).toHtmlEscaped()},
        {QStringLiteral("{{verdict}}"), verdictText(result.verdict).toHtmlEscaped()},
        {QStringLiteral("{{started_at}}"), formatTimestamp(result.startedAt).toHtmlEscaped()},
        {QStringLiteral("{{finished_at}}"), formatTimestamp(result.finishedAt).toHtmlEscaped()},
        {QStringLiteral("{{steps}}"), renderSteps(result.steps)},
        {QStringLiteral("{{events}}"), renderEvents(result.events)}};
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it)
        html.replace(it.key(), it.value());
    return html;
}

QString UniversalMainWindow::renderSteps(
    const std::vector<orbita::stand::StepRunResult>& steps, int level) const
{
    QString html;
    if (level == 0)
        html += QStringLiteral("<table><thead><tr><th>Этап / измерение</th><th>Требование</th><th>Измерено</th><th>Результат</th><th>Сообщение</th></tr></thead><tbody>");
    for (const auto& step : steps) {
        const QString indent(level * 4, QChar(0x00A0));
        html += QStringLiteral("<tr><td>%1%2</td><td>%3</td><td></td><td><b>%4</b></td><td>%5</td></tr>")
            .arg(indent,
                 htmlCell(step.title),
                 htmlCell(step.tuRequirement),
                 verdictText(step.verdict).toHtmlEscaped(),
                 htmlCell(step.message));
        for (const auto& measurement : step.measurements) {
            html += QStringLiteral("<tr><td>%1%2</td><td></td><td>%3 %4</td><td>%5</td><td>%6</td></tr>")
                .arg(indent + QString(4, QChar(0x00A0)),
                     htmlCell(measurement.title),
                     QString::number(measurement.measured, 'g', 12).toHtmlEscaped(),
                     htmlCell(measurement.unit),
                     verdictText(measurement.verdict).toHtmlEscaped(),
                     htmlCell(measurement.message));
        }
        if (!step.children.empty()) html += renderSteps(step.children, level + 1);
    }
    if (level == 0) html += QStringLiteral("</tbody></table>");
    return html;
}

QString UniversalMainWindow::renderEvents(
    const std::vector<orbita::stand::RunEvent>& events) const
{
    QString html = QStringLiteral(
        "<table><thead><tr><th>Время</th><th>Узел</th><th>Стадия</th><th>Результат</th><th>Сообщение</th></tr></thead><tbody>");
    for (const auto& event : events) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
            .arg(formatTimestamp(event.timestamp).toHtmlEscaped(),
                 htmlCell(event.nodeId),
                 htmlCell(event.stage),
                 verdictText(event.verdict).toHtmlEscaped(),
                 htmlCell(event.message));
    }
    html += QStringLiteral("</tbody></table>");
    return html;
}

QString UniversalMainWindow::formatTimestamp(
    std::chrono::system_clock::time_point timestamp) const
{
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(milliseconds)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}
