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
    require(overview && status, "power YALK overview controls not found");

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

    require(overview->property("fresh").toBool(), "fresh POWER_YALK event must mark overview fresh");
    require(overview->property("backgroundChannelCount").toInt() == 80,
            "fresh POWER_YALK event must render all 80 channels");
    require(overview->property("renderedChannelCount").toInt() == 80,
            "power YALK overview must show 80 live bars");
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
    require(!overview->isEnabled(), "stale POWER_YALK overview must be visibly disabled");
    require(status->text().contains(QStringLiteral("НЕТ СВЕЖИХ ДАННЫХ")),
            "stale power YALK status must be explicit");

    std::cout << "Power YALK live overview event test passed\n";
    return EXIT_SUCCESS;
}
