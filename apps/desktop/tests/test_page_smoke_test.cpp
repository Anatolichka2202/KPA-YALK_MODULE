#include "test_page.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

#include <cmath>
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

void saveScene(TestPage& page, const QString& requested, const QString& scene, const QString& path)
{
    if (path.isEmpty() || requested != scene) return;
    page.resize(1664, 935);
    page.show();
    QApplication::processEvents();
    require(page.grab().save(path), "cannot save operator UI screenshot");
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestPage page;
    const QString screenshot = qEnvironmentVariable("ORBITA_UI_SCREENSHOT");
    const QString scene = qEnvironmentVariable("MILTECH_UI_SCENE", QStringLiteral("YALK"));

    auto* scope = page.findChild<QComboBox*>(QStringLiteral("testScope"));
    auto* test = page.findChild<QComboBox*>(QStringLiteral("testType"));
    auto* operatorSelector = page.findChild<QComboBox*>(QStringLiteral("productionOperatorSelector"));
    auto* registry = page.findChild<QTableWidget*>(QStringLiteral("productionRegistryTable"));
    auto* queue = page.findChild<QTableWidget*>(QStringLiteral("productionSessionTable"));
    auto* enter = page.findChild<QPushButton*>(QStringLiteral("enterPreparation"));
    auto* equipment = page.findChild<QTableWidget*>(QStringLiteral("equipmentTable"));
    auto* histogram = page.findChild<QWidget*>(QStringLiteral("yalkChannelHistogram"));
    auto* contacts = page.findChild<QWidget*>(QStringLiteral("yalkContactThresholdOverview"));
    auto* overloadOverview = page.findChild<QWidget*>(QStringLiteral("yalkOverloadOverview"));
    auto* ytpHistogram = page.findChild<QWidget*>(QStringLiteral("ytpChannelHistogram"));
    auto* yvpOverview = page.findChild<QWidget*>(QStringLiteral("yvpEightChannelOverview"));
    auto* context = page.findChild<QLabel*>(QStringLiteral("frozenProcedureContext"));

    require(scope && test && operatorSelector && registry && queue && enter && equipment
                && histogram && contacts && overloadOverview && ytpHistogram && yvpOverview && context,
            "frozen operator UI controls not found");
    require(page.styleSheet().contains(QStringLiteral("#14171c")),
            "operator UI must keep the dark industrial palette");

    int routeEntries = 0;
    for (auto* label : page.findChildren<QLabel*>()) {
        if (!label->property("routeStageIndex").isValid()) continue;
        ++routeEntries;
        require(label->cursor().shape() != Qt::PointingHandCursor,
                "frozen global route must not be operator navigation");
    }
    require(routeEntries == 6, "runtime shell must retain six backend stage anchors");

    page.setProductionMode(true);
    page.setScenarioInfo(QStringLiteral("PROD_FULL"), true, false, {}, QStringLiteral("ready"));
    require(page.currentScenarioCode() == QStringLiteral("PROD_FULL"),
            "full production scope must resolve to PROD_FULL");

    page.setAvailableProductionProducts({QStringLiteral("345"), QStringLiteral("346")});
    QApplication::processEvents();
    require(registry->rowCount() == 2, "registered products must be visible in registry");
    require(queue->rowCount() == 0,
            "registered products must not be inserted into session queue automatically");

    operatorSelector->addItem(QStringLiteral("Иванов И.И."), QStringLiteral("Иванов И.И."));
    operatorSelector->setCurrentIndex(operatorSelector->findData(QStringLiteral("Иванов И.И.")));
    QApplication::processEvents();
    require(!enter->isEnabled(), "empty production queue must not enter preparation");

    QPushButton* addFirst = nullptr;
    for (auto* button : registry->findChildren<QPushButton*>()) {
        if (button->text().contains(QStringLiteral("Добавить"))) { addFirst = button; break; }
    }
    require(addFirst, "registry add action not found");
    addFirst->click();
    QApplication::processEvents();
    require(queue->rowCount() == 1, "registry action must add exactly one UBSI to queue");
    require(queue->item(0, 0)->text() == QStringLiteral("345"),
            "queue must retain registered UBSI serial");
    require(enter->isEnabled(), "operator plus non-empty queue must enable preparation");
    addFirst->click();
    require(queue->rowCount() == 1, "same UBSI must not be duplicated in one session queue");

    enter->click();
    QApplication::processEvents();
    for (auto* label : page.findChildren<QLabel*>()) {
        if (label->property("routeStageIndex").isValid())
            require(label->isHidden(), "global route must be hidden inside frozen runtime workspace");
    }
    require(!context->isHidden(), "current-procedure context must remain visible");

    page.setRunInProgress(true, QStringLiteral("full production"));

    orbita::stand::RunEvent supply;
    supply.nodeId = "supply_range";
    supply.stage = "SUPPLY";
    supply.data = {{"setpoint_v", "27"}, {"volts", "27.01"},
                   {"amperes", "0.238"}, {"elapsed_s", "0"}, {"duration_s", "0"}};
    page.setRunEvent(supply);
    saveScene(page, scene, QStringLiteral("POWER"), screenshot);

    orbita::stand::RunEvent yalkStart;
    yalkStart.nodeId = "yalk_channels";
    yalkStart.stage = "START";
    yalkStart.message = "YALK channels";
    page.setRunEvent(yalkStart);

    orbita::stand::RunEvent yalkBackground;
    yalkBackground.nodeId = "monitor";
    yalkBackground.stage = "BACKGROUND";
    yalkBackground.data = {{"section", "YALK"}};
    std::string backgroundMean, backgroundMinimum, backgroundMaximum;
    for (int index = 0; index < 100; ++index) {
        if (index) { backgroundMean += ','; backgroundMinimum += ','; backgroundMaximum += ','; }
        const double mean = 3.1 + (index % 9 - 4) * 0.003;
        const double spread = index % 11 == 0 ? 0.018 : 0.006;
        backgroundMean += std::to_string(mean);
        backgroundMinimum += std::to_string(mean - spread);
        backgroundMaximum += std::to_string(mean + spread);
    }
    yalkBackground.data["background_mean"] = backgroundMean;
    yalkBackground.data["background_min"] = backgroundMinimum;
    yalkBackground.data["background_max"] = backgroundMaximum;
    page.setRunEvent(yalkBackground);
    QApplication::processEvents();
    require(histogram->property("backgroundChannelCount").toInt() == 80,
            "YALK BACKGROUND must render all 80 configured addresses");
    require(histogram->property("renderedChannelCount").toInt() == 80,
            "YALK overview must not wait for current-channel measurement");

    orbita::stand::RunEvent overload;
    overload.nodeId = "yalk_overload_positive";
    overload.stage = "OVERLOAD";
    overload.data = {{"polarity", "+12 V"}, {"stressed_channel", "37"},
                     {"target_count", "88"}, {"impact_index", "37"},
                     {"impact_count", "176"}, {"settle_ms", "10000"}};
    page.setRunEvent(overload);
    for (int observed = 1; observed <= 88; ++observed) {
        if (observed == 37) continue;
        const double delta = observed == 30 ? 3.0 : observed == 20 ? 1.7
            : (observed % 5 - 2) * 0.35;
        const double baseline = 1800.0 + (observed % 13 - 6) * 0.8;
        orbita::stand::RunEvent measurement;
        measurement.nodeId = "yalk_overload_positive";
        measurement.stage = "MEASUREMENT";
        measurement.verdict = std::abs(delta) <= 2.0
            ? orbita::stand::RunVerdict::Ok : orbita::stand::RunVerdict::Fail;
        measurement.data = {{"polarity", "+12 V"}, {"stressed_channel", "37"},
            {"observed_channel", std::to_string(observed)},
            {"baseline_code", std::to_string(baseline)},
            {"current_code", std::to_string(baseline + delta)},
            {"delta_code", std::to_string(delta)}, {"lower_delta_code", "-2"},
            {"upper_delta_code", "2"}};
        page.setRunEvent(measurement);
    }
    QApplication::processEvents();
    require(overloadOverview->property("overloadMeasurementCount").toInt() == 87,
            "overload workspace must keep baseline/current for all 87 observed channels");
    saveScene(page, scene, QStringLiteral("YALK_OVERLOAD"), screenshot);

    page.setRunEvent(yalkStart);
    for (int address = 1; address <= 87; ++address) {
        if (!(address <= 28 || (address >= 32 && address <= 43)
                || (address >= 45 && address <= 70) || address >= 74)) continue;
        const double measured = 3.1 + (address % 9 - 4) * 0.003;
        const double spread = address % 11 == 0 ? 0.018 : 0.006;
        orbita::stand::RunEvent yalk;
        yalk.nodeId = "yalk_channels";
        yalk.stage = "MEASUREMENT";
        yalk.verdict = orbita::stand::RunVerdict::Ok;
        yalk.data = {{"ulk_address", std::to_string(address)}, {"command_v", "3.1"},
                     {"v7_v", "3.100"}, {"yalk_v", std::to_string(measured)},
                     {"lower_limit_v", "3.069"}, {"upper_limit_v", "3.131"},
                     {"signal", address % 7 == 0 ? "1" : "0"},
                     {"value_samples", address == 11 ? "3.050,3.100,3.112"
                         : std::to_string(measured - spread) + "," + std::to_string(measured)
                           + "," + std::to_string(measured + spread)}};
        page.setRunEvent(yalk);
    }
    require(histogram->property("warningChannelCount").toInt() == 1,
            "one passing channel with outlying samples must remain production UI warning");
    saveScene(page, scene, QStringLiteral("YALK"), screenshot);

    orbita::stand::RunEvent contactsStart;
    contactsStart.nodeId = "yalk_contacts";
    contactsStart.stage = "START";
    page.setRunEvent(contactsStart);
    for (double command : {0.0, 0.9, 2.5}) {
        for (int address = 1; address <= 87; ++address) {
            if (!(address <= 28 || (address >= 32 && address <= 43)
                    || (address >= 45 && address <= 70) || address >= 74)) continue;
            orbita::stand::RunEvent contact;
            contact.nodeId = "yalk_contacts";
            contact.stage = "MEASUREMENT";
            contact.verdict = orbita::stand::RunVerdict::Ok;
            contact.data = {{"ulk_address", std::to_string(address)},
                {"command_v", std::to_string(command)}, {"v7_v", std::to_string(command)},
                {"yalk_v", std::to_string(command)}, {"signal", command >= 2.0 ? "1" : "0"},
                {"lower_limit_v", std::to_string(command - .031)},
                {"upper_limit_v", std::to_string(command + .031)},
                {"value_samples", std::to_string(command)}};
            page.setRunEvent(contact);
        }
    }
    QApplication::processEvents();
    require(contacts->property("contactMeasurementCount").toInt() == 240,
            "contact workspace must retain three threshold states for 80 channels");
    saveScene(page, scene, QStringLiteral("YALK_CONTACT"), screenshot);

    orbita::stand::RunEvent operatorEvent;
    operatorEvent.nodeId = "ytp_channels";
    operatorEvent.stage = "OPERATOR";
    operatorEvent.data = {{"target_resistance_ohm", "120"}, {"point_index", "2"},
                          {"point_count", "3"}};
    page.setRunEvent(operatorEvent);
    for (int channel = 1; channel <= 30; ++channel) {
        const double measured = 120.0 + (channel % 9 - 4) * 0.08;
        orbita::stand::RunEvent ytp;
        ytp.nodeId = "ytp_channels";
        ytp.stage = "MEASUREMENT";
        ytp.verdict = orbita::stand::RunVerdict::Ok;
        ytp.data = {{"ytp_channel", std::to_string(channel)},
                    {"actual_reference_ohm", "120.000"},
                    {"measured_resistance_ohm", std::to_string(measured)},
                    {"value_samples", std::to_string(measured - .09) + ","
                        + std::to_string(measured) + "," + std::to_string(measured + .09)}};
        page.setRunEvent(ytp);
    }
    QApplication::processEvents();
    require(ytpHistogram->property("renderedChannelCount").toInt() == 30,
            "YTP frozen plane must retain all 30 channels");
    saveScene(page, scene, QStringLiteral("YTP"), screenshot);

    for (int channel = 1; channel <= 8; ++channel) {
        orbita::stand::RunEvent yvp;
        yvp.nodeId = "yvp_measurement";
        yvp.stage = "YVP_V7_POINT";
        yvp.verdict = orbita::stand::RunVerdict::NotRun;
        yvp.data = {{"backend", "v7_isd"}, {"yvp_channel", std::to_string(channel)},
                    {"gain_mv_per_pc", "1"}, {"set_frequency_hz", "500"},
                    {"measured_frequency_hz", "499.8"}, {"rigol_input_vpp", "2"},
                    {"v7_output_vrms", std::to_string(.141 + channel * .001)},
                    {"calculated_gain_mv_per_pc", std::to_string(.98 + channel * .004)},
                    {"acceptance", "not_applied"}};
        page.setRunEvent(yvp);
    }
    QApplication::processEvents();
    require(yvpOverview->property("yvpRenderedChannelCount").toInt() == 8,
            "YVP frozen plane must show eight channels for the current gain/frequency point");
    saveScene(page, scene, QStringLiteral("YVP"), screenshot);

    std::cout << "Frozen production operator UI smoke test passed\n";
    return EXIT_SUCCESS;
}
