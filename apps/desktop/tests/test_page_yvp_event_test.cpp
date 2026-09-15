#include "test_page.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
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
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestPage page;
    page.setProductionMode(true);

    auto* scope = page.findChild<QComboBox*>(QStringLiteral("testScope"));
    auto* test = page.findChild<QComboBox*>(QStringLiteral("testType"));
    auto* overview = page.findChild<QWidget*>(QStringLiteral("yvpEightChannelOverview"));
    auto* context = page.findChild<QLabel*>(QStringLiteral("frozenProcedureContext"));
    require(scope && test && overview && context, "production YVP controls not found");

    const int yvpScope = scope->findData(QStringLiteral("ЯВП-8"));
    require(yvpScope >= 0, "YVP production scope not found");
    scope->setCurrentIndex(yvpScope);

    require(page.currentScenarioCode() == QStringLiteral("PROD_YVP"),
            "YVP scope must resolve to PROD_YVP");
    require(test->currentText().contains(QStringLiteral("V7 / ИСД")),
            "YVP operator title must describe the current V7/ISD tract");

    int includedRouteStages = 0;
    bool powerIncluded = false;
    bool yvpIncluded = false;
    for (auto* label : page.findChildren<QLabel*>()) {
        if (!label->property("routeStageIndex").isValid()) continue;
        if (!label->property("includedInRoute").toBool()) continue;
        ++includedRouteStages;
        const int index = label->property("routeStageIndex").toInt();
        if (index == 1) powerIncluded = true;
        if (index == 4) yvpIncluded = true;
    }
    require(includedRouteStages == 4,
            "YVP backend route must remain Preparation -> Power -> YVP -> Finish");
    require(powerIncluded && yvpIncluded,
            "YVP backend route must retain supply_status and YVP stages");

    page.setRunInProgress(true, QStringLiteral("YVP V7/ISD"));

    orbita::stand::RunEvent start;
    start.nodeId = "yvp_measurement";
    start.stage = "START";
    start.message = "V7 / ИСД";
    page.setRunEvent(start);

    orbita::stand::RunEvent point;
    point.nodeId = "yvp_measurement";
    point.stage = "YVP_V7_POINT";
    point.verdict = orbita::stand::RunVerdict::NotRun;
    point.data = {
        {"backend", "v7_isd"},
        {"yvp_channel", "3"},
        {"gain_mv_per_pc", "0.5"},
        {"set_frequency_hz", "500"},
        {"measured_frequency_hz", "499.8"},
        {"frequency_verification", "v7"},
        {"rigol_input_vpp", "4"},
        {"v7_output_vrms", "0.0679"},
        {"v7_output_vpp", "0.192"},
        {"capacitance_pf", "1000"},
        {"charge_pc_from_commanded_vpp", "4000"},
        {"calculated_gain_mv_per_pc", "0.48"},
        {"acceptance", "not_applied"}
    };
    page.setRunEvent(point);
    QApplication::processEvents();

    bool channelShown = false;
    bool gainShown = false;
    bool calculatedShown = false;
    bool noAcceptanceShown = false;
    for (auto* label : page.findChildren<QLabel*>()) {
        const QString text = label->text();
        channelShown = channelShown || text == QStringLiteral("3 / 8");
        gainShown = gainShown || text.contains(QStringLiteral("0.5 мВ/пКл"));
        calculatedShown = calculatedShown || text.contains(QStringLiteral("0.48 мВ/пКл"));
        noAcceptanceShown = noAcceptanceShown
            || text.contains(QStringLiteral("критерий приёмки не применён"));
    }

    require(channelShown, "YVP channel from YVP_V7_POINT was not rendered");
    require(gainShown, "YVP requested gain from YVP_V7_POINT was not rendered");
    require(calculatedShown, "YVP calculated gain from YVP_V7_POINT was not rendered");
    require(noAcceptanceShown, "YVP no-acceptance state must be shown explicitly");
    require(context->text().contains(QStringLiteral("Канал 3 / 8 · Kу 0.5 · 500 Гц")),
            "YVP contextual left side did not advance from V7/ISD event");
    require(overview->property("yvpRenderedChannelCount").toInt() == 1,
            "YVP current-point plane must retain the first measured channel");
    require(overview->property("yvpCompletedPointCount").toInt() == 1,
            "YVP 8x7x7 matrix must count the first completed point");

    std::cout << "YVP V7/ISD operator matrix test passed\n";
    return EXIT_SUCCESS;
}
