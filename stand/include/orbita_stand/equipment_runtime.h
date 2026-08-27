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

class EquipmentRegistry final : public ICapabilityProvider {
public:
    using InvokeFunction = std::function<std::string(
        const std::string& operation,
        const std::map<std::string, std::string>& arguments)>;
    using SafeStopFunction = std::function<void()>;

    void bind(std::string capability, std::shared_ptr<EquipmentDevice> device);
    // Встроенные источники приложения (например, liborbita/E20) используют тот
    // же capability-контракт, но не обязаны притворяться DLL-плагином.
    void bind(std::string capability, InvokeFunction invoke,
              SafeStopFunction safeStop = {});
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
    std::map<std::string, std::shared_ptr<EquipmentDevice>> bindings_;
    std::map<std::string, BuiltinBinding> builtinBindings_;
};

std::string encodePluginArguments(const std::map<std::string, std::string>& arguments);
std::map<std::string, std::string> decodePluginArguments(const std::string& value);

} // namespace orbita::stand
