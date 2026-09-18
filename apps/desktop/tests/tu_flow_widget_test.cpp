#include "tu_flow_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
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
    flow.setOperators({QStringLiteral("Толмачёв А.Е."), QStringLiteral("Иванов И.И.")});
    flow.setRegisteredSerials({QStringLiteral("345"), QStringLiteral("346")});

    QString requestedSerial;
    QString requestedOperator;
    QObject::connect(&flow, &TuFlowWidget::readinessRequested,
                     [&](const QString& serial, const QString& operatorName) {
        requestedSerial = serial;
        requestedOperator = operatorName;
    });

    auto* operators = flow.findChild<QComboBox*>(QStringLiteral("tuOperator"));
    auto* registry = flow.findChild<QComboBox*>(QStringLiteral("tuRegisteredProducts"));
    auto* check = flow.findChild<QPushButton*>();
    auto* state = flow.findChild<QLabel*>(QStringLiteral("tuReadyState"));
    auto* serial = flow.findChild<QLabel*>(QStringLiteral("tuReadySerial"));
    require(operators && registry && state && serial, "TU flow controls not found");
    require(flow.findChild<QWidget*>(QStringLiteral("tuManualSerial")) == nullptr,
            "TU entry must not expose arbitrary manual serial input");
    require(registry->findData(QStringLiteral("345")) >= 0,
            "registered UBSI must be available in TU selector");
    require(operators->findData(QStringLiteral("Толмачёв А.Е.")) >= 0,
            "saved operator must be available in TU selector");

    QPushButton* readiness = nullptr;
    for (auto* button : flow.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Проверить готовность")) readiness = button;
    }
    require(readiness, "explicit TU readiness button not found");
    require(!readiness->isEnabled(), "readiness must require both operator and registered UBSI");

    operators->setCurrentIndex(operators->findData(QStringLiteral("Толмачёв А.Е.")));
    registry->setCurrentIndex(registry->findData(QStringLiteral("345")));
    QApplication::processEvents();
    require(readiness->isEnabled(), "selected operator and registered UBSI must enable readiness check");
    require(requestedSerial.isEmpty(), "selecting TU product must not probe equipment automatically");

    readiness->click();
    require(requestedSerial == QStringLiteral("345"),
            "readiness action must use registered UBSI");
    require(requestedOperator == QStringLiteral("Толмачёв А.Е."),
            "readiness action must carry selected operator");

    flow.beginStandCheck(QStringLiteral("345"), QStringLiteral("Толмачёв А.Е."),
                         {QStringLiteral("AKIP"), QStringLiteral("V7"),
                          QStringLiteral("R4831"), QStringLiteral("SCHEME")});
    require(state->text() == QStringLiteral("ПРОВЕРКА СТЕНДА…"),
            "explicit readiness action must enter stand-check state");
    flow.setEquipmentStatus(QStringLiteral("AKIP"), true, QStringLiteral("ready"));
    flow.setEquipmentStatus(QStringLiteral("V7"), true, QStringLiteral("ready"));
    QApplication::processEvents();

    require(serial->text().contains(QStringLiteral("УБСИ SN 345")),
            "ready screen must show selected serial");
    require(serial->text().contains(QStringLiteral("Толмачёв А.Е.")),
            "ready screen must retain selected operator");
    require(state->text() == QStringLiteral("СТЕНД ГОТОВ"),
            "all required automatic equipment ready must produce STAND READY");

    QPushButton* start = nullptr;
    for (auto* button : flow.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("НАЧАТЬ ПРОВЕРКУ")) start = button;
    }
    require(start && !start->isHidden(), "ready screen must expose start button");

    flow.beginStandCheck(QStringLiteral("345"), QStringLiteral("Толмачёв А.Е."),
                         {QStringLiteral("AKIP")});
    flow.setEquipmentStatus(QStringLiteral("AKIP"), false, QStringLiteral("нет связи"));
    QApplication::processEvents();
    require(state->text() == QStringLiteral("СТЕНД НЕ ГОТОВ"),
            "failed equipment check must not look ready");

    std::cout << "TU explicit readiness flow test passed\n";
    return EXIT_SUCCESS;
}
