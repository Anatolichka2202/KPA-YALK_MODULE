#include "test_page.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

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

void saveScene(TestPage& page, const QString& requested,
               const QString& scene, const QString& base)
{
    if (base.isEmpty() || requested != scene) return;
    for (const QSize size : {QSize(1920, 1080), QSize(1600, 900)}) {
        page.resize(size);
        page.show();
        QApplication::processEvents();
        const QString suffix = QStringLiteral("_%1x%2.png")
                                   .arg(size.width()).arg(size.height());
        require(page.grab().save(base + suffix), "cannot save TU acceptance screenshot");
    }
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const QString requested = qEnvironmentVariable("MILTECH_UI_SCENE");
    const QString screenshot = qEnvironmentVariable("ORBITA_UI_SCREENSHOT");

    TestPage page;
    page.setProductionMode(false);
    page.setScenarioInfo(QStringLiteral("ULK_COMBINED_CHECK"), true, false, {}, QStringLiteral("ready"));
    page.setAvailableProductionProducts({QStringLiteral("345")});

    auto* serialBox = page.findChild<QComboBox*>(QStringLiteral("tuRegisteredProducts"));
    auto* manualSerial = page.findChild<QLineEdit*>(QStringLiteral("tuManualSerial"));
    require(serialBox && manualSerial, "TU v0.5 serial controls missing");
    require(page.findChild<QComboBox*>(QStringLiteral("tuOperator")) == nullptr,
            "TU entry must not ask for operator before the run");

    serialBox->setCurrentIndex(serialBox->findData(QStringLiteral("345")));
    QApplication::processEvents();
    saveScene(page, requested, QStringLiteral("TU_ENTRY"), screenshot);

    auto* check = buttonByText(page, QStringLiteral("Проверить стенд"));
    require(check && check->isEnabled(), "TU readiness action must be enabled for selected serial");
    check->click();
    QApplication::processEvents();
    saveScene(page, requested, QStringLiteral("TU_READY"), screenshot);

    auto* start = buttonByText(page, QStringLiteral("НАЧАТЬ ПРОВЕРКУ"));
    require(start && !start->isHidden() && start->isEnabled(),
            "TU must reach ready state when selected scenario needs no equipment in test");
    start->click();
    page.setRunInProgress(true, QStringLiteral("running"));
    QApplication::processEvents();
    saveScene(page, requested, QStringLiteral("TU_RUNTIME"), screenshot);

    auto* tuTitle = page.findChild<QLabel*>(QStringLiteral("tuRuntimeTitle"));
    auto* productionFooter = page.findChild<QWidget*>(QStringLiteral("productionTelemetryFooter"));
    auto* readiness = page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.13"));
    auto* supply = page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.3"));
    auto* current = page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.5"));
    auto* yalkOpen = page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.10"));
    auto* yalkOverload = page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.11"));
    auto* reference = page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.9"));
    auto* functional = page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.1"));
    auto* accuracy = page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.14"));
    auto* yvpAfc = page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.7"));
    auto* yvpGain = page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.8"));
    require(tuTitle && readiness && supply && current && yalkOpen && yalkOverload
                && reference && functional && accuracy && yvpAfc && yvpGain,
            "automated TU requirement rail is incomplete");
    require(productionFooter && productionFooter->isHidden(),
            "production telemetry footer must not be shown in the simple TU runtime");

    require(!page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.2")),
            "non-automated 1.1.4.2 must not appear as a pending TU check");
    require(!page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.4")),
            "1.1.4.4 must not appear in the current automated TU route");
    require(!page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.6")),
            "1.1.4.6 must not appear in the current automated TU route");
    require(!page.findChild<QWidget*>(QStringLiteral("tuRequirement_1.1.4.12")),
            "1.1.4.12 must not appear in the current automated TU route");

    orbita::stand::RunEvent ready;
    ready.nodeId = "readiness";
    ready.stage = "MEASUREMENT";
    ready.verdict = orbita::stand::RunVerdict::Ok;
    ready.data = {{"seconds", "2.4"}};
    page.setRunEvent(ready);
    QApplication::processEvents();
    auto* readyStatus = page.findChild<QLabel*>(QStringLiteral("tuRequirementStatus_1.1.4.13"));
    require(readyStatus && readyStatus->text() == QStringLiteral("ВЫПОЛНЯЕТСЯ"),
            "readiness event must activate 1.1.4.13 in TU rail");

    orbita::stand::RunEvent yvp;
    yvp.nodeId = "yvp_channels";
    yvp.stage = "YVP_V7_POINT";
    yvp.verdict = orbita::stand::RunVerdict::NotRun;
    yvp.data = {{"yvp_channel", "2"}, {"gain_mv_per_pc", "4"},
                {"set_frequency_hz", "500"}, {"rigol_input_vpp", "0.5"},
                {"v7_output_vrms", "0.707"}, {"calculated_gain_mv_per_pc", "4.0"},
                {"acceptance", "evaluated_after_gain_sweep"}};
    page.setRunEvent(yvp);
    QApplication::processEvents();
    auto* afcStatus = page.findChild<QLabel*>(QStringLiteral("tuRequirementStatus_1.1.4.7"));
    auto* gainStatus = page.findChild<QLabel*>(QStringLiteral("tuRequirementStatus_1.1.4.8"));
    require(afcStatus && gainStatus
                && afcStatus->text() == QStringLiteral("ВЫПОЛНЯЕТСЯ")
                && gainStatus->text() == QStringLiteral("ВЫПОЛНЯЕТСЯ"),
            "YVP event must activate AFC and gain TU requirements");

    orbita::stand::ScenarioRunResult result;
    result.runId = "tu-ui-test";
    result.verdict = orbita::stand::RunVerdict::Fail;
    auto add = [&result](const char* id, const char* tu, orbita::stand::RunVerdict verdict) {
        orbita::stand::StepRunResult step;
        step.nodeId = id;
        step.tuRequirement = tu;
        step.verdict = verdict;
        result.steps.push_back(std::move(step));
    };
    add("readiness", "1.1.4.13", orbita::stand::RunVerdict::Ok);
    add("supply_range", "1.1.4.3, 1.1.4.5", orbita::stand::RunVerdict::Ok);
    add("yalk_initial", "1.1.4.10", orbita::stand::RunVerdict::Ok);
    add("yalk_overload", "1.1.4.11", orbita::stand::RunVerdict::Ok);
    add("yalk_reference_voltage", "1.1.4.9", orbita::stand::RunVerdict::Ok);
    add("yalk_channels", "1.1.4.1, 1.1.4.14", orbita::stand::RunVerdict::Ok);
    add("ytp_channels", "1.1.4.1, 1.1.4.14", orbita::stand::RunVerdict::Ok);
    add("yvp_channels", "1.1.4.1, 1.1.4.7, 1.1.4.8, 1.1.4.14", orbita::stand::RunVerdict::Fail);
    page.setRunResult(result, QStringLiteral("C:/reports/tu.html"), {});
    QApplication::processEvents();

    readyStatus = page.findChild<QLabel*>(QStringLiteral("tuRequirementStatus_1.1.4.13"));
    afcStatus = page.findChild<QLabel*>(QStringLiteral("tuRequirementStatus_1.1.4.7"));
    gainStatus = page.findChild<QLabel*>(QStringLiteral("tuRequirementStatus_1.1.4.8"));
    require(readyStatus && readyStatus->text() == QStringLiteral("НОРМА"),
            "TU result must finalize readiness as NORM");
    require(afcStatus && afcStatus->text() == QStringLiteral("НЕ НОРМА"),
            "TU result must preserve YVP AFC failure");
    require(gainStatus && gainStatus->text() == QStringLiteral("НЕ НОРМА"),
            "TU result must preserve YVP gain failure");

    auto* report = page.findChild<QLabel*>(QStringLiteral("finishReportPaths"));
    require(report && report->text().contains(QStringLiteral("tu.html"))
                && report->text().contains(QStringLiteral("tu-ui-test")),
            "TU finish must expose report path and run_id");
    saveScene(page, requested, QStringLiteral("TU_FINISH"), screenshot);

    std::cout << "Dedicated TU v0.5 runtime rail test passed\n";
    return EXIT_SUCCESS;
}
