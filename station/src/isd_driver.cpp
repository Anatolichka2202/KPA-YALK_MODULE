#include "orbita_stand/isd_driver.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace orbita::stand {
namespace {

enum class ActiveKind {
    Switch,
    Analog,
    YalkOutput
};

struct ActiveAction {
    ActiveKind kind = ActiveKind::Switch;
    unsigned type = 0;
    unsigned channel = 0;
    std::string owner;
};

struct TraceEntry {
    std::uint64_t sequence = 0;
    std::int64_t timeMilliseconds = 0;
    std::string owner;
    std::string operation;
    std::string detail;
    unsigned elapsedMilliseconds = 0;
    std::string result;
    IsdDriverState stateBefore = IsdDriverState::Known;
    IsdDriverState stateAfter = IsdDriverState::Known;
    std::string message;
};

bool sameResource(const ActiveAction& left, const ActiveAction& right)
{
    return left.kind == right.kind && left.type == right.type
        && left.channel == right.channel;
}

const char* kindName(ActiveKind kind)
{
    switch (kind) {
    case ActiveKind::Switch: return "switch";
    case ActiveKind::Analog: return "analog";
    case ActiveKind::YalkOutput: return "yalk_output";
    }
    return "unknown";
}

std::string clean(std::string value)
{
    for (char& ch : value) {
        if (ch == '\n' || ch == '\r' || ch == ';') ch = ' ';
    }
    return value;
}

std::int64_t nowMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

const char* toString(IsdDriverState state) noexcept
{
    switch (state) {
    case IsdDriverState::Known: return "known";
    case IsdDriverState::Unknown: return "unknown";
    case IsdDriverState::Recovering: return "recovering";
    }
    return "unknown";
}

struct IsdDriver::Impl {
    explicit Impl(IsdDriverOps value, std::size_t capacity)
        : ops(std::move(value)), traceCapacity(std::max<std::size_t>(1, capacity))
    {
        if (!ops.probe || !ops.reset || !ops.prepareYalk || !ops.setSwitch
            || !ops.setAnalog || !ops.setYalkVoltage || !ops.disableYalkOutput) {
            throw std::invalid_argument("ISD driver requires a complete transport operation set");
        }
    }

    IsdDriverOps ops;
    std::size_t traceCapacity = 256;
    mutable std::mutex mutex;
    IsdDriverState state = IsdDriverState::Known;
    std::vector<ActiveAction> active;
    std::deque<TraceEntry> trace;
    std::uint64_t nextSequence = 1;

    void appendTrace(TraceEntry entry)
    {
        entry.sequence = nextSequence++;
        entry.timeMilliseconds = nowMilliseconds();
        trace.push_back(std::move(entry));
        while (trace.size() > traceCapacity) trace.pop_front();
    }

    void requireKnownForMutation() const
    {
        if (state != IsdDriverState::Known) {
            throw std::runtime_error(
                "ISD state is UNKNOWN; perform explicit reset/recovery before new active commands");
        }
    }

    auto findResource(const ActiveAction& action)
    {
        return std::find_if(active.begin(), active.end(),
            [&](const ActiveAction& item) { return sameResource(item, action); });
    }

    auto findResource(const ActiveAction& action) const
    {
        return std::find_if(active.cbegin(), active.cend(),
            [&](const ActiveAction& item) { return sameResource(item, action); });
    }

    void ensureOwnerMayTouch(const ActiveAction& action) const
    {
        const auto found = findResource(action);
        if (found != active.end() && found->owner != action.owner) {
            throw std::runtime_error(
                "ISD resource ownership conflict: resource is owned by " + found->owner
                + ", requested by " + action.owner);
        }
    }

    void markActive(ActiveAction action)
    {
        const auto found = findResource(action);
        if (found != active.end()) active.erase(found);
        active.push_back(std::move(action));
    }

    void markInactive(const ActiveAction& action)
    {
        active.erase(std::remove_if(active.begin(), active.end(),
            [&](const ActiveAction& item) { return sameResource(item, action); }), active.end());
    }

    template<typename Function, typename Success>
    void executeMutation(const std::string& owner, const std::string& operation,
                         const std::string& detail, Function&& function,
                         Success&& success)
    {
        requireKnownForMutation();
        const IsdDriverState before = state;
        const auto started = std::chrono::steady_clock::now();
        try {
            function();
            success();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, owner, operation, detail,
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "ack", before, state, "OK"});
        } catch (const std::exception& error) {
            state = IsdDriverState::Unknown;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, owner, operation, detail,
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "indeterminate", before, state, clean(error.what())});
            throw;
        }
    }

    bool deactivate(const ActiveAction& action, bool recordFailure)
    {
        const IsdDriverState before = state;
        const auto started = std::chrono::steady_clock::now();
        try {
            if (action.kind == ActiveKind::Switch) {
                ops.setSwitch(action.type, action.channel, false);
            } else if (action.kind == ActiveKind::Analog) {
                ops.setAnalog(action.channel, 0, false);
            } else {
                ops.disableYalkOutput(action.channel);
            }
            markInactive(action);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, action.owner, "release",
                std::string("kind=") + kindName(action.kind)
                    + ",type=" + std::to_string(action.type)
                    + ",channel=" + std::to_string(action.channel),
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "ack", before, state, "OK"});
            return true;
        } catch (const std::exception& error) {
            state = IsdDriverState::Unknown;
            if (recordFailure) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                appendTrace({0, 0, action.owner, "release",
                    std::string("kind=") + kindName(action.kind)
                        + ",type=" + std::to_string(action.type)
                        + ",channel=" + std::to_string(action.channel),
                    static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                    "indeterminate", before, state, clean(error.what())});
            }
            return false;
        } catch (...) {
            state = IsdDriverState::Unknown;
            if (recordFailure) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                appendTrace({0, 0, action.owner, "release",
                    std::string("kind=") + kindName(action.kind)
                        + ",type=" + std::to_string(action.type)
                        + ",channel=" + std::to_string(action.channel),
                    static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                    "indeterminate", before, state, "unknown error"});
            }
            return false;
        }
    }
};

IsdDriver::IsdDriver(IsdDriverOps ops, std::size_t traceCapacity)
    : impl_(std::make_unique<Impl>(std::move(ops), traceCapacity)) {}

IsdDriver::~IsdDriver() = default;

std::string IsdDriver::probe()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const IsdDriverState before = impl_->state;
    const auto started = std::chrono::steady_clock::now();
    try {
        const auto response = impl_->ops.probe();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        impl_->appendTrace({0, 0, "system", "probe", {},
            static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
            "ack", before, impl_->state, clean(response)});
        return response;
    } catch (const std::exception& error) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        impl_->appendTrace({0, 0, "system", "probe", {},
            static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
            "failed", before, impl_->state, clean(error.what())});
        throw;
    }
}

void IsdDriver::reset(const std::string& owner)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const IsdDriverState before = impl_->state;
    impl_->state = IsdDriverState::Recovering;
    const auto started = std::chrono::steady_clock::now();
    try {
        impl_->ops.reset();
        impl_->active.clear();
        impl_->state = IsdDriverState::Known;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        impl_->appendTrace({0, 0, owner, "reset", "full_reset=1",
            static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
            "ack", before, impl_->state, "OK"});
    } catch (const std::exception& error) {
        impl_->state = IsdDriverState::Unknown;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        impl_->appendTrace({0, 0, owner, "reset", "full_reset=1",
            static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
            "indeterminate", before, impl_->state, clean(error.what())});
        throw;
    }
}

void IsdDriver::prepareYalk(const std::string& owner)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const IsdDriverState before = impl_->state;
    impl_->state = IsdDriverState::Recovering;
    const auto started = std::chrono::steady_clock::now();
    try {
        impl_->ops.prepareYalk();
        impl_->active.clear();
        impl_->state = IsdDriverState::Known;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        impl_->appendTrace({0, 0, owner, "yalk_prepare", "reset_then_prepare=1",
            static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
            "ack", before, impl_->state, "OK"});
    } catch (const std::exception& error) {
        impl_->state = IsdDriverState::Unknown;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        impl_->appendTrace({0, 0, owner, "yalk_prepare", "reset_then_prepare=1",
            static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
            "indeterminate", before, impl_->state, clean(error.what())});
        throw;
    }
}

void IsdDriver::setSwitch(unsigned type, unsigned channel, bool enabled,
                          const std::string& owner)
{
    if (!type || !channel) throw std::invalid_argument("ISD type/channel starts at one");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ActiveAction action{ActiveKind::Switch, type, channel, owner};
    impl_->ensureOwnerMayTouch(action);
    impl_->executeMutation(owner, "switch",
        "type=" + std::to_string(type) + ",channel=" + std::to_string(channel)
            + ",enabled=" + (enabled ? "1" : "0"),
        [&] { impl_->ops.setSwitch(type, channel, enabled); },
        [&] { enabled ? impl_->markActive(action) : impl_->markInactive(action); });
}

void IsdDriver::setAnalog(unsigned channel, unsigned value, bool enabled,
                          const std::string& owner)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ActiveAction action{ActiveKind::Analog, 1, channel, owner};
    impl_->ensureOwnerMayTouch(action);
    impl_->executeMutation(owner, "analog",
        "channel=" + std::to_string(channel) + ",value=" + std::to_string(value)
            + ",enabled=" + (enabled ? "1" : "0"),
        [&] { impl_->ops.setAnalog(channel, value, enabled); },
        [&] { enabled ? impl_->markActive(action) : impl_->markInactive(action); });
}

void IsdDriver::setYalkVoltage(unsigned channel, double volts,
                               const std::string& owner)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ActiveAction action{ActiveKind::YalkOutput, 5, channel, owner};
    impl_->ensureOwnerMayTouch(action);
    std::ostringstream detail;
    detail << "channel=" << channel << ",volts=" << volts;
    impl_->executeMutation(owner, "yalk_set_voltage", detail.str(),
        [&] { impl_->ops.setYalkVoltage(channel, volts); },
        [&] { impl_->markActive(action); });
}

void IsdDriver::disableYalkOutput(unsigned channel, const std::string& owner)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ActiveAction action{ActiveKind::YalkOutput, 5, channel, owner};
    impl_->ensureOwnerMayTouch(action);
    impl_->executeMutation(owner, "yalk_output_off",
        "channel=" + std::to_string(channel),
        [&] { impl_->ops.disableYalkOutput(channel); },
        [&] { impl_->markInactive(action); });
}

void IsdDriver::releaseOwner(const std::string& owner)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    bool failed = false;
    const auto snapshot = impl_->active;
    for (auto iterator = snapshot.rbegin(); iterator != snapshot.rend(); ++iterator) {
        if (iterator->owner == owner && !impl_->deactivate(*iterator, true)) failed = true;
    }
    if (failed) {
        throw std::runtime_error("ISD release completed with one or more indeterminate outputs");
    }
}

void IsdDriver::safeStopAll() noexcept
{
    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto snapshot = impl_->active;
        for (auto iterator = snapshot.rbegin(); iterator != snapshot.rend(); ++iterator) {
            (void)impl_->deactivate(*iterator, true);
        }
    } catch (...) {
        // Safe stop is best-effort by ABI contract. Never replace it with full reset:
        // live ISD type=4 may block when an internal RS-485 module does not answer.
    }
}

IsdDriverState IsdDriver::state() const noexcept
{
    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->state;
    } catch (...) {
        return IsdDriverState::Unknown;
    }
}

std::string IsdDriver::statusText() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::ostringstream output;
    output << "state=" << toString(impl_->state) << '\n';
    output << "active_count=" << impl_->active.size() << '\n';
    output << "trace_entries=" << impl_->trace.size() << '\n';
    output << "physical_verification=not_available\n";
    for (std::size_t index = 0; index < impl_->active.size(); ++index) {
        const auto& action = impl_->active[index];
        output << "active." << index << "=owner:" << action.owner
               << ",kind:" << kindName(action.kind)
               << ",type:" << action.type
               << ",channel:" << action.channel << '\n';
    }
    return output.str();
}

std::string IsdDriver::traceText() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::ostringstream output;
    for (const auto& entry : impl_->trace) {
        output << "seq=" << entry.sequence
               << ";time_ms=" << entry.timeMilliseconds
               << ";owner=" << clean(entry.owner)
               << ";operation=" << clean(entry.operation)
               << ";detail=" << clean(entry.detail)
               << ";elapsed_ms=" << entry.elapsedMilliseconds
               << ";result=" << entry.result
               << ";state_before=" << toString(entry.stateBefore)
               << ";state_after=" << toString(entry.stateAfter)
               << ";physical=not_verified"
               << ";message=" << clean(entry.message) << '\n';
    }
    return output.str();
}

void IsdDriver::clearTrace()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->trace.clear();
}

} // namespace orbita::stand
