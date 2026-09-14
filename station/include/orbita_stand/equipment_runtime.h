#pragma once

#include "orbita_stand/scenario.h"

#include <map>
#include <functional>
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

    // Legacy default routing by capability. Existing scenarios use this path.
    void bind(std::string capability, std::shared_ptr<EquipmentDevice> device);
    void bind(std::string capability, InvokeFunction invoke,
              SafeStopFunction safeStop = {});

    // Role/resource routing. Several resources may provide the same capability
    // without overwriting each other. Resource id is the stable logical role;
    // capability remains the operation contract implemented by that resource.
    void bindResource(std::string resourceId, std::shared_ptr<EquipmentDevice> device);
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

    void clear();
    bool hasCapability(const std::string& capability) const override;
    std::string invoke(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override;
    void safeStopAll() noexcept override;
    std::vector<std::string> capabilities() const;

private:
    struct BuiltinBinding {
        InvokeFunction invoke;
        SafeStopFunction safeStop;
    };
    struct BuiltinResourceBinding {
        std::set<std::string> capabilities;
        ResourceInvokeFunction invoke;
        SafeStopFunction safeStop;
    };

    std::map<std::string, std::shared_ptr<EquipmentDevice>> bindings_;
    std::map<std::string, BuiltinBinding> builtinBindings_;
    std::map<std::string, std::shared_ptr<EquipmentDevice>> resourceBindings_;
    std::map<std::string, BuiltinResourceBinding> builtinResourceBindings_;
};

std::string encodePluginArguments(const std::map<std::string, std::string>& arguments);
std::map<std::string, std::string> decodePluginArguments(const std::string& value);

} // namespace orbita::stand
