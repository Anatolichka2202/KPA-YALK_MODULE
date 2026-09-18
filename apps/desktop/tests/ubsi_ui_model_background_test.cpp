#include "ubsi_ui_model.h"

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

std::string sequence(int count, double offset, int nanIndex = -1)
{
    std::ostringstream out;
    for (int index = 0; index < count; ++index) {
        if (index) out << ',';
        if (index == nanIndex) out << "nan";
        else out << (offset + index);
    }
    return out.str();
}

} // namespace

int main()
{
    ubsi::ui::UiAdapter adapter;

    orbita::stand::RunEvent yalk;
    yalk.nodeId = "monitor";
    yalk.stage = "BACKGROUND";
    yalk.data = {
        {"section", "YALK"},
        {"background_mean", sequence(100, 1000.0)},
        {"background_min", sequence(100, 900.0)},
        {"background_max", sequence(100, 1100.0)},
    };
    adapter.apply(yalk);

    require(adapter.yalkAnalog.channels.size() == 80, "YALK analog model must keep 80 physical channels");
    require(adapter.yalkContact.channels.size() == 80, "YALK contact model must keep 80 physical channels");
    for (int index = 0; index < adapter.yalkAnalog.channels.size(); ++index) {
        const int address = adapter.yalkAnalog.channels[index].physicalAddress;
        const double expected = 1000.0 + address - 1;
        require(std::abs(adapter.yalkAnalog.channels[index].currentV - expected) < 1e-9,
                "YALK BACKGROUND did not update an analog channel by physical address");
        require(std::abs(adapter.yalkContact.channels[index].currentV - expected) < 1e-9,
                "YALK BACKGROUND did not update the analog part of contact view");
        require(adapter.yalkContact.channels[index].logic == -1,
                "YALK BACKGROUND must not invent digital contact state");
    }

    orbita::stand::RunEvent ytp;
    ytp.nodeId = "monitor";
    ytp.stage = "BACKGROUND";
    ytp.data = {
        {"section", "YTP"},
        {"background_mean", sequence(30, 200.0, 4)},
        {"background_min", sequence(30, 190.0, 4)},
        {"background_max", sequence(30, 210.0, 4)},
    };
    adapter.apply(ytp);

    require(adapter.ytp.channels.size() == 30, "YTP model must keep 30 channels");
    require(!std::isfinite(adapter.ytp.channels[4].currentOhm),
            "missing YTP background value must remain missing at the same channel index");
    require(std::abs(adapter.ytp.channels[5].currentOhm - 205.0) < 1e-9,
            "YTP positional background array shifted after a missing value");
    require(adapter.ytp.channels[5].verification == ubsi::ui::VerificationState::Pending,
            "background monitor must not invent YTP acceptance verdicts");

    std::cout << "UI all-channel background contract passed\n";
    return EXIT_SUCCESS;
}
