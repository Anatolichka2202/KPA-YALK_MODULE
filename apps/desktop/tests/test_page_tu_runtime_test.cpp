#include "test_page.h"
#include "tu_flow_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QWidget>

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <sstream>

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

bool hasLabelText(QWidget& root, const QString& text)
{
    for (auto* label : root.findChildren<QLabel*>())
        if (label->text() == text) return true;
    return false;
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
    require(page.findChild<QLineEdit*>(QStringLiteral("tuCompletionOperator"))->isHidden(),
            "post-run operator must be hidden before the run");

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

    auto* powerOverview = page.findChild<QWidget*>(QStringLiteral("powerYalkOverview"));
    auto* yalkOverview = page.findChild<QWidget*>(QStringLiteral("yalkChannelHistogram"));
    auto* contactOverview = page.findChild<QWidget*>(QStringLiteral("yalkContactThresholdOverview"));
    auto* overloadOverview = page.findChild<QWidget*>(QStringLiteral("yalkOverloadOverview"));
    auto* ytpOverview = page.findChild<QWidget*>(QStringLiteral("ytpChannelHistogram"));
    auto* yvpOverview = page.findChild<QWidget*>(QStringLiteral("yvpEightChannelOverview"));
    require(powerOverview && yalkOverview && contactOverview && overloadOverview
                && ytpOverview && yvpOverview,
            "TU runtime must expose every measurement view from the approved route");

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

    orbita::stand::RunEvent power;
    power.nodeId = "supply_range";
    power.stage = "SUPPLY";
    power.data = {{"setpoint_v", "27"}, {"volts", "27.01"}, {"amperes", "0.238"},
                  {"elapsed_s", "10"}, {"duration_s", "300"}};
    page.setRunEvent(power);
    QApplication::processEvents();
    saveScene(page, requested, QStringLiteral("TU_POWER"), screenshot);

    std::ostringstream background;
    for (int channel = 0; channel < 100; ++channel) {
        if (channel) background << ',';
        background << 3.1 + (channel % 9 - 4) * .003;
    }
    orbita::stand::RunEvent backgroundEvent;
    backgroundEvent.nodeId = "monitor";
    backgroundEvent.stage = "BACKGROUND";
    backgroundEvent.data = {{"section", "YALK"},
                            {"background_mean", background.str()},
                            {"background_min", background.str()},
                            {"background_max", background.str()}};
    page.setRunEvent(backgroundEvent);

    for (int address = 1; address <= 87; ++address) {
        if (!(address <= 28 || (address >= 32 && address <= 43)
              || (address >= 45 && address <= 70) || address >= 74)) continue;
        const double measured = 3.1 + (address % 9 - 4) * .003;
        orbita::stand::RunEvent event;
        event.nodeId = "yalk_channels";
        event.stage = "MEASUREMENT";
        event.verdict = orbita::stand::RunVerdict::Ok;
        event.data = {{"ulk_address", std::to_string(address)}, {"command_v", "3.1"},
                      {"v7_v", "3.100"}, {"yalk_v", std::to_string(measured)},
                      {"lower_limit_v", "3.069"}, {"upper_limit_v", "3.131"},
                      {"signal", "0"}, {"value_samples", std::to_string(measured)}};
        page.setRunEvent(event);
    }
    QApplication::processEvents();
    require(yalkOverview->property("renderedChannelCount").toInt() == 80,
            "TU YALK view must retain all 80 channels");
    saveScene(page, requested, QStringLiteral("TU_YALK"), screenshot);

    for (double command : {0.0, 0.9, 2.5}) {
        for (int address = 1; address <= 87; ++address) {
            if (!(address <= 28 || (address >= 32 && address <= 43)
                  || (address >= 45 && address <= 70) || address >= 74)) continue;
            orbita::stand::RunEvent event;
            event.nodeId = "yalk_contact_thresholds";
            event.stage = "MEASUREMENT";
            event.verdict = orbita::stand::RunVerdict::Ok;
            event.data = {{"ulk_address", std::to_string(address)},
                          {"command_v", std::to_string(command)},
                          {"v7_v", std::to_string(command)},
                          {"yalk_v", std::to_string(command)},
                          {"signal", command >= 2.0 ? "1" : "0"},
                          {"value_samples", std::to_string(command)}};
            page.setRunEvent(event);
        }
    }
    QApplication::processEvents();
    require(contactOverview->property("contactMeasurementCount").toInt() == 240,
            "TU contact view must retain all 240 threshold measurements");
    saveScene(page, requested, QStringLiteral("TU_CONTACT"), screenshot);

    orbita::stand::RunEvent overloadStart;
    overloadStart.nodeId = "yalk_overload";
    overloadStart.stage = "OVERLOAD";
    overloadStart.data = {{"polarity", "+12 V"}, {"stressed_channel", "37"},
                          {"impact_index", "37"}, {"impact_count", "176"},
                          {"settle_ms", "10000"}};
    page.setRunEvent(overloadStart);
    for (int observed = 1; observed <= 88; ++observed) {
        if (observed == 37) continue;
        const double baseline = 1800 + (observed % 13 - 6) * .8;
        const double delta = observed == 30 ? 3.0 : (observed % 5 - 2) * .35;
        orbita::stand::RunEvent event;
        event.nodeId = "yalk_overload";
        event.stage = "MEASUREMENT";
        event.verdict = std::abs(delta) <= 2
            ? orbita::stand::RunVerdict::Ok : orbita::stand::RunVerdict::Fail;
        event.data = {{"polarity", "+12 V"}, {"stressed_channel", "37"},
                      {"observed_channel", std::to_string(observed)},
                      {"baseline_code", std::to_string(baseline)},
                      {"current_code", std::to_string(baseline + delta)},
                      {"delta_code", std::to_string(delta)},
                      {"lower_delta_code", "-2"}, {"upper_delta_code", "2"}};
        page.setRunEvent(event);
    }
    QApplication::processEvents();
    require(overloadOverview->property("overloadMeasurementCount").toInt() == 87,
            "TU overload view must retain all observed channels");
    saveScene(page, requested, QStringLiteral("TU_OVERLOAD"), screenshot);

    orbita::stand::RunEvent ytpPrompt;
    ytpPrompt.nodeId = "ytp_channels";
    ytpPrompt.stage = "OPERATOR";
    ytpPrompt.data = {{"target_resistance_ohm", "120"}, {"point_index", "2"},
                      {"point_count", "3"}};
    page.setRunEvent(ytpPrompt);
    for (int channel = 1; channel <= 30; ++channel) {
        const double measured = 120 + (channel % 9 - 4) * .08;
        orbita::stand::RunEvent event;
        event.nodeId = "ytp_channels";
        event.stage = "MEASUREMENT";
        event.verdict = orbita::stand::RunVerdict::Ok;
        event.data = {{"ytp_channel", std::to_string(channel)},
                      {"actual_reference_ohm", "120.000"},
                      {"measured_resistance_ohm", std::to_string(measured)},
                      {"value_samples", std::to_string(measured)}};
        page.setRunEvent(event);
    }
    QApplication::processEvents();
    require(ytpOverview->property("renderedChannelCount").toInt() == 30,
            "TU YTP view must retain all 30 channels");
    saveScene(page, requested, QStringLiteral("TU_YTP"), screenshot);

    for (int channel = 1; channel <= 8; ++channel) {
        orbita::stand::RunEvent yvp;
        yvp.nodeId = "yvp_channels";
        yvp.stage = "YVP_V7_POINT";
        yvp.verdict = orbita::stand::RunVerdict::NotRun;
        yvp.data = {{"yvp_channel", std::to_string(channel)}, {"gain_mv_per_pc", "4"},
                    {"set_frequency_hz", "500"}, {"rigol_input_vpp", "0.5"},
                    {"v7_output_vrms", std::to_string(.141 + channel * .001)},
                    {"calculated_gain_mv_per_pc", std::to_string(3.96 + channel * .005)},
                    {"acceptance", "evaluated_after_gain_sweep"}};
        page.setRunEvent(yvp);
    }
    QApplication::processEvents();
    require(yvpOverview->property("yvpRenderedChannelCount").toInt() == 8,
            "TU YVP view must retain all eight channels");
    saveScene(page, requested, QStringLiteral("TU_YVP"), screenshot);
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
    add("yalk_contact_thresholds", "1.1.4.1", orbita::stand::RunVerdict::Ok);
    add("yalk_overload", "1.1.4.11", orbita::stand::RunVerdict::Ok);
    add("yalk_reference_voltage", "1.1.4.9", orbita::stand::RunVerdict::Ok);
    add("yalk_channels", "1.1.4.1, 1.1.4.14", orbita::stand::RunVerdict::Ok);
    add("ytp_channels", "1.1.4.1, 1.1.4.14", orbita::stand::RunVerdict::Ok);
    add("yvp_channels", "1.1.4.1, 1.1.4.7, 1.1.4.8, 1.1.4.14", orbita::stand::RunVerdict::Fail);

    QTemporaryDir reportDirectory;
    require(reportDirectory.isValid(), "cannot create temporary TU report directory");
    const QString reportFilePath = reportDirectory.filePath(QStringLiteral("tu.html"));
    QFile reportFile(reportFilePath);
    require(reportFile.open(QIODevice::WriteOnly | QIODevice::Text),
            "cannot create temporary TU protocol");
    reportFile.write("<html><body><table><tr><th>Оператор</th><td></td></tr></table></body></html>");
    reportFile.close();

    page.setRunResult(result, reportFilePath, {});
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

    auto* reportPath = page.findChild<QLabel*>(QStringLiteral("finishReportPaths"));
    require(reportPath && reportPath->text().contains(QStringLiteral("tu.html"))
                && reportPath->text().contains(QStringLiteral("tu-ui-test")),
            "TU finish must expose report path and run_id internally");

    auto* completionOperator = page.findChild<QLineEdit*>(QStringLiteral("tuCompletionOperator"));
    auto* tuFlow = page.findChild<TuFlowWidget*>(QStringLiteral("tuFlowWidget"));
    require(completionOperator && tuFlow && !completionOperator->isHidden(),
            "TU v0.5 must request operator only after the automatic run finishes");
    completionOperator->setText(QStringLiteral("Иванов И.И."));
    auto* makeReport = buttonByText(page, QStringLiteral("СФОРМИРОВАТЬ ОТЧЁТ"));
    require(makeReport && makeReport->isEnabled(),
            "post-run operator must enable report generation");
    makeReport->click();
    QApplication::processEvents();
    require(tuFlow->activeOperator() == QStringLiteral("Иванов И.И."),
            "TU operator must be captured at report time");
    require(hasLabelText(page, QStringLiteral("УБСИ: SN 345"))
                && hasLabelText(page, QStringLiteral("Оператор: Иванов И.И.")),
            "TU report page must show serial and post-run operator");
    auto* reportTable = page.findChild<QTableWidget*>(QStringLiteral("tuReportTable"));
    require(reportTable && reportTable->rowCount() == 12,
            "TU report page must show the 12 normative rows from prototype v0.5");
    require(reportFile.open(QIODevice::ReadOnly | QIODevice::Text),
            "cannot reopen generated TU protocol");
    const QString reportHtml = QString::fromUtf8(reportFile.readAll());
    require(reportHtml.contains(QStringLiteral("<th>Оператор</th><td>Иванов И.И.</td>")),
            "post-run operator must be persisted into the TU protocol");
    saveScene(page, requested, QStringLiteral("TU_FINISH"), screenshot);

    std::cout << "Dedicated TU v0.5 serial-first/post-run-operator flow passed\n";
    return EXIT_SUCCESS;
}
