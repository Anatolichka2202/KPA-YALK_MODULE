#include "test_page.h"

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

    TestPage page;
    page.setProductionMode(false);
    page.setScenarioInfo(QStringLiteral("ULK_COMBINED_CHECK"), true, false, {}, QStringLiteral("ready"));

    auto* serial = page.findChild<QLineEdit*>(QStringLiteral("tuSerialInput"));
    auto* operatorName = page.findChild<QLineEdit*>(QStringLiteral("tuOperatorInput"));
    auto* check = buttonByText(page, QStringLiteral("ПРОВЕРИТЬ СТЕНД"));
    require(serial && operatorName && check, "minimal TU entry missing");

    serial->setText(QStringLiteral("345"));
    operatorName->setText(QStringLiteral("Толмачёв А.Е."));
    require(check->isEnabled(), "serial + operator must enable readiness");
    check->click();
    QApplication::processEvents();

    auto* start = buttonByText(page, QStringLiteral("НАЧАТЬ ПОЛНЫЙ ПРОГОН"));
    require(start && !start->isHidden() && start->isEnabled(),
            "minimal TU must reach ready state when no equipment is required in test");

    QString scenario;
    QString objectSerial;
    QObject::connect(&page, &TestPage::runRequested,
                     [&](const QString& code, const QString& serialValue, bool) {
        scenario = code;
        objectSerial = serialValue;
    });
    start->click();
    QApplication::processEvents();
    require(scenario == QStringLiteral("ULK_COMBINED_CHECK"),
            "minimal delivery must launch the complete TU scenario");
    require(objectSerial == QStringLiteral("345"),
            "minimal delivery lost UBSI serial at run start");

    page.setRunInProgress(true, QStringLiteral("running"));
    auto* tuSerial = page.findChild<QLabel*>(QStringLiteral("tuRuntimeSerial"));
    if (tuSerial) {
        require(tuSerial->text().contains(QStringLiteral("345"))
                    && tuSerial->text().contains(QStringLiteral("Толмачёв А.Е.")),
                "runtime must preserve serial and operator");
    }

    orbita::stand::ScenarioRunResult result;
    result.runId = "tu-minimal-test";
    result.verdict = orbita::stand::RunVerdict::Ok;
    page.setRunResult(result, QStringLiteral("C:/reports/tu.html"), {});
    QApplication::processEvents();

    auto* report = page.findChild<QLabel*>(QStringLiteral("finishReportPaths"));
    require(report && report->text().contains(QStringLiteral("tu.html"))
                && report->text().contains(QStringLiteral("tu-minimal-test")),
            "minimal TU result must expose report and run id");

    std::cout << "Minimal TU full-run handoff passed\n";
    return EXIT_SUCCESS;
}
