#include "universal_mainwindow.h"

#include "generic_check_dialog.h"
#include "home_page.h"
#include "scenario_yaml_editor.h"
#include "station_admin_dialog.h"

#include <QCoreApplication>
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
        if (auto* engine = integrationScenarioEngine()) engine->requestStop();
        genericWatcher_->waitForFinished();
    }
}

void UniversalMainWindow::openGenericCheck()
{
    if (!genericDialog_) {
        genericDialog_ = new GenericCheckDialog(this);
        genericDialog_->setAttribute(Qt::WA_DeleteOnClose, false);

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

    const QDir directory(QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("scenarios")));
    for (const auto& file : directory.entryInfoList(
             {QStringLiteral("*.yaml")}, QDir::Files, QDir::Name)) {
        try {
            const auto scenario = orbita::stand::loadScenarioYaml(
                file.absoluteFilePath().toUtf8().toStdString());
            const auto errors = engine->validate(scenario);
            if (!errors.empty()) {
                integrationLog(QStringLiteral("Свободный сценарий %1 пропущен: %2")
                    .arg(file.fileName(), QString::fromStdString(errors.front())));
                continue;
            }
            QMap<QString, bool> requirements;
            std::function<void(const orbita::stand::ScenarioNode&)> collectRequirements;
            collectRequirements = [&](const orbita::stand::ScenarioNode& node) {
                for (const auto& capability : node.requiredCapabilities) {
                    const bool configured = std::any_of(
                        integrationStandProfile().components.begin(),
                        integrationStandProfile().components.end(),
                        [&](const auto& component) {
                            return component.enabled && component.kind == "equipment"
                                && componentProvides(component, capability);
                        });
                    requirements.insert(QString::fromStdString(capability), configured);
                }
                for (const auto& resource : node.requiredResources) {
                    const auto* component = orbita::stand::findComponentByBinding(
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
                    resourcesConfigured});
            }
        } catch (const std::exception& error) {
            integrationLog(QStringLiteral("Свободный сценарий %1 не загружен: %2")
                .arg(file.fileName(), QString::fromUtf8(error.what())));
        }
    }
    genericDialog_->setScenarios(entries);
}

void UniversalMainWindow::editGenericScenario(const QString& path)
{
    if (genericWatcher_->isRunning() || !QFileInfo::exists(path)) return;
    ScenarioYamlEditor editor(path, this);
    editor.exec();
    reloadGenericScenarios();
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

    orbita::stand::ScenarioDefinition scenario;
    QByteArray scenarioConfigSnapshot;
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

        // Physical preparation belongs to the selected delivery/profile. For
        // KTMA/UBSI this preserves its confirmed power-up order; for another
        // object type only requested devices are probed/bound and no product-
        // specific voltage/current is invented by the universal runner.
        integrationPrepareEquipmentForScenario(scenario);

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
        integrationStationSession().safeStopAll();
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

    engine->resetStop();
    const std::string profileVersion = integrationStandProfile().version;
    const std::string serial = objectSerial.toStdString();
    auto* leases = &integrationStationSession().leases();
    genericWatcher_->setFuture(QtConcurrent::run(
        [this, workflowId, path, scenario, profileVersion, serial, leases,
         scenarioConfigSnapshot, description, engineeringOverride] {
            const auto progress = [this](const orbita::stand::RunEvent& event) {
                QMetaObject::invokeMethod(this, [this, event] {
                    if (genericDialog_) genericDialog_->appendEvent(event);
                }, Qt::QueuedConnection);
            };

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
                auto result = orbita::stand::runProjectWorkflow(
                    *project, workflowId.toStdString(), *integrationScenarioEngine(),
                    *integrationEquipmentRegistry(), profileVersion, serial, false,
                    std::move(context), &scenario, progress,
                    leases);
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

            // Compatibility fallback for development layouts that do not yet
            // deploy project packages next to the executable.
            auto result = orbita::stand::runScenarioWithEvidence(
                *integrationScenarioEngine(), *integrationEquipmentRegistry(),
                scenario, profileVersion, serial, false, progress,
                leases);
            result.contextAttributes["engineering_description"] = description.toUtf8().toStdString();
            result.contextAttributes["engineering_override_note"] = engineeringOverride.toUtf8().toStdString();
            result.contextAttributes["scenario_config_sha256"] = QCryptographicHash::hash(
                scenarioConfigSnapshot, QCryptographicHash::Sha256).toHex().toStdString();
            return result;
        }));
}

void UniversalMainWindow::stopGenericScenario()
{
    if (!genericWatcher_->isRunning()) return;
    if (auto* engine = integrationScenarioEngine()) engine->requestStop();
    if (genericDialog_)
        genericDialog_->setRunning(true, QStringLiteral("Остановка и безопасный сброс оборудования…"));
}

void UniversalMainWindow::finishGenericScenario()
{
    if (!genericDialog_) return;
    const auto result = genericWatcher_->result();
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
        html += QStringLiteral("<table><thead><tr><th>Этап</th><th>Требование</th><th>Результат</th><th>Сообщение</th></tr></thead><tbody>");
    for (const auto& step : steps) {
        const QString indent(level * 4, QChar(0x00A0));
        html += QStringLiteral("<tr><td>%1%2</td><td>%3</td><td><b>%4</b></td><td>%5</td></tr>")
            .arg(indent,
                 htmlCell(step.title),
                 htmlCell(step.tuRequirement),
                 verdictText(step.verdict).toHtmlEscaped(),
                 htmlCell(step.message));
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
