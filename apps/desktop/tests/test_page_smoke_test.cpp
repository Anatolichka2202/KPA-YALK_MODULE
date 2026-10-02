#include "test_page.h"
#include "generic_check_dialog.h"
#include "home_page.h"
#include "scenario_yaml_editor.h"
#include "station_admin_dialog.h"

#include "orbita_stand/project.h"
#include "orbita_stand/station_session.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>

#include <cstdlib>
#include <cmath>
#include <iostream>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    HomePage home;
    home.setProjectWorkflows({
        {QStringLiteral("tu_normal"), QStringLiteral("Проверка по ТУ"), true, {},
         QStringLiteral("tu")},
        {QStringLiteral("tu_climate"), QStringLiteral("Климатические условия"), false,
         QStringLiteral("Environment runtime не реализован"), QStringLiteral("tu")}});
    auto* tuWorkflow = home.findChild<QPushButton*>(QStringLiteral("projectWorkflow_tu_normal"));
    auto* climateWorkflow = home.findChild<QPushButton*>(QStringLiteral("projectWorkflow_tu_climate"));
    require(tuWorkflow && !climateWorkflow,
            "home page must expose the TU route and hide unavailable climate workflow");
    auto* adminAction = home.findChild<QPushButton*>(QStringLiteral("stationAdminAction"));
    require(adminAction && !adminAction->isEnabled(),
            "unfinished administration must not be launchable from operator home");
    auto* freeAction = home.findChild<QPushButton*>(QStringLiteral("genericFreeAction"));
    require(freeAction && freeAction->isEnabled(),
            "free mode must open the read-only workspace instead of a YAML launcher");
    bool freeWorkspaceRequested = false;
    QObject::connect(&home, &HomePage::freeWorkspaceRequested, &home,
        [&] { freeWorkspaceRequested = true; });
    freeAction->click();
    require(freeWorkspaceRequested,
            "free mode must dispatch the dedicated read-only workspace request");
    QString dispatchedWorkflow;
    QObject::connect(&home, &HomePage::workflowRequested, &home,
        [&](const QString& workflowId) { dispatchedWorkflow = workflowId; });
    tuWorkflow->click();
    require(dispatchedWorkflow == QStringLiteral("tu_normal"),
            "home page must dispatch the selected project workflow id");

    GenericCheckDialog launcher;
    launcher.setScenarios({{
        QStringLiteral("free"), QStringLiteral("Свободная проверка проекта"),
        QStringLiteral("Сценарий контракта"), QStringLiteral("contract.scenario"),
        QStringLiteral("1"), QStringLiteral("scenario.yaml"),
        {QStringLiteral("measure.reference_voltage"),
         QStringLiteral("switch_matrix.primary:stand.switch_matrix")}}});
    auto* launcherScenario = launcher.findChild<QComboBox*>(QStringLiteral("genericScenario"));
    auto* launcherResources = launcher.findChild<QLabel*>(QStringLiteral("genericResourcePreview"));
    auto* launcherRun = launcher.findChild<QPushButton*>(QStringLiteral("runGeneric"));
    auto* overrideNote = launcher.findChild<QPlainTextEdit*>(QStringLiteral("genericEngineeringOverride"));
    require(launcherScenario && launcherResources && launcherRun && overrideNote,
            "project workflow launcher controls not found");
    require(launcherScenario->currentData(Qt::UserRole + 1).toString()
                == QStringLiteral("free"),
            "launcher must carry selected project workflow identity");
    require(launcherResources->text().contains(QStringLiteral("Свободная проверка проекта"))
                && launcherResources->text().contains(QStringLiteral("measure.reference_voltage"))
                && launcherResources->text().contains(QStringLiteral("switch_matrix.primary")),
            "launcher must preview workflow and scenario resource requirements");
    bool launcherRunRequested = false;
    overrideNote->setPlainText(QStringLiteral("Допуск сценария подтверждён инженером"));
    QObject::connect(&launcher, &GenericCheckDialog::runRequested, &launcher,
        [&](const QString& workflowId, const QString& scenarioPath,
            const QString&, const QString&, const QString& overrideText, const QString&) {
            launcherRunRequested = workflowId == QStringLiteral("free")
                && scenarioPath == QStringLiteral("scenario.yaml")
                && overrideText == QStringLiteral("Допуск сценария подтверждён инженером");
        });
    launcherRun->click();
    require(launcherRunRequested,
            "launcher must dispatch the selected workflow together with its scenario");

    orbita::stand::ProjectDefinition adminProject;
    adminProject.id = "contract-project";
    adminProject.title = "Contract project";
    adminProject.version = "1";
    orbita::stand::StationSession adminSession;
    StationAdminDialog adminDialog(adminProject, adminSession);
    auto* adminTabs = adminDialog.findChild<QTabWidget*>();
    auto* adminDiagnostics = adminDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("stationAdminDiagnostics"));
    require(adminTabs && adminTabs->count() == 3 && adminDiagnostics
                && adminDiagnostics->isReadOnly(),
            "station admin must expose read-only connections, resources, diagnostics tabs");

    QTemporaryDir editorFixture;
    require(editorFixture.isValid(), "cannot create editor fixture directory");
    const auto scenarioDirectory = QDir(editorFixture.path()).filePath(QStringLiteral("scenarios"));
    require(QDir().mkpath(scenarioDirectory), "cannot create scenario fixture directory");
    const auto publishedPath = QDir(scenarioDirectory).filePath(QStringLiteral("published.yaml"));
    QFile publishedFile(publishedPath);
    require(publishedFile.open(QIODevice::WriteOnly | QIODevice::Text),
            "cannot create published scenario fixture");
    publishedFile.write("schema: 1\nid: published\nversion: 1\nstate: published\nsteps: []\n");
    publishedFile.close();
    ScenarioYamlEditor publishedEditor(publishedPath);
    auto* publishedSave = publishedEditor.findChild<QPushButton*>(
        QStringLiteral("saveScenarioConfig"));
    require(publishedSave && !publishedSave->isEnabled(),
            "published scenario configuration must not be saved in place");

    TestPage page;
    const QString screenshot = qEnvironmentVariable("ORBITA_UI_SCREENSHOT");
    const QString scene = qEnvironmentVariable("MILTECH_UI_SCENE", QStringLiteral("YALK"));

    auto* object = page.findChild<QComboBox*>(QStringLiteral("testObject"));
    auto* scope = page.findChild<QComboBox*>(QStringLiteral("testScope"));
    auto* test = page.findChild<QComboBox*>(QStringLiteral("testType"));
    auto* mode = page.findChild<QComboBox*>(QStringLiteral("testMode"));
    auto* operatorConfiguration = page.findChild<QWidget*>(
        QStringLiteral("operatorConfigurationBridge"));
    auto* serial = page.findChild<QLineEdit*>(QStringLiteral("objectSerial"));
    auto* operatorEdit = page.findChild<QLineEdit*>(QStringLiteral("operatorName"));
    auto* operatorHistory = page.findChild<QComboBox*>(QStringLiteral("operatorHistory"));
    auto* lifecycle = page.findChild<QComboBox*>(QStringLiteral("productionLifecycle"));
    auto* operatorComment = page.findChild<QPlainTextEdit*>(
        QStringLiteral("productionOperatorComment"));
    auto* scenarioEditor = page.findChild<QPushButton*>(
        QStringLiteral("productionScenarioEditor"));
    auto* session = page.findChild<QTableWidget*>(QStringLiteral("productionSessionTable"));
    auto* equipment = page.findChild<QTableWidget*>(QStringLiteral("equipmentTable"));
    auto* histogram = page.findChild<QWidget*>(QStringLiteral("yalkChannelHistogram"));
    auto* contacts = page.findChild<QWidget*>(QStringLiteral("yalkContactThresholdOverview"));
    auto* overloadOverview = page.findChild<QWidget*>(QStringLiteral("yalkOverloadOverview"));
    auto* ytpHistogram = page.findChild<QWidget*>(QStringLiteral("ytpChannelHistogram"));

    require(object && scope && test && mode && operatorConfiguration && serial && operatorEdit && operatorHistory && lifecycle && operatorComment && scenarioEditor && session
                && equipment && histogram && contacts && overloadOverview && ytpHistogram,
            "new operator UI controls not found");
    require(operatorConfiguration->isHidden(),
            "operator UI must not expose internal scenario, mode and diagnostic selectors");
    require(operatorEdit->placeholderText() == QStringLiteral("Фамилия Имя Отчество"),
            "operator name must not be mixed with personnel number");
    require(page.styleSheet().contains(QStringLiteral("#14171c")),
            "operator UI must keep the dark industrial palette");

    int routeEntries = 0;
    for (auto* label : page.findChildren<QLabel*>()) {
        if (label->property("routeStageIndex").isValid()) {
            ++routeEntries;
            require(label->cursor().shape() == Qt::PointingHandCursor,
                    "route stage must be visibly interactive");
        }
    }
    require(routeEntries == 6, "operator workspace must expose six clickable route stages");

    page.setProductionMode(true);
    require(!lifecycle->isHidden() && !operatorComment->isHidden() && !scenarioEditor->isHidden(),
            "production session must expose lifecycle context and scenario editor");
    lifecycle->setCurrentIndex(lifecycle->findData(QStringLiteral("after_climate")));
    operatorComment->setPlainText(QStringLiteral("Протокол климатической камеры №42"));
    require(page.productionLifecycle() == QStringLiteral("after_climate")
                && page.productionOperatorComment().contains(QStringLiteral("№42")),
            "production lifecycle and operator comment must be readable for evidence");
    if (!screenshot.isEmpty() && scene == QStringLiteral("PRODUCTION_SESSION")) {
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot),
                "cannot save production-session operator UI screenshot");
    }
    const int yalkScope = scope->findData(QStringLiteral("ЯЛК-96"));
    require(yalkScope >= 0, "YALK production scope not found");
    scope->setCurrentIndex(yalkScope);
    int visibleRouteNumber = 0;
    for (auto* label : page.findChildren<QLabel*>()) {
        if (!label->property("routeStageIndex").isValid() || label->isHidden()) continue;
        ++visibleRouteNumber;
        require(label->text().contains(QStringLiteral("%1.").arg(visibleRouteNumber)),
                "visible production route stages must be numbered consecutively");
    }
    require(visibleRouteNumber == 4, "YALK production route must expose four relevant stages");
    scope->setCurrentIndex(scope->findData(QStringLiteral("УБСИ ПО ТУ")));
    page.setScenarioInfo(QStringLiteral("PROD_FULL"), true, false,
        {QStringLiteral("AKIP"), QStringLiteral("RS485"), QStringLiteral("ISD"),
         QStringLiteral("V7"), QStringLiteral("R4831")}, QStringLiteral("ready"));
    require(page.currentScenarioCode() == QStringLiteral("PROD_FULL"),
            "production card selection must directly own the backend scenario code");

    operatorEdit->setText(QStringLiteral("Иванов И.И."));
    serial->setText(QStringLiteral("УБСИ-0001"));
    auto* add = page.findChild<QPushButton*>(QStringLiteral("addProductionProduct"));
    require(add, "add UBSI button not found");
    add->click();
    require(session->rowCount() == 1, "UBSI must be added to one production session");
    require(session->item(0, 0)->text() == QStringLiteral("УБСИ-0001"),
            "production session must retain UBSI serial");

    page.registerEquipmentRow(QStringLiteral("RIGOL"), QStringLiteral("Rigol"),
                              QStringLiteral("USB"), QStringLiteral("not checked"));
    page.setEquipmentStatus(QStringLiteral("AKIP"), true, QStringLiteral("27.0 V"));
    page.setEquipmentStatus(QStringLiteral("RS485"), true, QStringLiteral("stream ready"));
    page.setEquipmentStatus(QStringLiteral("ISD"), true, QStringLiteral("ready"));
    page.setEquipmentStatus(QStringLiteral("V7"), true, QStringLiteral("ready"));

    auto* enter = page.findChild<QPushButton*>(QStringLiteral("enterPreparation"));
    require(enter, "enter preparation button not found");
    enter->click();

    // Передаём реальные формы RunEvent, используемые backend. Страница обязана
    // обработать их без синтетического демонстрационного движка.
    page.setRunInProgress(true, QStringLiteral("full production"));
    orbita::stand::RunEvent supply;
    supply.nodeId = "supply_range";
    supply.stage = "SUPPLY";
    supply.data = {{"setpoint_v", "27"}, {"volts", "27.01"},
                   {"amperes", "0.238"}, {"elapsed_s", "0"}, {"duration_s", "0"}};
    page.setRunEvent(supply);
    const double powerRoute[] = {24.0, 27.0, 35.0, 19.0, 19.0, 19.0, 19.0, 19.0};
    for (int index = 0; index < 8; ++index) {
        const double setpoint = powerRoute[index];
        supply.data = {{"setpoint_v", std::to_string(setpoint)},
                       {"volts", std::to_string(setpoint + (index % 3 - 1) * 0.012)},
                       {"amperes", std::to_string(0.238 + (index % 4 - 2) * 0.0015)},
                       {"elapsed_s", std::to_string(index >= 3 ? 28 + index : 0)},
                       {"duration_s", index >= 3 ? "300" : "0"}};
        page.setRunEvent(supply);
    }
    if (!screenshot.isEmpty() && scene == QStringLiteral("POWER")) {
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot), "cannot save power operator UI screenshot");
    }

    orbita::stand::RunEvent yalkStart;
    yalkStart.nodeId = "yalk_channels";
    yalkStart.stage = "START";
    yalkStart.message = "YALK channels";
    page.setRunEvent(yalkStart);

    orbita::stand::RunEvent yalkBackground;
    yalkBackground.nodeId = "monitor";
    yalkBackground.stage = "BACKGROUND";
    yalkBackground.data = {{"section", "YALK"}};
    std::string backgroundMean;
    std::string backgroundMinimum;
    std::string backgroundMaximum;
    for (int index = 0; index < 100; ++index) {
        if (index > 0) {
            backgroundMean += ',';
            backgroundMinimum += ',';
            backgroundMaximum += ',';
        }
        const double mean = 3.1 + (index % 9 - 4) * 0.003;
        const double spread = index % 11 == 0 ? 0.018 : 0.006;
        backgroundMean += std::to_string(mean);
        backgroundMinimum += std::to_string(mean - spread);
        backgroundMaximum += std::to_string(mean + spread);
    }
    yalkBackground.data["background_mean"] = backgroundMean;
    yalkBackground.data["background_min"] = backgroundMinimum;
    yalkBackground.data["background_max"] = backgroundMaximum;
    page.setRunEvent(yalkBackground);
    QApplication::processEvents();
    require(histogram->property("backgroundChannelCount").toInt() == 80,
            "YALK BACKGROUND alone must render all 80 configured addresses");
    require(histogram->property("renderedChannelCount").toInt() == 80,
            "YALK histogram must not wait for MEASUREMENT events");

    orbita::stand::RunEvent overload;
    overload.nodeId = "yalk_overload_positive";
    overload.stage = "OVERLOAD";
    overload.data = {{"polarity", "+12 V"}, {"stressed_channel", "37"},
                     {"target_count", "88"}, {"impact_index", "37"},
                     {"impact_count", "176"}, {"settle_ms", "10000"}};
    page.setRunEvent(overload);
    for (int observed = 1; observed <= 88; ++observed) {
        if (observed == 37) continue;
        const double delta = observed == 30 ? 3.0 : observed == 20 ? 1.7
            : (observed % 5 - 2) * 0.35;
        orbita::stand::RunEvent measurement;
        measurement.nodeId = "yalk_overload_positive";
        measurement.stage = "MEASUREMENT";
        measurement.verdict = std::abs(delta) <= 2.0
            ? orbita::stand::RunVerdict::Ok : orbita::stand::RunVerdict::Fail;
        measurement.data = {{"polarity", "+12 V"}, {"stressed_channel", "37"},
            {"observed_channel", std::to_string(observed)},
            {"baseline_code", "1800"}, {"current_code", std::to_string(1800.0 + delta)},
            {"delta_code", std::to_string(delta)}, {"lower_delta_code", "-2"},
            {"upper_delta_code", "2"}};
        page.setRunEvent(measurement);
    }
    QApplication::processEvents();
    require(!overloadOverview->isHidden(), "overload must have its own visible workspace");
    require(overloadOverview->property("overloadMeasurementCount").toInt() == 87,
            "overload workspace must show every observed channel of the current impact");

    if (!screenshot.isEmpty() && scene == QStringLiteral("YALK_BACKGROUND")) {
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot), "cannot save YALK background screenshot");
    }
    if (!screenshot.isEmpty() && scene == QStringLiteral("YALK_OVERLOAD")) {
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot), "cannot save YALK overload screenshot");
    }

    page.setRunEvent(yalkStart);

    for (int address = 1; address <= 87; ++address) {
        if (!(address <= 28 || (address >= 32 && address <= 43)
                || (address >= 45 && address <= 70) || address >= 74)) continue;
        const double measured = 3.1 + (address % 9 - 4) * 0.003;
        const double spread = address % 11 == 0 ? 0.018 : 0.006;
        orbita::stand::RunEvent yalk;
        yalk.nodeId = "yalk_channels";
        yalk.stage = "MEASUREMENT";
        yalk.verdict = orbita::stand::RunVerdict::Ok;
        yalk.data = {{"ulk_address", std::to_string(address)}, {"command_v", "3.1"},
                     {"v7_v", "3.100"}, {"yalk_v", std::to_string(measured)},
                     {"lower_limit_v", "3.069"}, {"upper_limit_v", "3.131"},
                     {"signal", address % 7 == 0 ? "1" : "0"},
                     {"value_samples", address == 11 ? "3.050,3.100,3.112" : std::to_string(measured - spread) + ","
                         + std::to_string(measured) + "," + std::to_string(measured + spread)}};
        page.setRunEvent(yalk);
    }
    require(histogram->property("warningChannelCount").toInt() == 1,
            "one passing channel with individual samples outside limits must be marked as a UI warning");

    if (!screenshot.isEmpty() && scene == QStringLiteral("YALK")) {
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot), "cannot save YALK operator UI screenshot");
    }

    orbita::stand::RunEvent operatorEvent;
    operatorEvent.nodeId = "ytp_channels";
    operatorEvent.stage = "OPERATOR";
    operatorEvent.data = {{"target_resistance_ohm", "120"}, {"point_index", "2"},
                          {"point_count", "3"}};
    page.setRunEvent(operatorEvent);

    orbita::stand::RunEvent ytpBackground;
    ytpBackground.nodeId = "monitor";
    ytpBackground.stage = "BACKGROUND";
    ytpBackground.data = {{"section", "YTP"}};
    std::string ytpMean;
    std::string ytpMinimum;
    std::string ytpMaximum;
    for (int index = 0; index < 30; ++index) {
        if (index > 0) { ytpMean += ','; ytpMinimum += ','; ytpMaximum += ','; }
        const double mean = 120.0 + (index % 7 - 3) * 0.08;
        ytpMean += std::to_string(mean);
        ytpMinimum += std::to_string(mean - 0.12);
        ytpMaximum += std::to_string(mean + 0.12);
    }

    orbita::stand::RunEvent contactsStart;
    contactsStart.nodeId = "yalk_contacts";
    contactsStart.stage = "START";
    page.setRunEvent(contactsStart);
    for (double command : {0.0, 0.9, 2.5}) {
        for (int address = 1; address <= 87; ++address) {
            if (!(address <= 28 || (address >= 32 && address <= 43)
                    || (address >= 45 && address <= 70) || address >= 74)) continue;
            orbita::stand::RunEvent contact;
            contact.nodeId = "yalk_contacts";
            contact.stage = "MEASUREMENT";
            contact.verdict = orbita::stand::RunVerdict::Ok;
            contact.data = {{"ulk_address", std::to_string(address)},
                {"command_v", std::to_string(command)}, {"v7_v", std::to_string(command)},
                {"yalk_v", std::to_string(command)}, {"signal", command >= 2.0 ? "1" : "0"},
                {"lower_limit_v", std::to_string(command - .031)},
                {"upper_limit_v", std::to_string(command + .031)},
                {"value_samples", std::to_string(command)}};
            page.setRunEvent(contact);
        }
    }
    QApplication::processEvents();
    require(!contacts->isHidden(), "contact thresholds must have their own visible workspace");
    require(contacts->property("contactMeasurementCount").toInt() == 240,
            "contact workspace must retain all three threshold states for 80 channels");
    if (!screenshot.isEmpty() && scene == QStringLiteral("YALK_CONTACT")) {
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot), "cannot save YALK contact screenshot");
    }
    ytpBackground.data["background_mean"] = ytpMean;
    ytpBackground.data["background_min"] = ytpMinimum;
    ytpBackground.data["background_max"] = ytpMaximum;
    page.setRunEvent(ytpBackground);
    QApplication::processEvents();
    require(ytpHistogram->property("backgroundChannelCount").toInt() == 30,
            "YTP BACKGROUND alone must render all 30 channels");
    require(ytpHistogram->property("renderedChannelCount").toInt() == 30,
            "YTP histogram must not wait for MEASUREMENT events");
    if (!screenshot.isEmpty() && scene == QStringLiteral("YTP_BACKGROUND")) {
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot), "cannot save YTP background screenshot");
    }

    for (int channel = 1; channel <= 30; ++channel) {
        const double measured = 120.0 + (channel % 9 - 4) * 0.08;
        const double spread = channel % 8 == 0 ? 0.35 : 0.09;
        orbita::stand::RunEvent ytp;
        ytp.nodeId = "ytp_channels";
        ytp.stage = "MEASUREMENT";
        ytp.verdict = orbita::stand::RunVerdict::Ok;
        ytp.data = {{"ytp_channel", std::to_string(channel)},
                    {"actual_reference_ohm", "120.000"},
                    {"measured_resistance_ohm", std::to_string(measured)},
                    {"value_samples", std::to_string(measured - spread) + ","
                        + std::to_string(measured) + "," + std::to_string(measured + spread)}};
        page.setRunEvent(ytp);
    }

    if (!screenshot.isEmpty() && scene == QStringLiteral("YTP")) {
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot), "cannot save operator UI screenshot");
    }

    if (!screenshot.isEmpty() && scene == QStringLiteral("TU_YALK")) {
        page.setProductionMode(false);
        page.setScenarioInfo(QStringLiteral("ULK_COMBINED_CHECK"), true, false, {}, QString());
        serial->setText(QStringLiteral("УБСИ-0001"));
        enter->click();
        page.setRunInProgress(true, QStringLiteral("проверка по ТУ"));
        page.setRunEvent(supply);
        page.setRunEvent(yalkStart);
        page.setRunEvent(yalkBackground);
        orbita::stand::RunEvent yalk;
        yalk.nodeId = "yalk_channels";
        yalk.stage = "MEASUREMENT";
        yalk.verdict = orbita::stand::RunVerdict::Ok;
        yalk.data = {{"ulk_address", "87"}, {"command_v", "3.1"},
                     {"v7_v", "3.100"}, {"yalk_v", "3.106"},
                     {"lower_limit_v", "3.069"}, {"upper_limit_v", "3.131"},
                     {"signal", "0"}, {"value_samples", "3.097,3.106,3.109"}};
        page.setRunEvent(yalk);
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot), "cannot save TU YALK operator UI screenshot");
    }

    std::cout << "Unified production/TU operator navigation smoke test passed\n";
    return EXIT_SUCCESS;
}
