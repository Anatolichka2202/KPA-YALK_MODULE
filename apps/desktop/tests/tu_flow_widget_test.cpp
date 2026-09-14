#include "tu_flow_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

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
    TuFlowWidget flow;
    flow.setRegisteredSerials({QStringLiteral("345"), QStringLiteral("346")});

    QString chosen;
    QObject::connect(&flow, &TuFlowWidget::serialChosen,
                     [&chosen](const QString& serial) { chosen = serial; });

    auto* registry = flow.findChild<QComboBox*>(QStringLiteral("tuRegisteredProducts"));
    auto* manual = flow.findChild<QLineEdit*>(QStringLiteral("tuManualSerial"));
    auto* state = flow.findChild<QLabel*>(QStringLiteral("tuReadyState"));
    auto* serial = flow.findChild<QLabel*>(QStringLiteral("tuReadySerial"));
    require(registry && manual && state && serial, "TU flow controls not found");
    require(registry->findData(QStringLiteral("345")) >= 0,
            "registered UBSI must be available in TU selector");

    manual->setText(QStringLiteral("TU-001"));
    QMetaObject::invokeMethod(manual, "returnPressed", Qt::DirectConnection);
    require(chosen == QStringLiteral("TU-001"), "manual SN must activate TU selection");

    flow.beginStandCheck(QStringLiteral("TU-001"),
                         {QStringLiteral("AKIP"), QStringLiteral("V7"),
                          QStringLiteral("R4831"), QStringLiteral("SCHEME")});
    require(state->text() == QStringLiteral("ПРОВЕРКА СТЕНДА…"),
            "stand check must start automatically after product selection");
    flow.setEquipmentStatus(QStringLiteral("AKIP"), true, QStringLiteral("ready"));
    flow.setEquipmentStatus(QStringLiteral("V7"), true, QStringLiteral("ready"));
    QApplication::processEvents();

    require(serial->text() == QStringLiteral("УБСИ SN TU-001"),
            "ready screen must show selected serial");
    require(state->text() == QStringLiteral("СТЕНД ГОТОВ"),
            "all required automatic equipment ready must produce STAND READY");
    auto* start = flow.findChild<QPushButton*>(QStringLiteral("primary"));
    require(start && start->isVisibleTo(&flow), "ready screen must expose start button");

    flow.beginStandCheck(QStringLiteral("TU-001"), {QStringLiteral("AKIP")});
    flow.setEquipmentStatus(QStringLiteral("AKIP"), false, QStringLiteral("нет связи"));
    QApplication::processEvents();
    require(state->text() == QStringLiteral("СТЕНД НЕ ГОТОВ"),
            "failed equipment check must not look ready");

    std::cout << "TU readiness flow test passed\n";
    return EXIT_SUCCESS;
}
