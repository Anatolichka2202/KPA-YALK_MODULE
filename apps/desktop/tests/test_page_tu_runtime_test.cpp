#include "test_page.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
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
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestPage page;
    page.setProductionMode(false);
    page.setScenarioInfo(QStringLiteral("ULK_COMBINED_CHECK"), true, false, {}, QStringLiteral("ready"));
    page.setAvailableProductionProducts({QStringLiteral("345")});

    auto* operatorBox = page.findChild<QComboBox*>(QStringLiteral("tuOperator"));
    auto* serialBox = page.findChild<QComboBox*>(QStringLiteral("tuRegisteredProducts"));
    require(operatorBox && serialBox, "TU selection controls missing");
    operatorBox->addItem(QStringLiteral("Иванов И.И."), QStringLiteral("Иванов И.И."));
    operatorBox->setCurrentIndex(operatorBox->findData(QStringLiteral("Иванов И.И.")));
    serialBox->setCurrentIndex(serialBox->findData(QStringLiteral("345")));

    auto* check = buttonByText(page, QStringLiteral("Проверить готовность"));
    require(check && check->isEnabled(), "TU readiness action must be enabled for operator+serial");
    check->click();
    QApplication::processEvents();

    auto* start = buttonByText(page, QStringLiteral("НАЧАТЬ ПРОВЕРКУ"));
    // The TestPage is intentionally not shown in this offscreen unit test, so
    // QWidget::isVisible() would also include top-level visibility. isHidden()
    // tests the TU flow's own state instead.
    require(start && !start->isHidden() && start->isEnabled(),
            "TU must reach ready state when selected scenario needs no equipment in test");
    start->click();
    page.setRunInProgress(true, QStringLiteral("running"));
    QApplication::processEvents();

    auto* tuTitle = page.findChild<QLabel*>(QStringLiteral("tuRuntimeTitle"));
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

    std::cout << "Dedicated TU runtime rail test passed\n";
    return EXIT_SUCCESS;
}
