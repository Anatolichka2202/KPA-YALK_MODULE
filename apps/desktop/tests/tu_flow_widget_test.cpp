#include "tu_flow_widget.h"

#include <QApplication>
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

QPushButton* buttonByText(QWidget& root, const QString& text)
{
    for (auto* button : root.findChildren<QPushButton*>())
        if (button->text() == text) return button;
    return nullptr;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TuFlowWidget flow;

    auto* serial = flow.findChild<QLineEdit*>(QStringLiteral("tuSerialInput"));
    auto* operatorName = flow.findChild<QLineEdit*>(QStringLiteral("tuOperatorInput"));
    auto* state = flow.findChild<QLabel*>(QStringLiteral("tuReadyState"));
    auto* readySerial = flow.findChild<QLabel*>(QStringLiteral("tuReadySerial"));
    auto* readyOperator = flow.findChild<QLabel*>(QStringLiteral("tuReadyOperator"));
    auto* check = buttonByText(flow, QStringLiteral("ПРОВЕРИТЬ СТЕНД"));
    require(serial && operatorName && state && readySerial && readyOperator && check,
            "minimal TU controls missing");
    require(!check->isEnabled(), "stand check must require serial and operator");

    QString requestedSerial;
    QString requestedOperator;
    QObject::connect(&flow, &TuFlowWidget::readinessRequested,
                     [&](const QString& s, const QString& op) {
        requestedSerial = s;
        requestedOperator = op;
    });

    serial->setText(QStringLiteral("345"));
    require(!check->isEnabled(), "operator must be required");
    operatorName->setText(QStringLiteral("Толмачёв А.Е."));
    require(check->isEnabled(), "serial + operator must enable stand check");
    check->click();
    require(requestedSerial == QStringLiteral("345")
                && requestedOperator == QStringLiteral("Толмачёв А.Е."),
            "minimal TU flow must preserve serial and operator");

    flow.beginStandCheck(requestedSerial, requestedOperator,
                         {QStringLiteral("AKIP"), QStringLiteral("V7"),
                          QStringLiteral("R4831"), QStringLiteral("SCHEME")});
    require(state->text() == QStringLiteral("ПРОВЕРКА СТЕНДА…"),
            "stand check state missing");
    require(readySerial->text() == QStringLiteral("УБСИ 345"),
            "ready page lost serial");
    require(readyOperator->text().contains(QStringLiteral("Толмачёв А.Е.")),
            "ready page lost operator");

    flow.setEquipmentStatus(QStringLiteral("AKIP"), true, QStringLiteral("ready"));
    flow.setEquipmentStatus(QStringLiteral("V7"), true, QStringLiteral("ready"));
    QApplication::processEvents();
    require(state->text() == QStringLiteral("СТЕНД ГОТОВ"),
            "automatic equipment ready must produce STAND READY");

    auto* start = buttonByText(flow, QStringLiteral("НАЧАТЬ ПОЛНЫЙ ПРОГОН"));
    require(start && !start->isHidden(), "ready state must expose full run action");

    std::cout << "Minimal TU serial/operator flow passed\n";
    return EXIT_SUCCESS;
}
