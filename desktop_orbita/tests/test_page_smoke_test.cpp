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
    require(object && scope && test && mode && equipment, "test page controls not found");
    require(object->count() == 2, "BSI and UBSI must both be selectable");

    object->setCurrentIndex(object->findData(QStringLiteral("BSI")));
    require(scope->count() == 8, "BSI must contain whole block plus seven cell types");
    require(scope->findData(QStringLiteral("ЯП-А")) >= 0, "BSI YPA cell missing");
    require(scope->findData(QStringLiteral("ЯФК")) >= 0, "BSI YFK cell missing");

    scope->setCurrentIndex(scope->findData(QStringLiteral("ЯЛК-96")));
    require(test->findData(QStringLiteral("YALK_ANALOG")) >= 0,
            "BSI YALK analog procedure missing");
    test->setCurrentIndex(test->findData(QStringLiteral("YALK_ANALOG")));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("E20-10"))),
            "BSI YALK must require Orbita/E20");
    require(equipment->isRowHidden(rowByName(equipment, QStringLiteral("Адаптер RS-485"))),
            "BSI YALK must not require the direct adapter");

    object->setCurrentIndex(object->findData(QStringLiteral("UBSI-7")));
    require(scope->count() == 5, "UBSI must contain whole block plus four cell types");
    scope->setCurrentIndex(scope->findData(QStringLiteral("ЯЛК-96")));
    test->setCurrentIndex(test->findData(QStringLiteral("YALK_ANALOG")));
    require(!equipment->isRowHidden(rowByName(equipment, QStringLiteral("Адаптер RS-485"))),
            "UBSI YALK must require the direct adapter");
    require(equipment->isRowHidden(rowByName(equipment, QStringLiteral("E20-10"))),
            "direct UBSI YALK procedure must not require E20");

    mode->setCurrentIndex(1);
    require(mode->currentData().isNull() || mode->currentIndex() == 1,
            "demonstration mode must be selectable");
    std::cout << "Test page BSI/UBSI selection smoke test passed\n";
    return EXIT_SUCCESS;
}
