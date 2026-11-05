#include "miltech/orbita_sample_bridge.h"

#include "orbita.h"
#include "orbita_stand/sample_source.h"
#include "orbita_stand/station_session.h"
#include "orbita_stand/telemetry_procedures.h"
#include "miltech/orbita_sample_simulator.h"

#include <QTemporaryDir>
#include <QFile>
#include <cstdint>
#include <cstring>
#include <thread>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class FakeSampleSource final : public orbita::stand::ISampleSource {
public:
    bool open() override
    {
        open_ = true;
        return true;
    }

    void close() noexcept override
    {
        stop();
        open_ = false;
    }

    bool isOpen() const noexcept override { return open_; }

    bool start() override
    {
        ++startCalls;
        if (failStart) return false;
        open();
        running_ = true;
        return true;
    }

    void stop() noexcept override
    {
        ++stopCalls;
        running_ = false;
    }

    bool isRunning() const noexcept override { return running_; }

    void setSamplesCallback(SamplesCallback callback) override
    {
        samplesCallback_ = std::move(callback);
    }

    void setErrorCallback(ErrorCallback callback) override
    {
        errorCallback_ = std::move(callback);
    }

    void emitSamples(const std::vector<int16_t>& samples)
    {
        if (samplesCallback_) samplesCallback_(samples);
    }

    void emitError(const std::string& message)
    {
        if (errorCallback_) errorCallback_(message);
    }

    bool hasSamplesCallback() const { return static_cast<bool>(samplesCallback_); }
    bool hasErrorCallback() const { return static_cast<bool>(errorCallback_); }

    bool failStart = false;
    int startCalls = 0;
    int stopCalls = 0;

private:
    bool open_ = false;
    bool running_ = false;
    SamplesCallback samplesCallback_;
    ErrorCallback errorCallback_;
};

orbita::Orbita configuredDecoder()
{
    orbita::Orbita decoder;
    // This address is part of the repository's confirmed Orbita address set
    // data/catalog/address_sets/ubsi_yvp_fast.txt.
    decoder.setChannels({{"M16P1A11B21T21", "test", "test"}});
    if (decoder.getChannels().size() != 1) {
        throw std::runtime_error("test Orbita address was not accepted");
    }
    return decoder;
}

void lifecycleContract()
{
    auto decoder = configuredDecoder();
    FakeSampleSource source;
    miltech::integration::OrbitaSampleBridge bridge(source, decoder);

    require(bridge.start(), "bridge failed to start");
    require(bridge.isRunning(), "bridge did not enter running state");
    require(source.isRunning(), "sample source did not start");
    require(decoder.isRunning(), "Orbita decoder did not start");
    require(source.hasSamplesCallback(), "sample callback was not attached");
    require(source.hasErrorCallback(), "error callback was not attached");

    source.emitSamples(std::vector<int16_t>(256, 0));
    source.emitError("synthetic-source-error");
    require(bridge.lastError() == "synthetic-source-error",
            "source error was not propagated through the bridge");

    bridge.stop();
    require(!bridge.isRunning(), "bridge remained running after stop");
    require(!source.isRunning(), "sample source remained running after stop");
    require(!decoder.isRunning(), "Orbita decoder remained running after stop");
    require(!source.hasSamplesCallback(), "sample callback survived bridge stop");
    require(!source.hasErrorCallback(), "error callback survived bridge stop");
}

void scenarioParameterValidationContract()
{
    using namespace orbita::stand;
    StationSession session;
    registerTelemetryProcedures(session.scenarioEngine());
    int reads = 0;
    session.equipment().bind("orbita.parameter_source",
        [&](const std::string&, const std::map<std::string, std::string>&) {
            ++reads;
            return std::string("status=ready\nframes_processed=12\nphrase_error_percent=1\n"
                               "group_error_percent=2\nchannel_count=4\n");
        });
    ScenarioDefinition scenario;
    scenario.id = "simulated_health";
    scenario.title = "Проверка потока";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.publicationState = PublicationState::Published;
    ScenarioNode step;
    step.id = "health";
    step.title = "Поток";
    step.tuRequirement = "test.1";
    step.procedure = "telemetry.health";
    step.requiredCapabilities.insert("orbita.parameter_source");
    step.arguments = {{"max_phrase_error_percent", "2.5"},
                      {"max_group_error_percent", "3"}, {"diagnostic_only", "false"}};
    scenario.steps.push_back(step);

    // Некорректные параметры должны отклоняться до первого чтения имитатора,
    // в том числе при загрузке YAML без использования визуального редактора.
    for (const auto& value : {"-1", "101", "nan", "2.5xyz"}) {
        scenario.steps[0].arguments["max_phrase_error_percent"] = value;
        require(!session.scenarioEngine().validate(scenario).empty(),
                "invalid percentage was accepted by scenario validation");
        require(session.runScenario(scenario, "simulator", "SN", false).verdict == RunVerdict::Incomplete,
                "invalid scenario was executed");
        require(reads == 0, "invalid scenario accessed the simulated source");
    }
    scenario.steps[0].arguments["max_phrase_error_percent"] = "2.5";
    scenario.steps[0].arguments["diagnostic_only"] = "maybe";
    require(!session.scenarioEngine().validate(scenario).empty(),
            "invalid diagnostic mode was accepted");
    require(session.runScenario(scenario, "simulator", "SN", false).verdict == RunVerdict::Incomplete
                && reads == 0, "invalid mode accessed the simulated source");
    scenario.steps[0].arguments["diagnostic_only"] = "false";
    scenario.steps[0].arguments["max_phrase_error_percentage"] = "4";
    require(!session.scenarioEngine().validate(scenario).empty(),
            "unknown parameter was accepted");
    require(session.runScenario(scenario, "simulator", "SN", false).verdict == RunVerdict::Incomplete
                && reads == 0, "unknown parameter accessed the simulated source");
    scenario.steps[0].arguments.erase("max_phrase_error_percentage");
    auto laterStep = scenario.steps[0];
    laterStep.id = "later";
    laterStep.arguments["max_group_error_percent"] = "101";
    scenario.steps.push_back(laterStep);
    require(session.runScenario(scenario, "simulator", "SN", false).verdict == RunVerdict::Incomplete
                && reads == 0, "an invalid later step allowed earlier simulator activity");
    scenario.steps.pop_back();
    const auto result = session.runScenario(scenario, "simulator", "SN", false);
    require(result.verdict == RunVerdict::Ok && reads == 1
                && result.steps.size() == 1 && result.steps[0].measurements.size() == 4,
            "valid scenario did not evaluate simulated telemetry");
}

void failedSourceRollbackContract()
{
    auto decoder = configuredDecoder();
    FakeSampleSource source;
    source.failStart = true;
    miltech::integration::OrbitaSampleBridge bridge(source, decoder);

    require(!bridge.start(), "bridge must fail when station sample source fails");
    require(!bridge.isRunning(), "failed bridge start left running state set");
    require(!decoder.isRunning(), "failed source start left Orbita decoder running");
    require(!source.hasSamplesCallback(), "failed start leaked sample callback");
    require(!source.hasErrorCallback(), "failed start leaked error callback");
    require(!bridge.lastError().empty(), "failed start did not preserve diagnostic");
}

void syntheticStreamContract()
{
    auto decoder = configuredDecoder();
    auto source = miltech::integration::createOrbitaSampleSimulator();
    require(source->open(), "synthetic source did not open");
    miltech::integration::OrbitaSampleBridge bridge(*source, decoder);
    require(bridge.start(), "synthetic source did not start");
    bool valid = false;
    double measured = -1;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < until && !valid) {
        decoder.waitForData(std::chrono::milliseconds(100));
        for (const auto& value : decoder.getSnapshot().values) {
            if (value.valid) { valid = true; measured = value.value; }
        }
    }
    QTemporaryDir recordingDirectory;
    require(recordingDirectory.isValid(), "recording directory unavailable");
    bool rejected = false;
    try { decoder.startRecording(recordingDirectory.filePath("missing/out.tlm").toUtf8().toStdString()); }
    catch (const std::exception&) { rejected = true; }
    require(rejected && !decoder.isRecording(), "failed recording start was reported as active");
    const auto recordingPath = recordingDirectory.filePath("record.tlm");
    decoder.startRecording(recordingPath.toUtf8().toStdString());
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    QFile during(recordingPath);
    require(during.open(QIODevice::ReadOnly) && during.size() > 256,
            "recording was not flushed while running");
    during.close();
    decoder.stopRecording();
    QFile recording(recordingPath);
    require(recording.open(QIODevice::ReadOnly) && recording.size() > 256,
            "recording lost received words on stop");
    const QByteArray bytes = recording.readAll();
    qsizetype position = 256;
    std::uint32_t previousBlock = 0, previousTime = 0;
    while (position < bytes.size()) {
        require(bytes.size() - position >= 24, "truncated block header");
        std::uint32_t block = 0, count = 0, elapsed = 0;
        std::memcpy(&block, bytes.constData() + position, 4);
        std::memcpy(&count, bytes.constData() + position + 4, 4);
        std::memcpy(&elapsed, bytes.constData() + position + 8, 4);
        require(block == previousBlock + 1 && count > 0 && count <= 65536
                    && elapsed >= previousTime, "invalid block sequence or time");
        position += 24 + static_cast<qsizetype>(count) * 2;
        require(position <= bytes.size(), "truncated block data");
        previousBlock = block;
        previousTime = elapsed;
    }
    require(position == bytes.size() && previousBlock > 0, "recording contains no complete blocks");
    bridge.stop();
    require(valid && measured == 15,
            "synthetic M16 samples did not decode to the expected simulator value");
    require(!source->isRunning(), "synthetic source did not stop");
    FakeSampleSource silent;
    miltech::integration::OrbitaSampleBridge restarted(silent, decoder);
    require(restarted.start(), "silent source did not start after simulator");
    for (const auto& value : decoder.getSnapshot().values)
        require(!value.valid, "previous session value survived decoder restart");
    require(!decoder.waitForData(std::chrono::milliseconds(300)),
            "old samples were delivered after decoder restart");
    restarted.stop();
    require(bridge.start(), "synthetic source did not restart");
    bridge.stop();
    source->close();
}

void freshnessContract()
{
    const auto now = std::chrono::steady_clock::now();
    orbita::Snapshot snapshot;
    snapshot.values = {{"fresh", 12, true, now - std::chrono::seconds(1)},
                       {"stale", 42, true, now - std::chrono::seconds(6)},
                       {"unknown", 7, true, {}},
                       {"invalid", 5, false, now}};
    miltech::integration::invalidateStaleValues(snapshot, now, std::chrono::seconds(5));
    require(snapshot.values[0].valid && !snapshot.values[1].valid
                && !snapshot.values[2].valid && !snapshot.values[3].valid,
            "freshness must be evaluated independently for every channel");
}

} // namespace

int main()
{
    try {
        lifecycleContract();
        failedSourceRollbackContract();
        syntheticStreamContract();
        freshnessContract();
        scenarioParameterValidationContract();
        std::cout << "Orbita station sample bridge contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Orbita station sample bridge contract failed: "
                  << error.what() << '\n';
        return 1;
    }
}
