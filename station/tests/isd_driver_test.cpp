#include "orbita_stand/isd_driver.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace orbita::stand;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

struct FakeIsd {
    std::mutex mutex;
    std::vector<std::string> calls;
    std::atomic<int> inFlight{0};
    std::atomic<int> maxInFlight{0};
    bool failNextSwitch = false;
    bool failNextReset = false;
    bool failNextProbe = false;
    unsigned fullResetCount = 0;

    void record(const std::string& call)
    {
        const int current = ++inFlight;
        int observed = maxInFlight.load();
        while (current > observed
               && !maxInFlight.compare_exchange_weak(observed, current)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        {
            std::lock_guard<std::mutex> lock(mutex);
            calls.push_back(call);
        }
        --inFlight;
    }

    void clearCalls()
    {
        std::lock_guard<std::mutex> lock(mutex);
        calls.clear();
    }

    IsdDriverOps ops()
    {
        IsdDriverOps result;
        result.probe = [this] {
            record("probe");
            if (failNextProbe) {
                failNextProbe = false;
                throw std::runtime_error("simulated probe failure");
            }
            return std::string("HTTP page");
        };
        result.serviceFullReset = [this] {
            record("service_full_reset");
            ++fullResetCount;
            if (failNextReset) {
                failNextReset = false;
                throw std::runtime_error("simulated service timeout");
            }
        };
        result.setSwitch = [this](unsigned type, unsigned channel, bool enabled) {
            record("switch:" + std::to_string(type) + ":"
                   + std::to_string(channel) + ":" + (enabled ? "1" : "0"));
            if (failNextSwitch) {
                failNextSwitch = false;
                throw std::runtime_error("simulated timeout after request");
            }
        };
        result.setAnalog = [this](unsigned channel, unsigned value, bool enabled) {
            record("analog:" + std::to_string(channel) + ":"
                   + std::to_string(value) + ":" + (enabled ? "1" : "0"));
        };
        result.setYalkVoltage = [this](unsigned channel, double volts) {
            record("yalk_voltage:" + std::to_string(channel) + ":" + std::to_string(volts));
        };
        result.disableYalkOutput = [this](unsigned channel) {
            record("yalk_off:" + std::to_string(channel));
        };
        return result;
    }
};

void ownershipAndTargetedCleanup()
{
    FakeIsd fake;
    IsdDriver driver(fake.ops(), 64);

    require(driver.sessionState() == IsdSessionState::Operational,
            "A new session must be operational, not globally-known");
    require(driver.statusText().find("global_hardware_state=not_readable") != std::string::npos,
            "Driver status must not claim global hardware state is known");

    driver.setSwitch(2, 37, true, "yvp.5");
    require(driver.ownedCount() == 1, "Acknowledged ON route is not owned");
    require(driver.statusText().find("certainty=acknowledged_active") != std::string::npos,
            "Acknowledged ON route has wrong certainty");

    bool conflict = false;
    try { driver.setSwitch(2, 37, false, "other"); }
    catch (const std::runtime_error&) { conflict = true; }
    require(conflict, "A foreign owner changed an owned route");

    const auto resetBefore = fake.fullResetCount;
    driver.releaseOwner("yvp.5");
    require(driver.ownedCount() == 0, "releaseOwner left the route owned");
    require(fake.fullResetCount == resetBefore,
            "releaseOwner must never use firmware type=4/full reset");
    require(!fake.calls.empty() && fake.calls.back() == "switch:2:37:0",
            "releaseOwner did not send targeted OFF");
}

void timeoutOnRemainsPossiblyActive()
{
    FakeIsd fake;
    IsdDriver driver(fake.ops(), 64);
    fake.failNextSwitch = true;

    bool failed = false;
    try { driver.setSwitch(2, 38, true, "timeout-owner"); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "Injected active-command timeout was not propagated");
    require(driver.sessionState() == IsdSessionState::Indeterminate,
            "Active-command timeout must make the session indeterminate");
    require(driver.ownedCount() == 1,
            "Timed-out ON must remain owned/possibly-active");
    require(driver.statusText().find("certainty=possibly_active") != std::string::npos,
            "Timed-out ON was not marked possibly-active");
    require(driver.traceText().find("result=indeterminate") != std::string::npos,
            "Indeterminate result is absent from the trace");

    bool blocked = false;
    try { driver.setSwitch(2, 39, true, "next-owner"); }
    catch (const std::runtime_error&) { blocked = true; }
    require(blocked, "New active mutation was accepted while indeterminate");

    driver.releaseOwner("timeout-owner");
    require(driver.ownedCount() == 0,
            "Targeted cleanup did not clear the possibly-active route");
    require(driver.sessionState() == IsdSessionState::Operational,
            "Successful targeted cleanup did not resolve process-owned uncertainty");
    require(fake.calls.back() == "switch:2:38:0",
            "Cleanup after timeout did not attempt targeted OFF");
}

void restartRecoveryReplaysExactDesiredState()
{
    FakeIsd fake;
    IsdDriver driver(fake.ops(), 128);

    driver.setSwitch(2, 33, true, "run");
    driver.setAnalog(44, 777, true, "run");
    driver.setYalkVoltage(1, 3.1, "run");

    fake.failNextSwitch = true;
    bool failed = false;
    try { driver.setSwitch(2, 35, true, "run"); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "Recovery setup did not enter indeterminate state");
    require(driver.sessionState() == IsdSessionState::Indeterminate,
            "Timed-out ON did not make recovery necessary");
    require(driver.ownedCount() == 4,
            "Desired state for timed-out route was not retained");

    fake.clearCalls();
    driver.recoverAfterRestart("operator-restart");

    const std::vector<std::string> expected{
        "probe",
        "switch:2:33:1",
        "analog:44:777:1",
        "yalk_voltage:1:3.100000",
        "switch:2:35:1",
    };
    require(fake.calls == expected,
            "Recovery did not probe then replay exact remembered state in activation order");
    require(fake.fullResetCount == 0,
            "Restart recovery must never use firmware type=4/full reset");
    require(driver.sessionState() == IsdSessionState::Operational,
            "Successful restart recovery did not restore operational session");
    require(driver.ownedCount() == 4,
            "Recovery must retain ownership of replayed active routes");
    const auto status = driver.statusText();
    require(status.find("possibly_active") == std::string::npos,
            "Recovered routes were not promoted to acknowledged state");
    require(status.find("kind=analog,type=1,channel=44,value=777") != std::string::npos,
            "Analog desired code is not retained for recovery/status");
    require(status.find("kind=yalk_output,type=5,channel=1,volts=3.1") != std::string::npos,
            "YALK desired voltage is not retained for recovery/status");
    require(driver.traceText().find("operation=recover_after_restart") != std::string::npos,
            "Recovery operation is absent from the trace");

    driver.safeStopAll();
}

void failedRecoveryStaysIndeterminate()
{
    FakeIsd fake;
    IsdDriver driver(fake.ops(), 64);
    fake.failNextSwitch = true;
    try { driver.setSwitch(2, 41, true, "run"); } catch (...) {}
    fake.failNextProbe = true;
    const auto resetBefore = fake.fullResetCount;

    bool failed = false;
    try { driver.recoverAfterRestart("operator-restart"); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "Recovery probe failure was swallowed");
    require(driver.sessionState() == IsdSessionState::Indeterminate,
            "Failed recovery must keep session indeterminate");
    require(driver.ownedCount() == 1,
            "Failed recovery lost desired owned state");
    require(fake.fullResetCount == resetBefore,
            "Failed recovery fell back to a destructive full reset");
    driver.safeStopAll();
}

void safeStopNeverUsesFullReset()
{
    FakeIsd fake;
    IsdDriver driver(fake.ops(), 64);
    driver.setSwitch(2, 33, true, "cleanup");
    driver.setAnalog(44, 100, true, "cleanup");
    driver.setYalkVoltage(1, 3.1, "cleanup");
    const auto resetBefore = fake.fullResetCount;
    driver.safeStopAll();
    require(fake.fullResetCount == resetBefore,
            "safeStopAll issued service/full reset");
    require(driver.ownedCount() == 0,
            "safeStopAll did not release owned outputs");
}

void probeIsConnectivityOnly()
{
    FakeIsd fake;
    IsdDriver driver(fake.ops(), 64);
    driver.setSwitch(2, 34, true, "probe-owner");
    const auto before = driver.statusText();
    const auto response = driver.probe();
    require(response == "HTTP page", "Probe did not return transport response");
    const auto after = driver.statusText();
    require(before == after,
            "Probe changed session state or ownership");
}

void serviceResetIsExplicitAndOneShot()
{
    FakeIsd fake;
    IsdDriver driver(fake.ops(), 64);
    driver.setSwitch(2, 40, true, "service-test");
    driver.serviceFullReset("technician");
    require(fake.fullResetCount == 1,
            "Explicit service full reset was not called exactly once");
    require(driver.ownedCount() == 0,
            "Acknowledged service full reset did not clear process ownership");

    fake.failNextReset = true;
    bool failed = false;
    try { driver.serviceFullReset("technician"); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "Service full-reset failure was not propagated");
    require(fake.fullResetCount == 2,
            "Failed service full reset was retried implicitly");
    require(driver.sessionState() == IsdSessionState::Indeterminate,
            "Failed service full reset must leave session indeterminate");
}

void serializesTransport()
{
    FakeIsd fake;
    IsdDriver driver(fake.ops(), 128);
    std::vector<std::thread> threads;
    for (unsigned index = 0; index < 8; ++index) {
        threads.emplace_back([&, index] {
            driver.setSwitch(2, 1 + index, true, "parallel");
        });
    }
    for (auto& thread : threads) thread.join();
    require(fake.maxInFlight.load() == 1,
            "ISD transport calls were not strictly serialized");
    driver.safeStopAll();
}

} // namespace

int main()
{
    try {
        ownershipAndTargetedCleanup();
        timeoutOnRemainsPossiblyActive();
        restartRecoveryReplaysExactDesiredState();
        failedRecoveryStaysIndeterminate();
        safeStopNeverUsesFullReset();
        probeIsConnectivityOnly();
        serviceResetIsExplicitAndOneShot();
        serializesTransport();
        std::cout << "ISD driver tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ISD driver test failed: " << error.what() << '\n';
        return 1;
    }
}
