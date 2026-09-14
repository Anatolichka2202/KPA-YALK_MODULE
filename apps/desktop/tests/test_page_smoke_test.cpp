#include "test_page.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QTableWidget>

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
    TestPage page;
    const QString screenshot = qEnvironmentVariable("ORBITA_UI_SCREENSHOT");
    const QString scene = qEnvironmentVariable("MILTECH_UI_SCENE", QStringLiteral("YALK"));

    auto* object = page.findChild<QComboBox*>(QStringLiteral("testObject"));
    auto* scope = page.findChild<QComboBox*>(QStringLiteral("testScope"));
    auto* test = page.findChild<QComboBox*>(QStringLiteral("testType"));
    auto* mode = page.findChild<QComboBox*>(QStringLiteral("testMode"));
    auto* serial = page.findChild<QLineEdit*>(QStringLiteral("objectSerial"));
    auto* operatorEdit = page.findChild<QLineEdit*>(QStringLiteral("operatorName"));
    auto* operatorHistory = page.findChild<QComboBox*>(QStringLiteral("operatorHistory"));
    auto* session = page.findChild<QTableWidget*>(QStringLiteral("productionSessionTable"));
    auto* equipment = page.findChild<QTableWidget*>(QStringLiteral("equipmentTable"));
    auto* histogram = page.findChild<QWidget*>(QStringLiteral("yalkChannelHistogram"));
    auto* contacts = page.findChild<QWidget*>(QStringLiteral("yalkContactThresholdOverview"));
    auto* overloadOverview = page.findChild<QWidget*>(QStringLiteral("yalkOverloadOverview"));
    auto* ytpHistogram = page.findChild<QWidget*>(QStringLiteral("ytpChannelHistogram"));

    require(object && scope && test && mode && serial && operatorEdit && operatorHistory && session
                && equipment && histogram && contacts && overloadOverview && ytpHistogram,
            "new operator UI controls not found");
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

    // Feed real RunEvent shapes used by backend. The page must accept them
    // without a synthetic demo engine.
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
