#include "orbita_stand/station_session.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace orbita::stand;
using namespace std::chrono_literals;

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct State { int reads = 0; int stops = 0; bool fail = false; bool disconnected = false; };
class Simulator final : public IParameterSource {
public:
    Simulator(std::string id, double value, State& state)
        : id_(std::move(id)), value_(value), state_(state),
          timestamp_(std::chrono::system_clock::now()) {}
    std::string sourceId() const override { return id_; }
    std::string probe() override { return "ready"; }
    ParameterSample read(const std::string& key) override {
        ++state_.reads;
        if (state_.fail) throw std::runtime_error("simulated disconnect");
        ParameterSample sample;
        sample.parameterKey = key;
        sample.sequence = 1;
        sample.timestamp = timestamp_;
        sample.physicalValue = value_;
        sample.quality = state_.disconnected ? SampleQuality::Disconnected : SampleQuality::Good;
        return sample;
    }
    void safeStop() noexcept override { ++state_.stops; }
private:
    std::string id_;
    double value_;
    State& state_;
    std::chrono::system_clock::time_point timestamp_;
};
}

int main()
{
    try {
        State first, second;
        StationSession station;
        auto& samples = station.parameters();
        samples.add(std::make_unique<Simulator>("one", 12, first));
        samples.add(std::make_unique<Simulator>("two", 34, second));
        require(!samples.latest("one", "voltage", 1s), "unread value was invented");
        samples.refresh("one", "voltage");
        samples.refresh("two", "voltage");
        auto monitor = samples.latest("one", "voltage", 1s);
        auto check = samples.latest("one", "voltage", 1s);
        require(monitor && check && monitor->physicalValue == 12 && check->physicalValue == 12
                    && first.reads == 1, "consumers did not share acquired sample");
        require(samples.latest("two", "voltage", 1s)->physicalValue == 34,
                "source identity was lost");
        std::this_thread::sleep_for(20ms);
        samples.refresh("one", "voltage");
        require(samples.latest("one", "voltage", 1ms)->quality == SampleQuality::Stale,
                "reading the same sample refreshed its age");
        samples.refresh("one", "current");
        first.disconnected = true;
        samples.refresh("one", "voltage");
        require(samples.latest("one", "current", 1s)->quality == SampleQuality::Disconnected,
                "reported disconnect did not mark other parameters");
        first.disconnected = false;
        samples.refresh("one", "current");
        first.fail = true;
        require(samples.refresh("one", "voltage").quality == SampleQuality::Disconnected,
                "source failure was hidden");
        require(samples.latest("one", "current", 1s)->quality == SampleQuality::Disconnected,
                "other parameters hid source failure");
        first.fail = false;
        samples.refresh("one", "voltage");
        require(samples.latest("one", "voltage", 1ms)->quality == SampleQuality::Stale,
                "replayed sample became fresh after disconnect");
        require(samples.latest("two", "voltage", 1s)->quality == SampleQuality::Good,
                "one source failure invalidated another source");
        bool duplicateRejected = false;
        try { samples.add(std::make_unique<Simulator>("two", 0, second)); }
        catch (const std::invalid_argument&) { duplicateRejected = true; }
        require(duplicateRejected, "duplicate source id accepted");
        station.clear();
        require(first.stops == 1 && second.stops == 1, "session did not stop sources once");
        std::cout << "parameter repository contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
