#include "test_page.h"

#include <QApplication>
#include <QComboBox>
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
    require(object && scope && test && mode && equipment && summary, "test page controls not found");
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
    scope->setCurrentIndex(scope->findData(QStringLiteral("УЛК+ЯТП")));
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
        mode->setCurrentIndex(0);
        scope->setCurrentIndex(scope->findData(QStringLiteral("ЯТП")));
        test->setCurrentIndex(test->findData(QStringLiteral("YTP_FULL_5_6")));
        page.setEquipmentStatus(QStringLiteral("RS485"), true,
            QStringLiteral("ROKT / UDP 192.168.0.115:1113"));
        page.setEquipmentStatus(QStringLiteral("ISD"), true,
            QStringLiteral("HTTP 192.168.0.101"));
        page.setEngineerMode(false);
        orbita::stand::ScenarioRunResult preview;
        preview.runId = "preview";
        preview.scenarioId = "ubsi.468157.002.ytp.tu5_6";
        preview.verdict = orbita::stand::RunVerdict::Ok;
        orbita::stand::StepRunResult channels;
        channels.title = "Проверка 30 каналов ЯТП";
        channels.verdict = orbita::stand::RunVerdict::Ok;
        for (int channel = 1; channel <= 30; ++channel) {
            for (const double point : {0.0, 120.0, 240.0}) {
                const double measured = point + (channel % 5 - 2) * 0.08;
                orbita::stand::MeasurementResult value;
                value.title = "ЯТП канал " + std::to_string(channel);
                value.reference = point;
                value.measured = measured;
                value.unit = "Ом";
                value.verdict = orbita::stand::RunVerdict::Ok;
                value.attributes = {
                    {"ytp_channel", std::to_string(channel)},
                    {"target_resistance_ohm", std::to_string(point)},
                    {"actual_reference_ohm", std::to_string(point)},
                    {"raw", std::to_string(330 + int(point / 240.0 * 3670))},
                    {"calibration_zero_raw", "330"}, {"calibration_full_raw", "4000"},
                    {"measured_resistance_ohm", std::to_string(measured)},
                    {"absolute_error_ohm", std::to_string(std::abs(measured - point))},
                    {"reduced_error_percent", std::to_string((measured - point) / 2.4)},
                    {"sample_count", "16"}, {"temperature_mode", "0"},
                    {"value_samples", std::to_string(measured - 0.03) + ","
                        + std::to_string(measured) + "," + std::to_string(measured + 0.03)}};
                channels.measurements.push_back(std::move(value));
            }
        }
        preview.steps.push_back(std::move(channels));
        page.setRunResult(preview);
        page.resize(1664, 935);
        page.show();
        QApplication::processEvents();
        require(page.grab().save(screenshot), "cannot save UI screenshot");
    }
    std::cout << "Test page BSI/UBSI selection smoke test passed\n";
    return EXIT_SUCCESS;
}
