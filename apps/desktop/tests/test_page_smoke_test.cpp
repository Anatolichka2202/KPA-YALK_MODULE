#include "test_page.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
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

int rowByName(QTableWidget* table, const QString& name)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0) && table->item(row, 0)->text().contains(name)) return row;
    }
    return -1;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestPage page;

    auto* object = page.findChild<QComboBox*>(QStringLiteral("testObject"));
    auto* scope = page.findChild<QComboBox*>(QStringLiteral("testScope"));
    auto* test = page.findChild<QComboBox*>(QStringLiteral("testType"));
    auto* mode = page.findChild<QComboBox*>(QStringLiteral("testMode"));
    auto* equipment = page.findChild<QTableWidget*>(QStringLiteral("equipmentTable"));
    auto* summary = page.findChild<QTableWidget*>(QStringLiteral("cellSummaryTable"));
    auto* includeYvp = page.findChild<QCheckBox*>(QStringLiteral("includeYvp"));
    auto* histogram = page.findChild<QWidget*>(QStringLiteral("channelHistogram"));
    require(object && scope && test && mode && equipment && summary && includeYvp && histogram,
            "test page controls not found");
    require(!includeYvp->isChecked(), "YVP must be disabled by default in minimal delivery 2.0");
    require(equipment->columnCount() == 5,
            "equipment table must distinguish PC link, control type, status and diagnostics");
    require(object->count() == 1, "delivery UI must contain only ULK");
    require(scope->count() == 3, "delivery UI must contain YTP, YALK and combined mode");
    require(scope->currentData() == QStringLiteral("ЯТП"),
            "YTP must be initially selected");
    require(test->currentData() == QStringLiteral("YTP_FULL_5_6"),
            "full three-point YTP check must be the initial procedure");
    require(page.currentScenarioCode() == QStringLiteral("YTP_FULL_5_6"),
            "scenario editor must follow the selected delivery procedure");
    require(page.styleSheet().contains(QStringLiteral("background:#14171c"))
                && !page.styleSheet().contains(QStringLiteral("background:#f7f9fc")),
            "delivery test page must use the dark theme");
    require(rowByName(equipment, QStringLiteral("АКИП")) >= 0,
            "AKIP voltage/current status must be present in the TU delivery UI");
    require(rowByName(equipment, QStringLiteral("Rigol")) < 0,
            "Rigol must not be present in the minimal delivery UI");
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("Адаптер УЛК"))),
            "adapter status must always be visible");
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("ИСД"))),
            "ISD status must always be visible");

    page.setScenarioInfo(QStringLiteral("YTP_120_CHECK"), true, true,
        {QStringLiteral("RS485"), QStringLiteral("ISD"), QStringLiteral("R4831")},
        QStringLiteral("ready"));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("Р4831"))),
            "YTP must show the common manual resistance store");

    scope->setCurrentIndex(scope->findData(QStringLiteral("ЯЛК-96")));
    test->setCurrentIndex(test->findData(QStringLiteral("YALK_FULL_5_6")));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("В7-78/1"))),
            "YALK must show its reference voltmeter");
    require(equipment->isRowHidden(rowByName(equipment, QStringLiteral("Р4831"))),
            "YALK must hide the YTP resistance store");

    page.setScenarioInfo(QStringLiteral("ULK_COMBINED_CHECK"), true, false,
        {QStringLiteral("RS485"), QStringLiteral("ISD"),
         QStringLiteral("V7"), QStringLiteral("R4831")}, QStringLiteral("ready"));
    auto* fullTuCard = page.findChild<QPushButton*>(QStringLiteral("modeCard_УБСИ ПО ТУ"));
    require(fullTuCard, "full UBSI TU mode card not found");
    fullTuCard->click();
    require(scope->currentData() == QStringLiteral("УБСИ ПО ТУ"),
            "full UBSI TU card must select its scope");
    require(test->currentData() == QStringLiteral("ULK_COMBINED_CHECK"),
            "combined card must select the combined runtime scenario");
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("Р4831")))
                && !equipment->isRowHidden(rowByName(equipment, QStringLiteral("В7-78/1"))),
            "combined mode must expose both YALK and YTP equipment");

    page.setScenarioInfo(QStringLiteral("YTP_FULL_5_6"), true, false,
        {QStringLiteral("RS485"), QStringLiteral("R4831")},
        QStringLiteral("ready"));
    scope->setCurrentIndex(scope->findData(QStringLiteral("ЯТП")));
    require(test->findData(QStringLiteral("YTP_FULL_5_6")) >= 0,
            "UBSI must expose the standalone 30-channel YTP scenario");
    require(test->findData(QStringLiteral("YTP_120_CHECK")) >= 0,
            "UBSI must expose the fixed 120-ohm YTP check");
    test->setCurrentIndex(test->findData(QStringLiteral("YTP_FULL_5_6")));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("Адаптер УЛК")))
                && !equipment->isRowHidden(rowByName(equipment, QStringLiteral("Р4831"))),
            "YTP must show the adapter and manual resistance reference");

    page.setEngineerMode(false);
    require(equipment->isVisibleTo(&page),
            "operator must retain the equipment readiness table");
    require(equipment->isColumnHidden(1) && equipment->isColumnHidden(4),
            "operator mode must hide transport and plugin diagnostics");
    page.setEngineerMode(true);
    require(!equipment->isColumnHidden(1) && !equipment->isColumnHidden(4),
            "engineer mode must expose equipment diagnostics");

    mode->setCurrentIndex(1);
    require(mode->currentData().isNull() || mode->currentIndex() == 1,
            "demonstration mode must be selectable");
    if (const QString screenshot = qEnvironmentVariable("ORBITA_UI_SCREENSHOT");
        !screenshot.isEmpty()) {
        page.setProductionMode(qEnvironmentVariableIsSet("MILTECH_UI_PRODUCTION"));
        mode->setCurrentIndex(0);
        const QString scene = qEnvironmentVariable("MILTECH_UI_SCENE", QStringLiteral("YALK"));
        if (auto* serial = page.findChild<QLineEdit*>(QStringLiteral("objectSerial")))
            serial->setText(QStringLiteral("УБСИ-0147"));
        page.setEquipmentStatus(QStringLiteral("RS485"), true,
            QStringLiteral("ROKT / UDP 192.168.0.115:1113"));
        page.setEquipmentStatus(QStringLiteral("ISD"), true,
            QStringLiteral("HTTP 192.168.0.101"));
        page.setEquipmentStatus(QStringLiteral("V7"), true,
            QStringLiteral("Взаимодействие с прибором подтверждено"));
        page.setEquipmentStatus(QStringLiteral("AKIP"), true,
            QStringLiteral("27,0 В · выход включён"));
        page.setEngineerMode(false);
        orbita::stand::RunEvent start;
        start.stage = "START";
        if (scene == QStringLiteral("POWER")) {
            if (scope->findData(QStringLiteral("ПИТАНИЕ")) < 0)
                scope->addItem(QStringLiteral("Питание / потребление"), QStringLiteral("ПИТАНИЕ"));
            scope->setCurrentIndex(scope->findData(QStringLiteral("ПИТАНИЕ")));
            test->clear();
            test->addItem(QStringLiteral("Питание / потребление"), QStringLiteral("PROD_POWER"));
            QMetaObject::invokeMethod(&page, "updateSelectionSummary", Qt::DirectConnection);
            page.setRunInProgress(true, QStringLiteral("Выдержка 19 В · 02:15 / 05:00"));
            start.message = "Выдержка 19 В · контроль работоспособности";
            page.setRunEvent(start);
            for (int sample = 0; sample < 12; ++sample) {
                orbita::stand::RunEvent event;
                event.stage = "SUPPLY";
                event.verdict = orbita::stand::RunVerdict::Ok;
                event.data = {{"setpoint_v", "19"}, {"duration_s", "300"},
                              {"elapsed_s", std::to_string(124 + sample)},
                              {"amperes", std::to_string(0.278 + (sample % 4) * 0.001)}};
                page.setRunEvent(event);
            }
        } else if (scene == QStringLiteral("YTP")) {
            scope->setCurrentIndex(scope->findData(QStringLiteral("ЯТП")));
            test->setCurrentIndex(test->findData(QStringLiteral("YTP_FULL_5_6")));
            page.setRunInProgress(true, QStringLiteral("ЯТП · точка 120 Ом"));
            start.message = "ЯТП · 30 каналов · точка 120 Ом";
            page.setRunEvent(start);
            for (int channel = 1; channel <= 30; ++channel) {
                const double measured = 120.0 + (channel % 7 - 3) * 0.07;
                const double span = channel % 11 == 0 ? 0.22 : 0.06;
                orbita::stand::RunEvent event;
                event.stage = "MEASUREMENT";
                event.verdict = orbita::stand::RunVerdict::Ok;
                event.data = {
                    {"ytp_channel", std::to_string(channel)},
                    {"actual_reference_ohm", "120.000"},
                    {"measured_resistance_ohm", std::to_string(measured)},
                    {"raw", std::to_string(2160 + channel)},
                    {"value_samples", std::to_string(measured - span / 2.0) + ","
                        + std::to_string(measured) + ","
                        + std::to_string(measured + span / 2.0)}};
                page.setRunEvent(event);
            }
        } else {
            scope->setCurrentIndex(scope->findData(QStringLiteral("ЯЛК-96")));
            test->setCurrentIndex(test->findData(QStringLiteral("YALK_FULL_5_6")));
            page.setRunInProgress(true, QStringLiteral("ЯЛК-96 · точка 6,2 В"));
            start.message = "ЯЛК-96 · 80 аналоговых каналов";
            page.setRunEvent(start);
            for (int address = 1; address <= 87; ++address) {
                if (!(address <= 28 || (address >= 32 && address <= 43)
                        || (address >= 45 && address <= 70) || address >= 74)) continue;
                const double measured = 6.2 + (address % 7 - 3) * 0.0012;
                const double span = address % 13 == 0 ? 0.006 : 0.0014;
                orbita::stand::RunEvent event;
                event.stage = "MEASUREMENT";
                event.verdict = orbita::stand::RunVerdict::Ok;
                event.data = {
                    {"ulk_address", std::to_string(address)}, {"command_v", "6.2"},
                    {"v7_v", "6.2000"}, {"yalk_v", std::to_string(measured)},
                    {"analog_code", std::to_string(4010 + address)},
                    {"signal", address % 9 == 0 ? "1" : "0"},
                    {"value_samples", std::to_string(measured - span / 2.0) + ","
                        + std::to_string(measured - span / 4.0) + ","
                        + std::to_string(measured) + ","
                        + std::to_string(measured + span / 3.0) + ","
                        + std::to_string(measured + span / 2.0)}};
                page.setRunEvent(event);
            }
        }
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot), "cannot save UI screenshot");
    }
    std::cout << "Test page BSI/UBSI selection smoke test passed\n";
    return EXIT_SUCCESS;
}
