#include "test_page.h"

#include <QApplication>
#include <QComboBox>
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
    require(object->count() == 2, "BSI and UBSI must both be selectable");
    require(object->currentData() == QStringLiteral("UBSI-7"), "UBSI must be the initial object");
    require(scope->currentData() == QStringLiteral("BLOCK"),
            "the complete UBSI block must be the initial scope");
    require(test->currentData() == QStringLiteral("UBSI_NORMAL_5_6"),
            "the published UBSI TU 5.6 scenario must be initially selected");
    page.setScenarioInfo(QStringLiteral("UBSI_NORMAL_5_6"), true, false,
        {QStringLiteral("E20"), QStringLiteral("RS485"), QStringLiteral("ISD"),
         QStringLiteral("V7"), QStringLiteral("AKIP"), QStringLiteral("RIGOL"),
         QStringLiteral("SCOPE"), QStringLiteral("R4831"), QStringLiteral("SCHEME")},
        QStringLiteral("ready"));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("Адаптер RS-485"))),
            "initial UBSI YALK view must show the direct adapter");
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("АКИП-1160/6"))),
            "initial full-block view must show required power equipment");
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("E20-10"))),
            "full UBSI must verify its output telemetry through Orbita");

    object->setCurrentIndex(object->findData(QStringLiteral("BSI")));
    require(scope->count() == 8, "BSI must contain whole block plus seven cell types");
    require(scope->findData(QStringLiteral("ЯП-А")) >= 0, "BSI YPA cell missing");
    require(scope->findData(QStringLiteral("ЯФК")) >= 0, "BSI YFK cell missing");

    scope->setCurrentIndex(scope->findData(QStringLiteral("BLOCK")));
    require(test->currentData() == QStringLiteral("BSI_DIAGNOSTIC"),
            "BSI must expose an honest telemetry diagnostic, not a fake TU 5.6 run");
    page.setScenarioInfo(QStringLiteral("BSI_DIAGNOSTIC"), true, true,
                         {QStringLiteral("E20")}, QStringLiteral("ready"));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("E20-10"))),
            "BSI diagnostic must require Orbita/E20");

    scope->setCurrentIndex(scope->findData(QStringLiteral("ЯЛК-96")));
    require(test->findData(QStringLiteral("CELL_DIAGNOSTIC")) >= 0,
            "BSI cell diagnostic procedure missing");
    test->setCurrentIndex(test->findData(QStringLiteral("CELL_DIAGNOSTIC")));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("E20-10"))),
            "BSI YALK must require Orbita/E20");
    require(equipment->isRowHidden(rowByName(equipment, QStringLiteral("Адаптер RS-485"))),
            "BSI YALK must not require the direct adapter");

    object->setCurrentIndex(object->findData(QStringLiteral("UBSI-7")));
    require(scope->count() == 5, "UBSI must contain whole block plus four cell types");
    scope->setCurrentIndex(scope->findData(QStringLiteral("ЯЛК-96")));
    test->setCurrentIndex(test->findData(QStringLiteral("YALK_FULL_5_6")));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("Адаптер RS-485"))),
            "UBSI YALK must require the direct adapter");
    require(equipment->isRowHidden(rowByName(equipment, QStringLiteral("E20-10"))),
            "direct UBSI YALK procedure must not require E20");

    page.setScenarioInfo(QStringLiteral("YTP_FULL_5_6"), true, false,
        {QStringLiteral("RS485"), QStringLiteral("R4831"), QStringLiteral("SCHEME")},
        QStringLiteral("ready"));
    scope->setCurrentIndex(scope->findData(QStringLiteral("ЯТП")));
    require(test->findData(QStringLiteral("YTP_FULL_5_6")) >= 0,
            "UBSI must expose the standalone 30-channel YTP scenario");
    require(test->findData(QStringLiteral("YTP_120_CHECK")) >= 0,
            "UBSI must expose the fixed 120-ohm YTP check");
    test->setCurrentIndex(test->findData(QStringLiteral("YTP_FULL_5_6")));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("Адаптер RS-485")))
                && !equipment->isRowHidden(rowByName(equipment, QStringLiteral("Р4831"))),
            "YTP must show the adapter and manual resistance reference");
    require(equipment->isRowHidden(rowByName(equipment, QStringLiteral("E20-10"))),
            "YTP must not require Orbita/E20");

    scope->setCurrentIndex(scope->findData(QStringLiteral("BLOCK")));
    require(test->currentData() == QStringLiteral("UBSI_NORMAL_5_6"),
            "whole UBSI must select the TU 5.6 procedure");
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("E20-10"))),
            "whole UBSI must verify the output Orbita stream");
    const int adapterRow = rowByName(equipment, QStringLiteral("Адаптер RS-485"));
    const int schemeRow = rowByName(equipment, QStringLiteral("Кабели и оснастка"));
    require(adapterRow >= 0 && !equipment->isRowHidden(adapterRow),
            "whole UBSI must use the Ethernet RS-485 adapter");
    require(schemeRow >= 0 && !equipment->isRowHidden(schemeRow),
            "whole UBSI must require operator confirmation of scheme A.1");
    require(equipment->item(schemeRow, 3)->flags().testFlag(Qt::ItemIsUserCheckable),
            "scheme readiness must be confirmable by the operator");
    equipment->item(schemeRow, 3)->setCheckState(Qt::Checked);
    require(equipment->item(schemeRow, 3)->text() == QStringLiteral("ПОДТВЕРЖДЕНО"),
            "operator confirmation must update the visible status");

    object->setCurrentIndex(object->findData(QStringLiteral("BSI")));
    scope->setCurrentIndex(scope->findData(QStringLiteral("BLOCK")));
    require(equipment->isRowHidden(rowByName(equipment, QStringLiteral("Адаптер RS-485"))),
            "BSI telemetry diagnostic must not require the UBSI direct adapter");

    page.setEngineerMode(false);
    require(equipment->isColumnHidden(1) && equipment->isColumnHidden(4),
            "operator mode must hide transport and plugin diagnostics");
    page.setEngineerMode(true);
    require(!equipment->isColumnHidden(1) && !equipment->isColumnHidden(4),
            "engineer mode must expose equipment diagnostics");

    mode->setCurrentIndex(1);
    require(mode->currentData().isNull() || mode->currentIndex() == 1,
            "demonstration mode must be selectable");
    std::cout << "Test page BSI/UBSI selection smoke test passed\n";
    return EXIT_SUCCESS;
}
