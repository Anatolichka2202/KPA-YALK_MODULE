#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <functional>
#include <chrono>

namespace orbita::stand {

enum class ResourceState { Ready, Active, Safe, Error, Indeterminate };

inline const char* toString(ResourceState state) noexcept
{
    switch (state) {
    case ResourceState::Ready: return "READY";
    case ResourceState::Active: return "ACTIVE";
    case ResourceState::Safe: return "SAFE";
    case ResourceState::Error: return "ERROR";
    case ResourceState::Indeterminate: return "INDETERMINATE";
    }
    return "INDETERMINATE";
}

struct ResourceStateRecord {
    ResourceState state = ResourceState::Indeterminate;
    std::string owner;
    std::string reason;
    std::chrono::system_clock::time_point changedAt = std::chrono::system_clock::now();
};

struct ResourceStateEvent {
    std::string resource;
    ResourceState state = ResourceState::Indeterminate;
    std::string owner;
    std::string reason;
    std::chrono::system_clock::time_point at = std::chrono::system_clock::now();
};

class ResourceBusyError final : public std::runtime_error {
public:
    ResourceBusyError(std::string resource, std::string owner)
        : std::runtime_error(
              "Station resource is busy: " + resource + " (owner: " + owner + ")")
        , resource_(std::move(resource))
        , owner_(std::move(owner))
    {
    }

    const std::string& resource() const noexcept { return resource_; }
    const std::string& owner() const noexcept { return owner_; }

private:
    std::string resource_;
    std::string owner_;
};

class ResourceRecoveryRequiredError final : public std::runtime_error {
public:
    ResourceRecoveryRequiredError(std::string resource, ResourceStateRecord record)
        : std::runtime_error("Station resource requires recovery: " + resource
                             + " (state: " + toString(record.state)
                             + ", reason: " + record.reason + ")")
        , resource_(std::move(resource)), record_(std::move(record)) {}
    const std::string& resource() const noexcept { return resource_; }
    const ResourceStateRecord& record() const noexcept { return record_; }
private:
    std::string resource_;
    ResourceStateRecord record_;
};

// Process-local ownership guard for logical station resources.
//
// The manager is deliberately independent from any delivery, scenario or
// registrar domain. A higher execution layer acquires the logical resources
// needed by a run before active operations start and keeps the returned Lease
// alive until cleanup/safe-stop completes.
//
// Re-entrant acquisition by the same owner is reference-counted. This permits
// nested procedures/sub-scenarios to acquire a resource already owned by their
// run without accidentally releasing the outer ownership early.
class ResourceLeaseManager final {
public:
    class Lease final {
    public:
        Lease() = default;
        ~Lease() { reset(); }

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept
            : manager_(std::exchange(other.manager_, nullptr))
            , owner_(std::move(other.owner_))
            , resources_(std::move(other.resources_))
        {
        }

        Lease& operator=(Lease&& other) noexcept
        {
            if (this == &other) return *this;
            reset();
            manager_ = std::exchange(other.manager_, nullptr);
            owner_ = std::move(other.owner_);
            resources_ = std::move(other.resources_);
            return *this;
        }

        explicit operator bool() const noexcept { return manager_ != nullptr; }
        const std::string& owner() const noexcept { return owner_; }
        const std::set<std::string>& resources() const noexcept { return resources_; }

        void reset() noexcept
        {
            if (!manager_) return;
            manager_->release(owner_, resources_);
            manager_ = nullptr;
            owner_.clear();
            resources_.clear();
        }

    private:
        Lease(ResourceLeaseManager* manager,
              std::string owner,
              std::set<std::string> resources)
            : manager_(manager)
            , owner_(std::move(owner))
            , resources_(std::move(resources))
        {
        }

        ResourceLeaseManager* manager_ = nullptr;
        std::string owner_;
        std::set<std::string> resources_;

        friend class ResourceLeaseManager;
    };

    Lease acquire(std::string owner, std::set<std::string> resources)
    {
        if (owner.empty()) throw std::invalid_argument("Resource lease owner is empty");
        if (resources.empty()) throw std::invalid_argument("Resource lease set is empty");
        for (const auto& resource : resources) {
            if (resource.empty())
                throw std::invalid_argument("Resource lease contains an empty resource id");
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Validate the complete set first. Acquisition is atomic: a conflict on
        // one resource must not leave the owner holding the resources checked
        // before it.
        for (const auto& resource : resources) {
            const auto existing = leases_.find(resource);
            if (existing != leases_.end() && existing->second.owner != owner)
                throw ResourceBusyError(resource, existing->second.owner);
            const auto state = states_.find(resource);
            if (state != states_.end()
                && (state->second.state == ResourceState::Error
                    || state->second.state == ResourceState::Indeterminate))
                throw ResourceRecoveryRequiredError(resource, state->second);
        }

        for (const auto& resource : resources) {
            auto [entry, inserted] = leases_.try_emplace(resource, Entry{owner, 0});
            (void)inserted;
            ++entry->second.references;
            if (entry->second.references == 1)
                setStateLocked(resource, ResourceState::Active, owner, "run acquired resource");
        }

        return Lease(this, std::move(owner), std::move(resources));
    }

    std::optional<std::string> ownerOf(const std::string& resource) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto existing = leases_.find(resource);
        if (existing == leases_.end()) return std::nullopt;
        return existing->second.owner;
    }

    bool busy(const std::string& resource) const
    {
        return ownerOf(resource).has_value();
    }

    std::vector<std::string> resourcesOwnedBy(const std::string& owner) const
    {
        std::vector<std::string> result;
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [resource, entry] : leases_) {
            if (entry.owner == owner) result.push_back(resource);
        }
        return result;
    }

    std::optional<ResourceStateRecord> stateOf(const std::string& resource) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = states_.find(resource);
        if (found == states_.end()) return std::nullopt;
        return found->second;
    }

    std::vector<ResourceStateEvent> stateTrace() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return trace_;
    }

    std::map<std::string, ResourceStateRecord> states() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return states_;
    }

    // Called only with the run's safety-stop result. A resource with no
    // acknowledgement remains blocked until an explicit recovery succeeds.
    void recordSafeStop(const std::string& resource, const std::string& owner,
                        bool confirmed, bool operationFailed, std::string reason)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto state = !confirmed ? ResourceState::Indeterminate
            : operationFailed ? ResourceState::Error : ResourceState::Safe;
        setStateLocked(resource, state, owner, std::move(reason));
    }

    // Recovery must be an explicit operation with a named evidence/reference.
    // The callback runs while acquisition remains blocked; exceptions are a
    // failed recovery, never an implicit READY transition.
    bool recover(const std::string& resource, const std::string& recoveryId,
                 const std::function<bool()>& operation)
    {
        if (resource.empty() || recoveryId.empty() || !operation)
            throw std::invalid_argument("Recovery requires resource, evidence id and operation");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = states_.find(resource);
            if (leases_.count(resource))
                throw ResourceBusyError(resource, leases_.at(resource).owner);
            if (found == states_.end()
                || (found->second.state != ResourceState::Error
                    && found->second.state != ResourceState::Indeterminate))
                throw std::logic_error("Resource does not require recovery: " + resource);
            setStateLocked(resource, found->second.state, recoveryId,
                           "recovery started: " + recoveryId);
        }
        bool confirmed = false;
        try { confirmed = operation(); } catch (...) { confirmed = false; }
        std::lock_guard<std::mutex> lock(mutex_);
        setStateLocked(resource, confirmed ? ResourceState::Ready : ResourceState::Indeterminate,
                       recoveryId, confirmed ? "recovery confirmed: " + recoveryId
                                             : "recovery failed: " + recoveryId);
        return confirmed;
    }

private:
    struct Entry {
        std::string owner;
        std::size_t references = 0;
    };

    void setStateLocked(const std::string& resource, ResourceState state,
                        const std::string& owner, std::string reason)
    {
        ResourceStateRecord record{state, owner, std::move(reason),
                                   std::chrono::system_clock::now()};
        trace_.push_back({resource, record.state, record.owner, record.reason, record.changedAt});
        states_[resource] = std::move(record);
    }

    void release(const std::string& owner, const std::set<std::string>& resources) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& resource : resources) {
            const auto existing = leases_.find(resource);
            if (existing == leases_.end() || existing->second.owner != owner) continue;
            if (existing->second.references > 1) {
                --existing->second.references;
            } else {
                leases_.erase(existing);
            }
        }
    }

    mutable std::mutex mutex_;
    std::map<std::string, Entry> leases_;
    std::map<std::string, ResourceStateRecord> states_;
    std::vector<ResourceStateEvent> trace_;
};

} // namespace orbita::stand
