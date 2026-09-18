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

enum class ResourceKind {
    Switch,
    Analog,
    YalkOutput,
};

struct OwnedResource {
    ResourceKind kind = ResourceKind::Switch;
    unsigned type = 0;
    unsigned channel = 0;
    std::string owner;
    IsdOwnedCertainty certainty = IsdOwnedCertainty::PossiblyActive;
};

struct TraceEntry {
    std::uint64_t sequence = 0;
    std::int64_t timestampMs = 0;
    std::string owner;
    std::string operation;
    std::string detail;
    unsigned elapsedMs = 0;
    std::string result;
    IsdSessionState before = IsdSessionState::Operational;
    IsdSessionState after = IsdSessionState::Operational;
    std::string error;
};

bool sameResource(const OwnedResource& left, const OwnedResource& right)
{
    return left.kind == right.kind
        && left.type == right.type
        && left.channel == right.channel;
}

const char* kindName(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::Switch: return "switch";
    case ResourceKind::Analog: return "analog";
    case ResourceKind::YalkOutput: return "yalk_output";
    }
    return "unknown";
}

std::int64_t nowMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string clean(std::string value)
{
    for (char& ch : value) {
        if (ch == '\n' || ch == '\r' || ch == ';') ch = ' ';
    }
    return value;
}

} // namespace

const char* toString(IsdSessionState state) noexcept
{
    switch (state) {
    case IsdSessionState::Operational: return "operational";
    case IsdSessionState::Indeterminate: return "indeterminate";
    }
    return "indeterminate";
}

const char* toString(IsdOwnedCertainty certainty) noexcept
{
    switch (certainty) {
    case IsdOwnedCertainty::AcknowledgedActive: return "acknowledged_active";
    case IsdOwnedCertainty::PossiblyActive: return "possibly_active";
    }
    return "possibly_active";
}

struct IsdDriver::Impl {
    explicit Impl(IsdDriverOps value, std::size_t capacity)
        : ops(std::move(value)), traceCapacity(std::max<std::size_t>(1, capacity))
    {
        if (!ops.probe || !ops.serviceFullReset || !ops.setSwitch
            || !ops.setAnalog || !ops.setYalkVoltage || !ops.disableYalkOutput) {
            throw std::invalid_argument("ISD driver requires a complete operation set");
        }
    }

    IsdDriverOps ops;
    std::size_t traceCapacity = 256;
    mutable std::mutex mutex;
    IsdSessionState state = IsdSessionState::Operational;
    std::vector<OwnedResource> owned;
    std::deque<TraceEntry> trace;
    std::uint64_t nextSequence = 1;

    void appendTrace(TraceEntry entry)
    {
        entry.sequence = nextSequence++;
        entry.timestampMs = nowMilliseconds();
        trace.push_back(std::move(entry));
        while (trace.size() > traceCapacity) trace.pop_front();
    }

    auto findResource(const OwnedResource& resource)
    {
        return std::find_if(owned.begin(), owned.end(),
            [&](const OwnedResource& item) { return sameResource(item, resource); });
    }

    auto findResource(const OwnedResource& resource) const
    {
        return std::find_if(owned.cbegin(), owned.cend(),
            [&](const OwnedResource& item) { return sameResource(item, resource); });
    }

    void ensureOwnerMayTouch(const OwnedResource& resource) const
    {
        const auto found = findResource(resource);
        if (found != owned.cend() && found->owner != resource.owner) {
            throw std::runtime_error(
                "ISD ownership conflict: resource belongs to " + found->owner
                + ", requested by " + resource.owner);
        }
    }

    void requireActivationAllowed() const
    {
        if (state != IsdSessionState::Operational) {
            throw std::runtime_error(
                "ISD session is indeterminate; new active mutations are blocked until targeted cleanup or explicit service recovery");
        }
    }

    void markPossiblyActive(OwnedResource resource)
    {
        const auto found = findResource(resource);
        if (found != owned.end()) owned.erase(found);
        resource.certainty = IsdOwnedCertainty::PossiblyActive;
        owned.push_back(std::move(resource));
    }

    void markAcknowledged(const OwnedResource& resource)
    {
        const auto found = findResource(resource);
        if (found != owned.end()) found->certainty = IsdOwnedCertainty::AcknowledgedActive;
    }

    void markInactive(const OwnedResource& resource)
    {
        owned.erase(std::remove_if(owned.begin(), owned.end(),
            [&](const OwnedResource& item) { return sameResource(item, resource); }), owned.end());
    }

    void maybeResolveAfterCleanup()
    {
        // The session state is only about unresolved mutations issued by this
        // process.  If every owned/possibly-active route has been positively
        // driven OFF, there is no remaining unresolved process-owned mutation.
        // This says nothing about unrelated physical routes in the ISD.
        if (owned.empty()) state = IsdSessionState::Operational;
    }

    template<typename Function>
    void activate(OwnedResource resource, std::string operation,
                  std::string detail, Function&& function)
    {
        requireActivationAllowed();
        ensureOwnerMayTouch(resource);
        const auto before = state;

        // Ownership is recorded BEFORE the request.  A timeout/connection close
        // cannot prove that the physical ON did not happen.
        markPossiblyActive(resource);
        const auto started = std::chrono::steady_clock::now();
        try {
            function();
            markAcknowledged(resource);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, resource.owner, std::move(operation), std::move(detail),
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "ack", before, state, {}});
        } catch (const std::exception& error) {
            state = IsdSessionState::Indeterminate;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, resource.owner, std::move(operation), std::move(detail),
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "indeterminate", before, state, clean(error.what())});
            throw;
        } catch (...) {
            state = IsdSessionState::Indeterminate;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, resource.owner, std::move(operation), std::move(detail),
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "indeterminate", before, state, "unknown error"});
            throw;
        }
    }

    template<typename Function>
    void deactivate(const OwnedResource& resource, std::string operation,
                    std::string detail, Function&& function)
    {
        ensureOwnerMayTouch(resource);
        const auto before = state;
        const auto started = std::chrono::steady_clock::now();
        try {
            function();
            markInactive(resource);
            maybeResolveAfterCleanup();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, resource.owner, std::move(operation), std::move(detail),
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "ack", before, state, {}});
        } catch (const std::exception& error) {
            state = IsdSessionState::Indeterminate;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, resource.owner, std::move(operation), std::move(detail),
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "indeterminate", before, state, clean(error.what())});
            throw;
        } catch (...) {
            state = IsdSessionState::Indeterminate;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, resource.owner, std::move(operation), std::move(detail),
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "indeterminate", before, state, "unknown error"});
            throw;
        }
    }

    bool cleanupOne(const OwnedResource& resource) noexcept
    {
        const auto before = state;
        const auto started = std::chrono::steady_clock::now();
        try {
            if (resource.kind == ResourceKind::Switch) {
                ops.setSwitch(resource.type, resource.channel, false);
            } else if (resource.kind == ResourceKind::Analog) {
                ops.setAnalog(resource.channel, 0, false);
            } else {
                ops.disableYalkOutput(resource.channel);
            }
            markInactive(resource);
            maybeResolveAfterCleanup();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, resource.owner, "release",
                std::string("kind=") + kindName(resource.kind)
                    + ",type=" + std::to_string(resource.type)
                    + ",channel=" + std::to_string(resource.channel),
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "ack", before, state, {}});
            return true;
        } catch (const std::exception& error) {
            state = IsdSessionState::Indeterminate;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, resource.owner, "release",
                std::string("kind=") + kindName(resource.kind)
                    + ",type=" + std::to_string(resource.type)
                    + ",channel=" + std::to_string(resource.channel),
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "indeterminate", before, state, clean(error.what())});
            return false;
        } catch (...) {
            state = IsdSessionState::Indeterminate;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            appendTrace({0, 0, resource.owner, "release",
                std::string("kind=") + kindName(resource.kind)
                    + ",type=" + std::to_string(resource.type)
                    + ",channel=" + std::to_string(resource.channel),
                static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
                "indeterminate", before, state, "unknown error"});
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
    const auto before = impl_->state;
    const auto started = std::chrono::steady_clock::now();
    try {
        const auto response = impl_->ops.probe();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        impl_->appendTrace({0, 0, "system", "probe", "http_connectivity_only=1",
            static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
            "ack", before, impl_->state, {}});
        return response;
    } catch (const std::exception& error) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        impl_->appendTrace({0, 0, "system", "probe", "http_connectivity_only=1",
            static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
            "failed", before, impl_->state, clean(error.what())});
        throw;
    }
}

void IsdDriver::serviceFullReset(const std::string& owner)
{
    if (owner.empty()) throw std::invalid_argument("ISD service operation requires owner");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto before = impl_->state;
    const auto started = std::chrono::steady_clock::now();
    try {
        // Exactly one underlying operation.  Retry policy is intentionally not
        // implemented here or in the HTTP transport.
        impl_->ops.serviceFullReset();
        impl_->owned.clear();
        impl_->state = IsdSessionState::Operational;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        impl_->appendTrace({0, 0, owner, "service_full_reset",
            "type=4,global_readback=unavailable",
            static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
            "ack", before, impl_->state, {}});
    } catch (const std::exception& error) {
        impl_->state = IsdSessionState::Indeterminate;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        impl_->appendTrace({0, 0, owner, "service_full_reset",
            "type=4,global_readback=unavailable",
            static_cast<unsigned>(std::max<std::int64_t>(0, elapsed)),
            "indeterminate", before, impl_->state, clean(error.what())});
        throw;
    }
}

void IsdDriver::setSwitch(unsigned type, unsigned channel, bool enabled,
                          const std::string& owner)
{
    if (!type || !channel) throw std::invalid_argument("ISD type/channel starts at one");
    if (owner.empty()) throw std::invalid_argument("ISD switch requires owner");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    OwnedResource resource{ResourceKind::Switch, type, channel, owner,
                           IsdOwnedCertainty::PossiblyActive};
    const std::string detail = "type=" + std::to_string(type)
        + ",channel=" + std::to_string(channel)
        + ",value=" + (enabled ? "1" : "0");
    if (enabled) {
        impl_->activate(resource, "switch", detail,
            [&] { impl_->ops.setSwitch(type, channel, true); });
    } else {
        impl_->deactivate(resource, "switch", detail,
            [&] { impl_->ops.setSwitch(type, channel, false); });
    }
}

void IsdDriver::setAnalog(unsigned channel, unsigned value, bool enabled,
                          const std::string& owner)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    if (owner.empty()) throw std::invalid_argument("ISD analog operation requires owner");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    OwnedResource resource{ResourceKind::Analog, 1, channel, owner,
                           IsdOwnedCertainty::PossiblyActive};
    const std::string detail = "type=1,channel=" + std::to_string(channel)
        + ",value=" + std::to_string(value)
        + ",work=" + (enabled ? "1" : "0");
    if (enabled) {
        impl_->activate(resource, "analog", detail,
            [&] { impl_->ops.setAnalog(channel, value, true); });
    } else {
        impl_->deactivate(resource, "analog", detail,
            [&] { impl_->ops.setAnalog(channel, value, false); });
    }
}

void IsdDriver::setYalkVoltage(unsigned channel, double volts,
                               const std::string& owner)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    if (owner.empty()) throw std::invalid_argument("ISD YALK output requires owner");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    OwnedResource resource{ResourceKind::YalkOutput, 5, channel, owner,
                           IsdOwnedCertainty::PossiblyActive};
    std::ostringstream detail;
    detail << "type=5,channel=" << channel << ",volts=" << volts << ",work=1,bus=1";
    impl_->activate(resource, "yalk_voltage", detail.str(),
        [&] { impl_->ops.setYalkVoltage(channel, volts); });
}

void IsdDriver::disableYalkOutput(unsigned channel, const std::string& owner)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    if (owner.empty()) throw std::invalid_argument("ISD YALK output requires owner");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    OwnedResource resource{ResourceKind::YalkOutput, 5, channel, owner,
                           IsdOwnedCertainty::PossiblyActive};
    impl_->deactivate(resource, "yalk_output_off",
        "type=1,channel=" + std::to_string(channel) + ",work=off_sequence",
        [&] { impl_->ops.disableYalkOutput(channel); });
}

void IsdDriver::releaseOwner(const std::string& owner)
{
    if (owner.empty()) throw std::invalid_argument("ISD release requires owner");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    bool failed = false;
    const auto snapshot = impl_->owned;
    for (auto iterator = snapshot.rbegin(); iterator != snapshot.rend(); ++iterator) {
        if (iterator->owner == owner && !impl_->cleanupOne(*iterator)) failed = true;
    }
    if (failed) {
        throw std::runtime_error(
            "ISD targeted release left one or more routes indeterminate");
    }
    impl_->maybeResolveAfterCleanup();
}

void IsdDriver::safeStopAll() noexcept
{
    if (!impl_) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto snapshot = impl_->owned;
    for (auto iterator = snapshot.rbegin(); iterator != snapshot.rend(); ++iterator) {
        (void)impl_->cleanupOne(*iterator);
    }
    impl_->maybeResolveAfterCleanup();
}

IsdSessionState IsdDriver::sessionState() const noexcept
{
    if (!impl_) return IsdSessionState::Indeterminate;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state;
}

std::size_t IsdDriver::ownedCount() const noexcept
{
    if (!impl_) return 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->owned.size();
}

std::string IsdDriver::statusText() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::ostringstream out;
    out << "status=ready\n"
        << "session_state=" << toString(impl_->state) << "\n"
        << "global_hardware_state=not_readable\n"
        << "owned_count=" << impl_->owned.size() << "\n";
    for (std::size_t index = 0; index < impl_->owned.size(); ++index) {
        const auto& item = impl_->owned[index];
        out << "owned." << index << "=owner=" << item.owner
            << ",kind=" << kindName(item.kind)
            << ",type=" << item.type
            << ",channel=" << item.channel
            << ",certainty=" << toString(item.certainty) << "\n";
    }
    return out.str();
}

std::string IsdDriver::traceText() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::ostringstream out;
    for (const auto& entry : impl_->trace) {
        out << "sequence=" << entry.sequence
            << ";timestamp_ms=" << entry.timestampMs
            << ";owner=" << entry.owner
            << ";operation=" << entry.operation
            << ";" << entry.detail
            << ";elapsed_ms=" << entry.elapsedMs
            << ";result=" << entry.result
            << ";state_before=" << toString(entry.before)
            << ";state_after=" << toString(entry.after)
            << ";error=" << clean(entry.error)
            << '\n';
    }
    return out.str();
}

void IsdDriver::clearTrace()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->trace.clear();
}

} // namespace orbita::stand
