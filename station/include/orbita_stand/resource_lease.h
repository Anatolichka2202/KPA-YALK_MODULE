#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace orbita::stand {

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
        }

        for (const auto& resource : resources) {
            auto [entry, inserted] = leases_.try_emplace(resource, Entry{owner, 0});
            (void)inserted;
            ++entry->second.references;
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

private:
    struct Entry {
        std::string owner;
        std::size_t references = 0;
    };

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
};

} // namespace orbita::stand
