#include "test_page.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
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
    auto* session = page.findChild<QTableWidget*>(QStringLiteral("productionSessionTable"));
    auto* equipment = page.findChild<QTableWidget*>(QStringLiteral("equipmentTable"));
    auto* histogram = page.findChild<QWidget*>(QStringLiteral("channelHistogram"));

    require(object && scope && test && mode && serial && operatorEdit && session
                && equipment && histogram,
            "new operator UI controls not found");
    require(page.styleSheet().contains(QStringLiteral("#14171c")),
            "operator UI must keep the dark industrial palette");

    page.setProductionMode(true);
    // KtmaMainWindow normally fills the hidden production selector. Reproduce
    // only that backend bridge here.
    scope->clear();
    scope->addItem(QStringLiteral("УБСИ · полная"), QStringLiteral("УБСИ ПО ТУ"));
    scope->addItem(QStringLiteral("ЯЛК-96"), QStringLiteral("ЯЛК-96"));
    scope->addItem(QStringLiteral("ЯТП"), QStringLiteral("ЯТП"));
    scope->addItem(QStringLiteral("ЯВП-8"), QStringLiteral("ЯВП-8"));
    test->clear();
    test->addItem(QStringLiteral("Полная производственная проверка УБСИ"),
                  QStringLiteral("PROD_FULL"));
    page.setScenarioInfo(QStringLiteral("PROD_FULL"), true, false,
        {QStringLiteral("AKIP"), QStringLiteral("RS485"), QStringLiteral("ISD"),
         QStringLiteral("V7"), QStringLiteral("R4831")}, QStringLiteral("ready"));

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

    orbita::stand::RunEvent yalk;
    yalk.nodeId = "yalk_channels";
    yalk.stage = "MEASUREMENT";
    yalk.verdict = orbita::stand::RunVerdict::Ok;
    yalk.data = {{"ulk_address", "1"}, {"command_v", "3.1"}, {"v7_v", "3.100"},
                 {"yalk_v", "3.097"}, {"signal", "1"},
                 {"value_samples", "3.096,3.097,3.098"}};
    page.setRunEvent(yalk);

    orbita::stand::RunEvent operatorEvent;
    operatorEvent.nodeId = "ytp_channels";
    operatorEvent.stage = "OPERATOR";
    operatorEvent.data = {{"target_resistance_ohm", "120"}, {"point_index", "2"},
                          {"point_count", "3"}};
    page.setRunEvent(operatorEvent);

    orbita::stand::RunEvent ytp;
    ytp.nodeId = "ytp_channels";
    ytp.stage = "MEASUREMENT";
    ytp.verdict = orbita::stand::RunVerdict::Ok;
    ytp.data = {{"ytp_channel", "1"}, {"actual_reference_ohm", "120.000"},
                {"measured_resistance_ohm", "120.08"},
                {"value_samples", "120.02,120.08,120.05"}};
    page.setRunEvent(ytp);

    std::cout << "New production/TU operator UI smoke test passed\n";
    return EXIT_SUCCESS;
}
