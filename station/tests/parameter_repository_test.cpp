#include "orbita_stand/station_session.h"
#include "orbita_stand/parameter_procedures.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <future>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace orbita::stand;
using namespace std::chrono_literals;

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct State {
    int reads = 0; int stops = 0; bool fail = false; bool disconnected = false;
    std::atomic_bool advance{false};
    std::atomic<double> nextValue{0};
    std::atomic<int> timestampOffsetMs{0};
};
class Simulator final : public IParameterSource {
public:
    Simulator(std::string id, double value, State& state)
        : id_(std::move(id)), value_(value), state_(state),
          timestamp_(std::chrono::system_clock::now()) {}
    std::string sourceId() const override { return id_; }
    ParameterProvenance provenance() const noexcept override { return ParameterProvenance::Simulated; }
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
        sample.timestamp = timestamp_ + std::chrono::milliseconds(state_.timestampOffsetMs.load());
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
class SlowSimulator final : public IParameterSource {
public:
    std::atomic_bool entered{false};
    std::atomic_bool release{false};
    std::string sourceId() const override { return "slow"; }
    std::string probe() override { return "ready"; }
    ParameterSample read(const std::string& key) override {
        entered = true;
        while (!release.load()) std::this_thread::sleep_for(1ms);
        ParameterSample result; result.parameterKey = key; return result;
    }
    void safeStop() noexcept override {}
};
}

int main()
{
    try {
        {
            ParameterRepository repository;
            auto source = std::make_unique<SlowSimulator>();
            auto* slow = source.get();
            repository.add(std::move(source));
            std::thread reader([&] { repository.refresh("slow", "value"); });
            while (!slow->entered.load()) std::this_thread::yield();
            auto subscription = std::async(std::launch::async, [&] { return repository.subscribeStream("slow"); });
            const bool returned = subscription.wait_for(2s) == std::future_status::ready;
            slow->release = true;
            reader.join();
            auto active = subscription.get();
            require(returned && active && active->provenance() == ParameterProvenance::Unknown,
                    "subscription blocked or invented the source provenance");
        }
        {
            // Файл содержит фактические результаты выбранного источника и
            // доступен до остановки; отказ пути не мешает второму источнику.
            QTemporaryDir directory;
            require(directory.isValid(), "recording test directory unavailable");
            State firstSource, secondSource;
            ParameterRepository repository;
            repository.add(std::make_unique<Simulator>("one", 12, firstSource));
            repository.add(std::make_unique<Simulator>("two", 34, secondSource));
            const auto path = directory.filePath(QStringLiteral("запись.jsonl"));
            ParameterStreamRecording recording(repository, "one", path.toUtf8().toStdString());
            const auto wait = [](const auto& predicate) {
                const auto deadline = std::chrono::steady_clock::now() + 3s;
                while (!predicate() && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(10ms);
                return predicate();
            };
            require(wait([&] { return recording.status().state != ParameterRecordingState::Starting; })
                        && recording.status().state == ParameterRecordingState::Recording,
                    "recording did not open output file");
            repository.refresh("one", "voltage");
            repository.refresh("two", "voltage");
            require(wait([&] { return recording.status().samplesWritten == 1; }),
                    "recording did not flush sample while active");
            QFile file(path);
            require(file.open(QIODevice::ReadOnly), "partial recording unavailable");
            const auto partial = file.readAll().split('\n');
            file.close();
            require(partial.size() >= 3
                        && QJsonDocument::fromJson(partial[0]).object().value("source_id").toString() == "one"
                        && QJsonDocument::fromJson(partial[0]).object().value("provenance").toString() == "simulated"
                        && recording.status().provenance == ParameterProvenance::Simulated
                        && QJsonDocument::fromJson(partial[1]).object().value("value").toDouble() == 12,
                    "partial file lost source or observed value");
            firstSource.fail = true;
            repository.refresh("one", "voltage");
            require(wait([&] { return recording.status().samplesWritten == 2; }),
                    "disconnection was not recorded");
            recording.requestStop();
            require(wait([&] { return recording.status().state == ParameterRecordingState::Completed; }),
                    "recording did not finish");
            require(file.open(QIODevice::ReadOnly), "completed recording unavailable");
            const auto complete = file.readAll().split('\n');
            file.close();
            require(complete.size() == 5, "unexpected number of recording lines");
            const auto disconnected = QJsonDocument::fromJson(complete[2]).object();
            require(disconnected.value("quality").toString() == "disconnected"
                        && disconnected.value("value").isNull()
                        && QJsonDocument::fromJson(complete[3]).object().value("complete").toBool(),
                    "recording did not retain its final marker or included another source");
            ParameterStreamRecording badPath(repository, "one",
                directory.filePath(QStringLiteral("missing/output.jsonl")).toUtf8().toStdString());
            require(wait([&] { return badPath.status().state == ParameterRecordingState::Failed; })
                        && !badPath.status().error.empty(), "write failure was not observable");
            repository.refresh("two", "voltage");
            require(repository.latest("two", "voltage", 1s)->physicalValue == 34,
                    "recording failure interrupted another source");
            ParameterStreamRecording interrupted(repository, "two",
                directory.filePath(QStringLiteral("interrupted.jsonl")).toUtf8().toStdString());
            require(wait([&] { return interrupted.status().state == ParameterRecordingState::Recording; }),
                    "second recording did not start");
            repository.clear();
            interrupted.requestStop();
            require(wait([&] { return interrupted.status().state == ParameterRecordingState::Failed; }),
                    "source shutdown was incorrectly reported as a complete recording");
        }
        {
            // Два источника продолжают опрашиваться независимо; подписка
            // получает только выбранный источник и не теряет очередь при остановке.
            State recordedState, otherState;
            ParameterRepository repository;
            repository.add(std::make_unique<Simulator>("recorded", 12, recordedState));
            repository.add(std::make_unique<Simulator>("other", 34, otherState));
            auto stream = repository.subscribeStream("recorded", 2);
            repository.refresh("recorded", "voltage");
            repository.refresh("other", "voltage");
            repository.refresh("recorded", "current");
            stream->close();
            repository.refresh("recorded", "voltage");
            const auto saved = stream->take();
            require(saved.closed && !saved.overflow && saved.samples.size() == 2
                        && saved.samples[0].parameterKey == "voltage"
                        && saved.samples[1].parameterKey == "current"
                        && saved.samples[0].timestamp != std::chrono::system_clock::time_point{}
                        && otherState.reads == 1, "selected source stream lost identity or final samples");
            auto limited = repository.subscribeStream("recorded", 1);
            repository.refresh("recorded", "voltage");
            repository.refresh("recorded", "voltage");
            const auto overflow = limited->take();
            require(overflow.closed && overflow.overflow && overflow.samples.size() == 1,
                    "stream overflow did not preserve the partial queue and signal loss");
            auto failedStream = repository.subscribeStream("recorded");
            recordedState.fail = true;
            repository.refresh("recorded", "voltage");
            repository.refresh("other", "voltage");
            const auto failed = failedStream->take();
            const auto other = repository.latest("other", "voltage", 1s);
            require(failed.samples.size() == 1 && failed.samples[0].quality == SampleQuality::Disconnected
                        && !failed.samples[0].diagnostic.empty() && other
                        && other->quality == SampleQuality::Good && other->physicalValue == 34,
                    "source failure was not captured independently from the second source");
            auto shutdown = repository.subscribeStream("other");
            repository.clear();
            require(shutdown->take().closed, "repository shutdown left a stream open");
        }
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
        checkScenario.steps[0].arguments["conversion"] = "linear";
        checkScenario.steps[0].arguments["scale"] = "1000";
        checkScenario.steps[0].arguments["offset"] = "5";
        checkScenario.steps[0].arguments["output_unit"] = "mV";
        checkScenario.steps[0].arguments["lower"] = "12000";
        checkScenario.steps[0].arguments["upper"] = "12010";
        const auto converted = runFresh(12);
        require(converted.verdict == RunVerdict::Ok && converted.steps[0].measurements[0].measured == 12005
                    && converted.steps[0].measurements[0].unit == "mV"
                    && converted.steps[0].measurements[0].attributes.at("input_value") == "12"
                    && converted.steps[0].measurements[0].attributes.at("input_unit") == "V"
                    && converted.steps[0].measurements[0].attributes.at("scale") == "1000",
                "linear conversion lost input or result provenance");
        checkScenario.steps[0].arguments["scale"] = "1e308";
        require(runFresh(12).verdict == RunVerdict::Error,
                "overflowing conversion was reported as a measurement");
        checkScenario.steps[0].arguments.erase("conversion");
        checkScenario.steps[0].arguments.erase("scale");
        checkScenario.steps[0].arguments.erase("offset");
        checkScenario.steps[0].arguments.erase("output_unit");
        checkScenario.steps[0].arguments["lower"] = "11";
        checkScenario.steps[0].arguments["upper"] = "13";
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
        checkScenario.steps[0].procedure = "parameter.wait";
        checkScenario.steps[0].arguments["stable_ms"] = "100";
        checkScenario.steps[0].arguments["max_gap_ms"] = "200";
        checkScenario.steps[0].arguments["timeout_ms"] = "1000";
        require(station.scenarioEngine().validate(checkScenario).empty(), "valid stability wait rejected");
        std::thread sequence([&] {
            const auto emitSample = [&](double value, std::chrono::milliseconds pause) {
                std::this_thread::sleep_for(pause);
                first.nextValue = value; first.advance = true;
                samples.refresh("one", "voltage");
            };
            emitSample(12, 50ms); emitSample(14, 40ms); emitSample(12, 40ms); emitSample(12, 120ms);
        });
        const auto startWait = std::chrono::steady_clock::now();
        const auto stable = station.runScenario(checkScenario, "test", "", false);
        sequence.join();
        require(stable.verdict == RunVerdict::Ok && stable.steps[0].measurements[0].measured == 12
                    && std::chrono::steady_clock::now() - startWait >= 230ms,
                "stability wait ignored an intermediate out-of-range sample");
        checkScenario.steps[0].arguments["timeout_ms"] = "500";
        require(station.runScenario(checkScenario, "test", "", false).verdict == RunVerdict::Error,
                "stability wait accepted an old or repeated sample");
        checkScenario.steps[0].arguments["max_gap_ms"] = "50";
        std::thread sparse([&] {
            for (int i = 0; i < 3; ++i) {
                std::this_thread::sleep_for(120ms);
                first.nextValue = 12; first.advance = true;
                samples.refresh("one", "voltage");
            }
        });
        const auto sparseResult = station.runScenario(checkScenario, "test", "", false);
        sparse.join();
        require(sparseResult.verdict == RunVerdict::Error,
                "sparse in-range samples were accepted as stable");
        checkScenario.steps[0].arguments["max_gap_ms"] = "200";
        checkScenario.steps[0].arguments["max_gap_ms"] = "500";
        std::thread delayed([&] {
            std::this_thread::sleep_for(50ms);
            first.nextValue = 12; first.advance = true; samples.refresh("one", "voltage");
            std::this_thread::sleep_for(200ms);
            first.timestampOffsetMs = -150; first.advance = true; samples.refresh("one", "voltage");
        });
        const auto delayedResult = station.runScenario(checkScenario, "test", "", false);
        delayed.join();
        first.timestampOffsetMs = 0;
        require(delayedResult.verdict == RunVerdict::Error,
                "arrival-time duration was mistaken for source-time stability");
        checkScenario.steps[0].arguments["max_gap_ms"] = "200";
        const auto beforeOverflow = samples.observationsAfter("one", "voltage", UINT64_MAX).lastRevision;
        for (int i = 0; i < 260; ++i) samples.refresh("one", "voltage");
        require(samples.observationsAfter("one", "voltage", beforeOverflow).overflow,
                "lost intermediate observations were not detected");
        checkScenario.steps[0].arguments["stable_ms"] = "501";
        require(!station.scenarioEngine().validate(checkScenario).empty(), "stability longer than timeout accepted");
        checkScenario.steps[0].procedure = "parameter.measure";
        checkScenario.steps[0].arguments.erase("stable_ms");
        checkScenario.steps[0].arguments.erase("max_gap_ms");
        checkScenario.steps[0].arguments["timeout_ms"] = "2000";
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
