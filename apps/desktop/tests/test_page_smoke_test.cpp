#include "test_page.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QTableWidget>

#include <cstdlib>
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

    auto* object = page.findChild<QComboBox*>(QStringLiteral("testObject"));
    auto* scope = page.findChild<QComboBox*>(QStringLiteral("testScope"));
    auto* test = page.findChild<QComboBox*>(QStringLiteral("testType"));
    auto* mode = page.findChild<QComboBox*>(QStringLiteral("testMode"));
    auto* serial = page.findChild<QLineEdit*>(QStringLiteral("objectSerial"));
    auto* operatorEdit = page.findChild<QLineEdit*>(QStringLiteral("operatorName"));
    auto* operatorHistory = page.findChild<QComboBox*>(QStringLiteral("operatorHistory"));
    auto* session = page.findChild<QTableWidget*>(QStringLiteral("productionSessionTable"));
    auto* equipment = page.findChild<QTableWidget*>(QStringLiteral("equipmentTable"));
    auto* histogram = page.findChild<QWidget*>(QStringLiteral("channelHistogram"));

    require(object && scope && test && mode && serial && operatorEdit && operatorHistory && session
                && equipment && histogram,
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
                     {"signal", address % 7 == 0 ? "1" : "0"},
                     {"value_samples", std::to_string(measured - spread) + ","
                        + std::to_string(measured) + "," + std::to_string(measured + spread)}};
        page.setRunEvent(yalk);
    }

    const QString screenshot = qEnvironmentVariable("ORBITA_UI_SCREENSHOT");
    const QString scene = qEnvironmentVariable("MILTECH_UI_SCENE", QStringLiteral("YALK"));
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

    std::cout << "Unified production/TU operator navigation smoke test passed\n";
    return EXIT_SUCCESS;
}
