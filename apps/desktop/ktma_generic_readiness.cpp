#include "ktma_mainwindow.h"

#include "ktma/ubsi/equipment_readiness.h"
#include "orbita_stand/catalog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QInputDialog>
#include <QLineEdit>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {

const std::vector<std::string>& componentCapabilities(
    const orbita::stand::ComponentProfile& component)
{
    return component.capabilities.empty() ? component.bindings : component.capabilities;
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

bool enabledFlag(const std::map<std::string, std::string>& config,
                 const std::string& key)
{
    const auto found = config.find(key);
    if (found == config.end()) return false;
    return found->second == "true" || found->second == "1"
        || found->second == "yes" || found->second == "on";
}

bool providesAny(const orbita::stand::ComponentProfile& component,
                 const std::set<std::string>& required)
{
    if (required.empty()) return true;
    const auto& capabilities = componentCapabilities(component);
    return std::any_of(capabilities.begin(), capabilities.end(),
        [&](const std::string& capability) { return required.count(capability) != 0; });
}

} // namespace

void KtmaMainWindow::integrationPrepareEquipmentForScenario(
    const orbita::stand::ScenarioDefinition& scenario)
{
    integrationEnsureStandRuntime();
    if (!integrationStandRuntimeReady())
        throw std::runtime_error("Стендовый runtime не инициализирован");

    auto& session = integrationStationSession();
    auto& profile = integrationStandProfile();
    auto* registry = integrationEquipmentRegistry();
    if (!registry) throw std::runtime_error("Реестр оборудования не инициализирован");

    const auto required = scenarioCapabilities(scenario);
    session.clearPhysicalEquipment();

    // Application-level services survive clearPhysicalEquipment(), but these two
    // bindings are intentionally refreshed so a free scenario has the same
    // catalog/manual contracts as the specialised KTMA workflow.
    const std::string catalogDatabase = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("parameters.db")).toUtf8().toStdString();
    registry->bind("catalog.parameter_resolver",
        [catalogDatabase](const std::string& operation,
                          const std::map<std::string, std::string>& arguments) {
            if (operation != "resolve")
                throw std::invalid_argument("Неизвестная операция каталога: " + operation);
            const auto requiredArgument = [&arguments](const char* key) -> const std::string& {
                const auto value = arguments.find(key);
                if (value == arguments.end() || value->second.empty())
                    throw std::invalid_argument(std::string("Каталогу требуется ") + key);
                return value->second;
            };
            const auto binding = orbita::stand::resolveCatalogParameterBinding(
                catalogDatabase, requiredArgument("block_type"), requiredArgument("parameter_group"),
                static_cast<unsigned>(std::stoul(requiredArgument("channel_index"))));
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

            const QString title = arguments.count("title")
                ? QString::fromStdString(arguments.at("title"))
                : QStringLiteral("Ручная операция");
            const QString operatorName = qEnvironmentVariable(
                "USERNAME", qEnvironmentVariable("USER", QStringLiteral("неизвестен")));

            if (operation == "confirm_text") {
                bool accepted = false;
                QString value;
                const QString prompt = arguments.count("prompt")
                    ? QString::fromStdString(arguments.at("prompt"))
                    : QStringLiteral("Введите подтверждающее значение:");
                QMetaObject::invokeMethod(this, [&] {
                    value = QInputDialog::getText(
                        this, title, prompt, QLineEdit::Normal, {}, &accepted).trimmed();
                }, Qt::BlockingQueuedConnection);
                std::ostringstream response;
                response << "status=" << (accepted && !value.isEmpty() ? "confirmed" : "not_confirmed")
                         << "\nvalue=" << value.toStdString()
                         << "\noperator=" << operatorName.toStdString()
                         << "\ntimestamp="
                         << QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString()
                         << "\n";
                return response.str();
            }

            bool accepted = false;
            double actual = 0.0;
            QString prompt = QStringLiteral("Введите фактическое значение:");
            if (const auto target = arguments.find("target_value"); target != arguments.end()) {
                actual = QString::fromStdString(target->second).toDouble();
                const QString unit = arguments.count("unit")
                    ? QString::fromStdString(arguments.at("unit")) : QString();
                prompt = QStringLiteral("Требуется: %1 %2\nВведите фактически установленное значение:")
                    .arg(QString::fromStdString(target->second), unit);
            }
            QMetaObject::invokeMethod(this, [&] {
                actual = QInputDialog::getDouble(
                    this, title, prompt, actual, -1000000.0, 1000000.0, 6, &accepted);
            }, Qt::BlockingQueuedConnection);
            if (!accepted) throw std::runtime_error("Ручная операция отменена оператором");
            std::ostringstream response;
            response << std::setprecision(15)
                     << "status=confirmed\nvalue=" << actual
                     << "\noperator=" << operatorName.toStdString()
                     << "\ntimestamp="
                     << QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString()
                     << "\n";
            return response.str();
        });

    const bool ubsiWorkflow = scenario.objectType.rfind("UBSI", 0) == 0;

    auto prepareComponent = [this, &session, &profile](
        const orbita::stand::ComponentProfile& component, bool armUbsiSupply) {
        if (!component.enabled) {
            const auto reason = component.configuration.find("disabled_reason");
            integrationLog(QStringLiteral("%1: %2")
                .arg(QString::fromStdString(component.id),
                     reason == component.configuration.end()
                         ? QStringLiteral("отключено в профиле")
                         : QString::fromStdString(reason->second)));
            return;
        }

        std::shared_ptr<orbita::stand::EquipmentDevice> device;
        try {
            device = session.createEquipmentComponent(component.id);
            const auto& capabilities = componentCapabilities(component);
            if (capabilities.empty())
                throw std::runtime_error("В профиле не указана capability оборудования");

            const std::string response = device->invoke(capabilities.front(), "probe", {});
            if (response.find("alive=0") != std::string::npos
                || response.find("alive=false") != std::string::npos) {
                throw std::runtime_error("Оборудование не подтвердило готовность");
            }

            const bool activeComponent = armUbsiSupply
                || std::find(capabilities.begin(), capabilities.end(), "stand.switch_matrix")
                    != capabilities.end()
                || std::find(capabilities.begin(), capabilities.end(), "signal.generator")
                    != capabilities.end();
            const auto confirmation = component.configuration.find("device.active_commands_confirmed");
            const bool explicitlyBlocked = confirmation != component.configuration.end()
                && !enabledFlag(component.configuration, "device.active_commands_confirmed");
            const bool deviceConfirmed = enabledFlag(
                component.configuration, "device.active_commands_confirmed");
            if (activeComponent && (explicitlyBlocked
                || (!profile.activeOutputsConfirmed && !deviceConfirmed))) {
                throw std::runtime_error(
                    "Активные воздействия заблокированы профилем стенда");
            }

            std::string finalResponse = response;
            if (armUbsiSupply) {
                device->invoke("power.dc_supply", "set_current_limit", {{"amperes", "0.6"}});
                device->invoke("power.dc_supply", "set_voltage", {{"volts", "27.0"}});
                device->invoke("power.dc_supply", "output", {{"enabled", "true"}});
                finalResponse = device->invoke("power.dc_supply", "read_state", {});
                if (finalResponse.find("output_enabled=true") == std::string::npos)
                    throw std::runtime_error("Источник не подтвердил питание УБСИ 27 В");
            }

            session.bindEquipmentComponent(component.id, device, true);
            integrationLog(QStringLiteral("%1: готово · %2")
                .arg(QString::fromStdString(component.id),
                     QString::fromStdString(finalResponse).trimmed()));
        } catch (...) {
            if (device) device->safeStop();
            throw;
        }
    };

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        if (ubsiWorkflow) {
            const auto plan = ktma::ubsi::buildEquipmentReadinessPlan(profile, required);
            ktma::ubsi::executeEquipmentReadinessPlan(profile, plan, {
                [&](const orbita::stand::ComponentProfile& component, bool armSupply) {
                    prepareComponent(component, armSupply);
                },
                [this](unsigned milliseconds) {
                    integrationLog(QStringLiteral("Выдержка %1 мс перед запуском адаптера")
                        .arg(milliseconds));
                    QEventLoop delay;
                    QTimer::singleShot(static_cast<int>(milliseconds), &delay, &QEventLoop::quit);
                    delay.exec(QEventLoop::ExcludeUserInputEvents);
                },
            });
        } else {
            // For arbitrary projects the universal runner does not invent a
            // product-specific voltage/current sequence. It only creates,
            // probes and binds equipment required by the scenario. Any active
            // stimulus must be an explicit scenario operation.
            for (const auto& component : profile.components) {
                if (component.kind != "equipment" || !providesAny(component, required)) continue;
                prepareComponent(component, false);
            }
        }
    } catch (...) {
        session.clearPhysicalEquipment();
        QApplication::restoreOverrideCursor();
        throw;
    }
    QApplication::restoreOverrideCursor();
}