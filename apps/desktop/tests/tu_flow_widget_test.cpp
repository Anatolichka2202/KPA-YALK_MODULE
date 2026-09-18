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
    flow.setRegisteredSerials({QStringLiteral("345"), QStringLiteral("346")});
    // Kept for compatibility, but the v0.5 TU entry must not render/use it.
    flow.setOperators({QStringLiteral("Толмачёв А.Е."), QStringLiteral("Иванов И.И.")});

    QString requestedSerial;
    QString requestedOperator;
    QObject::connect(&flow, &TuFlowWidget::readinessRequested,
                     [&](const QString& serial, const QString& operatorName) {
        requestedSerial = serial;
        requestedOperator = operatorName;
    });

    auto* registry = flow.findChild<QComboBox*>(QStringLiteral("tuRegisteredProducts"));
    auto* manual = flow.findChild<QLineEdit*>(QStringLiteral("tuManualSerial"));
    auto* state = flow.findChild<QLabel*>(QStringLiteral("tuReadyState"));
    auto* serial = flow.findChild<QLabel*>(QStringLiteral("tuReadySerial"));
    require(registry && manual && state && serial, "TU v0.5 selection controls not found");
    require(flow.findChild<QComboBox*>(QStringLiteral("tuOperator")) == nullptr,
            "TU entry must not request operator before the run");
    require(registry->findData(QStringLiteral("345")) >= 0,
            "registered UBSI must be available in TU selector");

    auto* readiness = buttonByText(flow, QStringLiteral("Проверить стенд"));
    auto* useManual = buttonByText(flow, QStringLiteral("Использовать введённый SN"));
    require(readiness && useManual, "TU v0.5 actions not found");
    require(!readiness->isEnabled(), "readiness must require a serial number");

    // Registered product path.
    registry->setCurrentIndex(registry->findData(QStringLiteral("345")));
    QApplication::processEvents();
    require(readiness->isEnabled(), "registered serial must enable stand check without operator");
    readiness->click();
    require(requestedSerial == QStringLiteral("345"),
            "readiness action must use registered UBSI");
    require(requestedOperator.isEmpty(),
            "operator must not be captured before TU run");

    flow.beginStandCheck(QStringLiteral("345"), QString(),
                         {QStringLiteral("AKIP"), QStringLiteral("V7"),
                          QStringLiteral("R4831"), QStringLiteral("SCHEME")});
    require(state->text() == QStringLiteral("ПРОВЕРКА СТЕНДА…"),
            "explicit readiness action must enter stand-check state");
    flow.setEquipmentStatus(QStringLiteral("AKIP"), true, QStringLiteral("ready"));
    flow.setEquipmentStatus(QStringLiteral("V7"), true, QStringLiteral("ready"));
    QApplication::processEvents();

    require(serial->text() == QStringLiteral("УБСИ SN 345"),
            "ready screen must show serial only");
    require(state->text() == QStringLiteral("СТЕНД ГОТОВ"),
            "all required automatic equipment ready must produce STAND READY");
    auto* start = buttonByText(flow, QStringLiteral("НАЧАТЬ ПРОВЕРКУ"));
    require(start && !start->isHidden(), "ready screen must expose start button");

    // Manual product path from prototype v0.5.
    flow.resetToSelection();
    requestedSerial.clear();
    manual->setText(QStringLiteral("TU-778"));
    useManual->click();
    QApplication::processEvents();
    require(readiness->isEnabled(), "manual serial must enable stand check");
    readiness->click();
    require(requestedSerial == QStringLiteral("TU-778"),
            "manual TU serial must be passed to readiness");

    flow.beginStandCheck(QStringLiteral("TU-778"), QString(), {QStringLiteral("AKIP")});
    flow.setEquipmentStatus(QStringLiteral("AKIP"), false, QStringLiteral("нет связи"));
    QApplication::processEvents();
    require(state->text() == QStringLiteral("СТЕНД НЕ ГОТОВ"),
            "failed equipment check must not look ready");

    std::cout << "TU v0.5 serial-first readiness flow test passed\n";
    return EXIT_SUCCESS;
}
