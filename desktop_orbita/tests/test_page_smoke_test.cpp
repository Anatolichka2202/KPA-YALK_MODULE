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
    require(object->count() == 1, "delivery UI must contain only ULK");
    require(scope->count() == 2, "delivery UI must contain only YTP and YALK");
    require(scope->currentData() == QStringLiteral("ЯТП"),
            "fixed 120-ohm YTP check must be initially selected");
    require(test->currentData() == QStringLiteral("YTP_120_CHECK"),
            "120-ohm YTP check must be the initial procedure");
    require(rowByName(equipment, QStringLiteral("АКИП")) < 0,
            "AKIP must not be present in the minimal delivery UI");
    require(rowByName(equipment, QStringLiteral("Rigol")) < 0,
            "Rigol must not be present in the minimal delivery UI");
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("Адаптер УЛК"))),
            "adapter status must always be visible");
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("ИСД"))),
            "ISD status must always be visible");

    page.setScenarioInfo(QStringLiteral("YTP_120_CHECK"), true, true,
        {QStringLiteral("RS485"), QStringLiteral("R4831")}, QStringLiteral("ready"));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("Р4831"))),
            "YTP must show the common manual resistance store");

    scope->setCurrentIndex(scope->findData(QStringLiteral("ЯЛК-96")));
    test->setCurrentIndex(test->findData(QStringLiteral("YALK_FULL_5_6")));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("В7-78/1"))),
            "YALK must show its reference voltmeter");
    require(equipment->isRowHidden(rowByName(equipment, QStringLiteral("Р4831"))),
            "YALK must hide the YTP resistance store");

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
    std::cout << "Test page BSI/UBSI selection smoke test passed\n";
    return EXIT_SUCCESS;
}
