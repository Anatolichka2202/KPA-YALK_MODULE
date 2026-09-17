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

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct FakeIsd {
    std::mutex mutex;
    std::vector<std::string> calls;
    std::atomic<int> inFlight{0};
    std::atomic<int> maxInFlight{0};
    bool failNextSwitch = false;
    unsigned resetCount = 0;

    void record(const std::string& call)
    {
        const int current = ++inFlight;
        int seen = maxInFlight.load();
        while (current > seen && !maxInFlight.compare_exchange_weak(seen, current)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        {
            std::lock_guard<std::mutex> lock(mutex);
            calls.push_back(call);
        }
        --inFlight;
    }

    IsdDriverOps ops()
    {
        IsdDriverOps result;
        result.probe = [this] {
            record("probe");
            return std::string("OK");
        };
        result.reset = [this] {
            record("reset");
            ++resetCount;
        };
        result.prepareYalk = [this] { record("yalk_prepare"); };
        result.setSwitch = [this](unsigned type, unsigned channel, bool enabled) {
            record("switch:" + std::to_string(type) + ":" + std::to_string(channel)
                + ":" + (enabled ? "1" : "0"));
            if (failNextSwitch) {
                failNextSwitch = false;
                throw std::runtime_error("simulated timeout");
            }
        };
        result.setAnalog = [this](unsigned channel, unsigned value, bool enabled) {
            record("analog:" + std::to_string(channel) + ":" + std::to_string(value)
                + ":" + (enabled ? "1" : "0"));
        };
        result.setYalkVoltage = [this](unsigned channel, double) {
            record("yalk_voltage:" + std::to_string(channel));
        };
        result.disableYalkOutput = [this](unsigned channel) {
            record("yalk_off:" + std::to_string(channel));
        };
        return result;
    }
};

} // namespace

int main()
{
    try {
        FakeIsd fake;
        IsdDriver driver(fake.ops(), 64);

        driver.setSwitch(2, 37, true, "yvp5");
        require(driver.state() == IsdDriverState::Known,
                "Successful switch must keep driver state KNOWN");
        require(driver.statusText().find("active_count=1") != std::string::npos,
                "Active switch was not tracked");
        require(driver.traceText().find("operation=switch") != std::string::npos,
                "Switch transaction was not traced");

        bool ownershipConflict = false;
        try { driver.setSwitch(2, 37, false, "other"); }
        catch (const std::runtime_error&) { ownershipConflict = true; }
        require(ownershipConflict,
                "A second owner was allowed to mutate an active ISD resource");

        driver.releaseOwner("yvp5");
        require(driver.statusText().find("active_count=0") != std::string::npos,
                "releaseOwner did not remove the tracked output");
        require(fake.resetCount == 0,
                "releaseOwner must not use full reset");

        driver.setSwitch(2, 38, true, "yvp6");
        fake.failNextSwitch = true;
        bool failureObserved = false;
        try { driver.setSwitch(2, 39, true, "yvp7"); }
        catch (const std::runtime_error&) { failureObserved = true; }
        require(failureObserved, "Injected transport failure was not propagated");
        require(driver.state() == IsdDriverState::Unknown,
                "Transport failure must move the driver to UNKNOWN");
        require(driver.traceText().find("result=indeterminate") != std::string::npos,
                "Indeterminate transaction was not recorded in trace");

        bool mutationBlocked = false;
        try { driver.setSwitch(2, 40, true, "yvp8"); }
        catch (const std::runtime_error&) { mutationBlocked = true; }
        require(mutationBlocked,
                "Active mutation was allowed while ISD state was UNKNOWN");

        driver.reset("recovery");
        require(driver.state() == IsdDriverState::Known,
                "Explicit reset did not recover KNOWN state");
        require(fake.resetCount == 1,
                "Explicit recovery reset was not issued exactly once");
        require(driver.statusText().find("active_count=0") != std::string::npos,
                "Successful reset must clear tracked outputs");

        driver.setSwitch(2, 33, true, "cleanup");
        driver.setAnalog(44, 0, true, "cleanup");
        driver.setYalkVoltage(1, 3.1, "cleanup");
        const unsigned resetsBeforeSafeStop = fake.resetCount;
        driver.safeStopAll();
        require(fake.resetCount == resetsBeforeSafeStop,
                "safeStopAll must never use full reset");
        require(driver.statusText().find("active_count=0") != std::string::npos,
                "safeStopAll did not release tracked outputs");

        driver.clearTrace();
        require(driver.traceText().empty(), "clearTrace did not clear the trace buffer");

        FakeIsd concurrentFake;
        IsdDriver concurrentDriver(concurrentFake.ops(), 128);
        std::vector<std::thread> threads;
        for (unsigned index = 0; index < 8; ++index) {
            threads.emplace_back([&, index] {
                concurrentDriver.setSwitch(2, 1 + index, true, "parallel");
            });
        }
        for (auto& thread : threads) thread.join();
        require(concurrentFake.maxInFlight.load() == 1,
                "ISD transport calls were not serialized by the driver");
        concurrentDriver.safeStopAll();

        std::cout << "ISD driver tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ISD driver test failed: " << error.what() << '\n';
        return 1;
    }
}
