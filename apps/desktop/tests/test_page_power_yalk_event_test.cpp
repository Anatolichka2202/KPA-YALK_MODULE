#include "test_page.h"

#include <QApplication>
#include <QLabel>
#include <QWidget>

#include <cstdlib>
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
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestPage page;
    page.setProductionMode(true);
    page.setRunInProgress(true, QStringLiteral("power"));

    auto* overview = page.findChild<QWidget*>(QStringLiteral("powerYalkOverview"));
    auto* status = page.findChild<QLabel*>(QStringLiteral("powerYalkStatus"));
    auto* analog = page.findChild<QWidget*>(QStringLiteral("yalkAnalogOverviewV05"));
    require(overview && status && analog,
            "power/restored analog YALK overview controls not found");

    std::ostringstream values;
    for (int channel = 0; channel < 80; ++channel) {
        if (channel) values << ',';
        values << (3.08 + (channel % 9 - 4) * 0.003);
    }

    orbita::stand::RunEvent fresh;
    fresh.nodeId = "supply_range";
    fresh.stage = "POWER_YALK";
    fresh.data = {{"fresh", "true"}, {"setpoint_v", "35"},
                  {"channel_count", "80"}, {"values_v", values.str()}};
    page.setRunEvent(fresh);
    QApplication::processEvents();

    require(overview->property("fresh").toBool(),
            "fresh POWER_YALK event must mark overview fresh");
    require(overview->isEnabled(),
            "fresh POWER_YALK overview must be enabled");
    require(status->text().contains(QStringLiteral("80 каналов")),
            "fresh power YALK status must state the 80-channel snapshot");
    require(status->text().contains(QStringLiteral("35")),
            "power YALK status must show current supply setpoint");

    orbita::stand::RunEvent stale;
    stale.nodeId = "supply_range";
    stale.stage = "POWER_YALK";
    stale.data = {{"fresh", "false"}, {"setpoint_v", "19"},
                  {"channel_count", "80"}, {"detail", "timeout"}};
    page.setRunEvent(stale);
    QApplication::processEvents();

    require(!overview->property("fresh").toBool(),
            "stale POWER_YALK event must not look fresh");
    require(!overview->isEnabled(),
            "stale POWER_YALK overview must be visibly disabled");
    require(status->text().contains(QStringLiteral("НЕТ СВЕЖИХ ДАННЫХ")),
            "stale power YALK status must be explicit");

    std::ostringstream background;
    for (int channel = 0; channel < 100; ++channel) {
        if (channel) background << ',';
        background << (0.002 + (channel % 7 - 3) * 0.0004);
    }
    orbita::stand::RunEvent backgroundEvent;
    backgroundEvent.nodeId = "monitor";
    backgroundEvent.stage = "BACKGROUND";
    backgroundEvent.data = {{"section", "YALK"},
                            {"background_mean", background.str()},
                            {"background_min", background.str()},
                            {"background_max", background.str()}};
    page.setRunEvent(backgroundEvent);

    orbita::stand::RunEvent analogPoint;
    analogPoint.nodeId = "yalk_channels";
    analogPoint.stage = "MEASUREMENT";
    analogPoint.verdict = orbita::stand::RunVerdict::Ok;
    analogPoint.data = {{"ulk_address", "9"}, {"command_v", "0"},
                        {"v7_v", "0.001"}, {"yalk_v", "0.002"},
                        {"signal", "0"},
                        {"value_samples", "0.0017,0.0020,0.0022,0.0019"},
                        {"lower_limit_v", "-0.030"},
                        {"upper_limit_v", "0.032"}};
    page.setRunEvent(analogPoint);
    QApplication::processEvents();

    require(analog->property("renderedChannelCount").toInt() == 80,
            "restored YALK plane must retain all 80 channels, not only the current one");

    std::cout << "Power and restored YALK overview event test passed\n";
    return EXIT_SUCCESS;
}
