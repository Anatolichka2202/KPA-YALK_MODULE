#pragma once

#include "orbita_stand/scenario.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace orbita::stand {

struct PluginDescriptor {
    std::string id;
    std::string displayName;
    std::set<std::string> capabilities;
    std::string libraryPath;
};

class EquipmentDevice final {
public:
    ~EquipmentDevice();
    EquipmentDevice(const EquipmentDevice&) = delete;
    EquipmentDevice& operator=(const EquipmentDevice&) = delete;

    const std::string& instanceId() const noexcept;
    const PluginDescriptor& descriptor() const noexcept;
    std::string invoke(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments = {});
    void cancel() noexcept;
    void safeStop() noexcept;

private:
    struct Impl;
    explicit EquipmentDevice(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class EquipmentPluginManager;
};

class EquipmentPluginManager final {
public:
    EquipmentPluginManager();
    ~EquipmentPluginManager();
    EquipmentPluginManager(const EquipmentPluginManager&) = delete;
    EquipmentPluginManager& operator=(const EquipmentPluginManager&) = delete;

    void loadDirectory(const std::string& path);
    const std::vector<PluginDescriptor>& plugins() const noexcept;
    std::shared_ptr<EquipmentDevice> createDevice(
        const std::string& pluginId,
        const std::string& instanceId,
        const std::map<std::string, std::string>& configuration);
    const std::vector<std::string>& diagnostics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct EquipmentResourceDescriptor {
    std::string id;
    std::set<std::string> capabilities;
    bool builtin = false;
};

class EquipmentRegistry final : public ICapabilityProvider {
public:
    using InvokeFunction = std::function<std::string(
        const std::string& operation,
        const std::map<std::string, std::string>& arguments)>;
    using ResourceInvokeFunction = std::function<std::string(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments)>;
    using SafeStopFunction = std::function<void()>;
    using ResourceInvokeNext = std::function<std::string()>;
    using ResourceInvokeInterceptor = std::function<std::string(
        const std::string& resourceId,
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments,
        const ResourceInvokeNext& next)>;

    // Compatibility routing while old procedures still address a capability
    // without a delivery role. New physical scenarios must use resources.
    void bind(std::string capability, std::shared_ptr<EquipmentDevice> device);
    void bind(std::string capability, InvokeFunction invoke,
              SafeStopFunction safeStop = {});

    // Canonical station routing: role -> capability -> operation.
    void bindResource(std::string resourceId, std::shared_ptr<EquipmentDevice> device);
    void bindResource(
        std::string resourceId,
        std::set<std::string> capabilities,
        std::shared_ptr<EquipmentDevice> device);
    void bindResource(
        std::string resourceId,
        std::set<std::string> capabilities,
        ResourceInvokeFunction invoke,
        SafeStopFunction safeStop = {});
    bool hasResource(const std::string& resourceId) const;
    bool resourceHasCapability(
        const std::string& resourceId,
        const std::string& capability) const override;
    std::string invokeResource(
        const std::string& resourceId,
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments = {}) override;
    std::vector<EquipmentResourceDescriptor> resources() const;

    // Product/delivery code may wrap one logical resource without teaching the
    // generic registry about a concrete protocol. Typical uses are operator
    // recovery, audit/evidence hooks and delivery-specific safety policy.
    // The interceptor receives `next`, which performs the original provider
    // invocation exactly once when called.
    void setResourceInvokeInterceptor(
        std::string resourceId,
        ResourceInvokeInterceptor interceptor);
    void clearResourceInvokeInterceptor(const std::string& resourceId) noexcept;

    // Readiness can rebuild physical bindings without discarding station-level
    // services such as catalog/manual input/protocol facades.
    void clearPhysical() noexcept;
    void clear();
    bool hasCapability(const std::string& capability) const override;
    std::string invoke(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override;
    void safeStopAll() noexcept override;
    std::vector<std::string> capabilities() const;

private:
    struct DefaultBinding {
        std::shared_ptr<EquipmentDevice> device;
        InvokeFunction invoke;
        SafeStopFunction safeStop;
    };
    struct ResourceBinding {
        std::set<std::string> capabilities;
        std::shared_ptr<EquipmentDevice> device;
        ResourceInvokeFunction invoke;
        SafeStopFunction safeStop;
    };

    std::map<std::string, DefaultBinding> defaults_;
    std::map<std::string, ResourceBinding> resources_;
    std::map<std::string, ResourceInvokeInterceptor> resourceInterceptors_;
};

std::string encodePluginArguments(const std::map<std::string, std::string>& arguments);
std::map<std::string, std::string> decodePluginArguments(const std::string& value);

} // namespace orbita::stand
