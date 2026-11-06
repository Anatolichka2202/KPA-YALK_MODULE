#include "orbita_stand/station_session.h"
#include "orbita_stand/parameter_procedures.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace orbita::stand;
using namespace std::chrono_literals;

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct State {
    int reads = 0; int stops = 0; bool fail = false; bool disconnected = false;
    std::atomic_bool advance{false};
    std::atomic<double> nextValue{0};
};
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
        if (state_.advance.exchange(false)) {
            timestamp_ = std::chrono::system_clock::now();
            value_ = state_.nextValue.load();
            ++sequence_;
        }
        ParameterSample sample;
        sample.parameterKey = key;
        sample.sequence = sequence_;
        sample.timestamp = timestamp_;
        sample.physicalValue = value_;
        sample.unit = "V";
        sample.quality = state_.disconnected ? SampleQuality::Disconnected : SampleQuality::Good;
        return sample;
    }
    void safeStop() noexcept override { ++state_.stops; }
private:
    std::string id_;
    double value_;
    State& state_;
    std::chrono::system_clock::time_point timestamp_;
    std::uint64_t sequence_ = 1;
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
        registerParameterProcedures(station.scenarioEngine(), samples);
        ScenarioDefinition checkScenario;
        checkScenario.id = "parameter-check";
        checkScenario.title = "Параметр";
        checkScenario.version = "1";
        checkScenario.catalogVersion = "1";
        checkScenario.publicationState = PublicationState::Published;
        ScenarioNode step;
        step.id = "measure";
        step.title = "Напряжение";
        step.tuRequirement = "test";
        step.procedure = "parameter.measure";
        step.arguments = {{"source", "one"}, {"parameter", "voltage"}, {"lower", "11"}, {"upper", "13"}, {"unit", "V"}, {"timeout_ms", "2000"}};
        checkScenario.steps.push_back(step);
        require(station.scenarioEngine().validate(checkScenario).empty(), "valid measurement rejected");
        auto runFresh = [&](double next) {
            std::thread producer([&] {
                std::this_thread::sleep_for(50ms);
                first.nextValue = next;
                first.advance = true;
                first.disconnected = false;
                samples.refresh("one", "voltage");
            });
            auto result = station.runScenario(checkScenario, "test", "", false);
            producer.join();
            return result;
        };
        // Старое значение 12 соответствует норме, новое 14 — нет. Сценарий
        // обязан дождаться нового отсчёта, а не принять снимок до воздействия.
        const auto changed = runFresh(14);
        require(changed.verdict == RunVerdict::Fail && changed.steps[0].measurements[0].measured == 14,
                "measurement accepted a sample from before the step");
        const auto measured = runFresh(12);
        require(measured.verdict == RunVerdict::Ok && measured.steps.size() == 1
                    && measured.steps[0].measurements.size() == 1
                    && measured.steps[0].measurements[0].measured == 12
                    && measured.steps[0].measurements[0].attributes.at("source_id") == "one"
                    && first.reads == 3, "scenario did not use the shared parameter snapshot");
        checkScenario.steps[0].arguments["unit"] = "A";
        require(runFresh(12).verdict == RunVerdict::Error,
                "mismatched source unit was accepted");
        checkScenario.steps[0].arguments["unit"] = "V";
        first.disconnected = true;
        samples.refresh("one", "voltage");
        std::thread recovery([&] {
            std::this_thread::sleep_for(50ms);
            first.disconnected = false;
            samples.refresh("one", "voltage"); // Старый отсчёт после восстановления.
            std::this_thread::sleep_for(50ms);
            first.nextValue = 14;
            first.advance = true;
            samples.refresh("one", "voltage");
        });
        const auto recovered = station.runScenario(checkScenario, "test", "", false);
        recovery.join();
        require(recovered.verdict == RunVerdict::Fail && recovered.steps[0].measurements[0].measured == 14,
                "measurement accepted an old sample after recovery");
        first.disconnected = true;
        samples.refresh("one", "voltage");
        require(station.runScenario(checkScenario, "test", "", false).verdict == RunVerdict::Error,
                "disconnected source was accepted as a measurement");
        first.disconnected = false;
        checkScenario.steps[0].arguments["lower"] = "13";
        checkScenario.steps[0].arguments["upper"] = "11";
        require(!station.scenarioEngine().validate(checkScenario).empty(), "reversed bounds accepted");

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
        require(samples.latest("two", "voltage", 10s)->quality == SampleQuality::Good,
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
