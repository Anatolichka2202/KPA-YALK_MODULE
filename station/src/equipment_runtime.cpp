#include "orbita_stand/equipment_runtime.h"
#include "orbita_stand/equipment_plugin.h"

#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringList>

#include <algorithm>
#include <array>
#include <atomic>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace orbita::stand {
namespace {

std::set<std::string> splitCapabilities(const char* value)
{
    std::set<std::string> result;
    if (!value) return result;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ';')) if (!item.empty()) result.insert(item);
    return result;
}

std::string escaped(std::string value)
{
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '\n' || character == '=') result.push_back('\\');
        result.push_back(character == '\n' ? 'n' : character);
    }
    return result;
}

std::string bufferText(const char* data, std::size_t capacity, std::size_t size)
{
    return {data, std::min(size, capacity)};
}

struct LoadedPlugin {
    std::shared_ptr<QLibrary> library;
    const orbita_equipment_api_v1* api = nullptr;
    PluginDescriptor descriptor;
};

} // namespace

std::string encodePluginArguments(const std::map<std::string, std::string>& arguments)
{
    std::size_t expectedSize = 0;
    for (const auto& [key, value] : arguments) {
        expectedSize += key.size() + value.size() + 2;
    }

    std::string output;
    output.reserve(expectedSize);
    for (const auto& [key, value] : arguments) {
        output += escaped(key);
        output += '=';
        output += escaped(value);
        output += '\n';
    }
    return output;
}

std::map<std::string, std::string> decodePluginArguments(const std::string& value)
{
    std::map<std::string, std::string> result;
    std::string key;
    std::string item;
    bool readingKey = true;
    bool escape = false;
    auto flush = [&]() {
        if (!key.empty()) result[key] = item;
        key.clear();
        item.clear();
        readingKey = true;
    };
    for (const char character : value) {
        if (escape) {
            (readingKey ? key : item).push_back(character == 'n' ? '\n' : character);
            escape = false;
        } else if (character == '\\') {
            escape = true;
        } else if (readingKey && character == '=') {
            readingKey = false;
        } else if (character == '\n') {
            flush();
        } else {
            (readingKey ? key : item).push_back(character);
        }
    }
    flush();
    return result;
}

struct EquipmentDevice::Impl {
    std::shared_ptr<LoadedPlugin> plugin;
    std::string instanceId;
    void* instance = nullptr;
    std::atomic_bool safeStopped{false};
};

EquipmentDevice::EquipmentDevice(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

EquipmentDevice::~EquipmentDevice()
{
    if (!impl_ || !impl_->instance || !impl_->plugin || !impl_->plugin->api) return;
    safeStop();
    impl_->plugin->api->destroy(impl_->instance);
}

const std::string& EquipmentDevice::instanceId() const noexcept { return impl_->instanceId; }
const PluginDescriptor& EquipmentDevice::descriptor() const noexcept { return impl_->plugin->descriptor; }

std::string EquipmentDevice::invoke(
    const std::string& capability,
    const std::string& operation,
    const std::map<std::string, std::string>& arguments)
{
    if (!impl_->plugin->descriptor.capabilities.count(capability)) {
        throw std::invalid_argument("Plugin " + impl_->plugin->descriptor.id
            + " does not provide capability " + capability);
    }

    // Any operation may have changed the physical state. The next safe-stop
    // must therefore reach the plugin even if the device was safe before it.
    impl_->safeStopped.store(false, std::memory_order_relaxed);

    const std::string request = encodePluginArguments(arguments);
    std::array<char, 4096> local{};
    orbita_plugin_buffer_v1 response{local.data(), local.size(), 0};
    auto status = impl_->plugin->api->invoke(
        impl_->instance, capability.c_str(), operation.c_str(), request.c_str(), &response);

    std::string text;
    if (status == ORBITA_PLUGIN_BUFFER_TOO_SMALL && response.size > local.size()) {
        std::vector<char> extended(response.size);
        response = {extended.data(), extended.size(), 0};
        status = impl_->plugin->api->invoke(
            impl_->instance, capability.c_str(), operation.c_str(), request.c_str(), &response);
        text = bufferText(extended.data(), extended.size(), response.size);
    } else {
        text = bufferText(local.data(), local.size(), response.size);
    }

    if (status != ORBITA_PLUGIN_OK) {
        throw std::runtime_error(text.empty()
            ? "Equipment plugin operation failed with status " + std::to_string(status)
            : text);
    }
    return text;
}

void EquipmentDevice::cancel() noexcept
{
    if (impl_ && impl_->instance) impl_->plugin->api->cancel(impl_->instance);
}

void EquipmentDevice::safeStop() noexcept
{
    if (!impl_ || !impl_->instance || !impl_->plugin || !impl_->plugin->api) return;
    bool expected = false;
    if (!impl_->safeStopped.compare_exchange_strong(
            expected, true, std::memory_order_relaxed)) {
        return;
    }
    try {
        impl_->plugin->api->safe_stop(impl_->instance);
    } catch (...) {
        // A failed stop must remain retryable by the next safety boundary.
        impl_->safeStopped.store(false, std::memory_order_relaxed);
    }
}

struct EquipmentPluginManager::Impl {
    std::vector<std::shared_ptr<LoadedPlugin>> loaded;
    std::vector<PluginDescriptor> descriptors;
    std::vector<std::string> diagnostics;
};

EquipmentPluginManager::EquipmentPluginManager() : impl_(std::make_unique<Impl>()) {}
EquipmentPluginManager::~EquipmentPluginManager() = default;

void EquipmentPluginManager::loadDirectory(const std::string& path)
{
    impl_->loaded.clear();
    impl_->descriptors.clear();
    impl_->diagnostics.clear();
    QDir directory(QString::fromUtf8(path));
#ifdef _WIN32
    const QStringList filters{QStringLiteral("*.dll")};
#elif defined(__APPLE__)
    const QStringList filters{QStringLiteral("*.dylib")};
#else
    const QStringList filters{QStringLiteral("*.so")};
#endif
    const auto files = directory.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const auto& file : files) {
        auto library = std::make_shared<QLibrary>(file.absoluteFilePath());
        if (!library->load()) {
            impl_->diagnostics.push_back(file.fileName().toStdString() + ": "
                + library->errorString().toUtf8().toStdString());
            continue;
        }
        const auto entry = reinterpret_cast<orbita_plugin_get_api_v1_fn>(
            library->resolve("orbita_plugin_get_api_v1"));
        if (!entry) {
            library->unload();
            continue;
        }
        const auto* api = entry();
        if (!api || api->abi_version != ORBITA_EQUIPMENT_ABI_V1
            || api->struct_size < sizeof(orbita_equipment_api_v1)
            || !api->plugin_id || !api->create || !api->destroy || !api->invoke
            || !api->cancel || !api->safe_stop) {
            impl_->diagnostics.push_back(
                file.fileName().toStdString() + ": несовместимый Equipment ABI");
            library->unload();
            continue;
        }
        if (std::any_of(impl_->loaded.begin(), impl_->loaded.end(), [api](const auto& plugin) {
                return plugin->descriptor.id == api->plugin_id;
            })) {
            impl_->diagnostics.push_back(
                file.fileName().toStdString() + ": повторяющийся plugin_id " + api->plugin_id);
            library->unload();
            continue;
        }
        auto plugin = std::make_shared<LoadedPlugin>();
        plugin->library = std::move(library);
        plugin->api = api;
        plugin->descriptor.id = api->plugin_id;
        plugin->descriptor.displayName = api->display_name_utf8
            ? api->display_name_utf8 : api->plugin_id;
        plugin->descriptor.capabilities = splitCapabilities(api->capabilities_utf8);
        plugin->descriptor.libraryPath = file.absoluteFilePath().toUtf8().toStdString();
        impl_->descriptors.push_back(plugin->descriptor);
        impl_->loaded.push_back(std::move(plugin));
    }
}

const std::vector<PluginDescriptor>& EquipmentPluginManager::plugins() const noexcept
{
    return impl_->descriptors;
}

std::shared_ptr<EquipmentDevice> EquipmentPluginManager::createDevice(
    const std::string& pluginId,
    const std::string& instanceId,
    const std::map<std::string, std::string>& configuration)
{
    const auto iterator = std::find_if(impl_->loaded.begin(), impl_->loaded.end(),
        [&pluginId](const auto& plugin) { return plugin->descriptor.id == pluginId; });
    if (iterator == impl_->loaded.end()) {
        throw std::runtime_error("Equipment plugin not loaded: " + pluginId);
    }

    auto deviceImpl = std::make_unique<EquipmentDevice::Impl>();
    deviceImpl->plugin = *iterator;
    deviceImpl->instanceId = instanceId;
    const std::string config = encodePluginArguments(configuration);
    std::vector<char> diagnosticBytes(2048);
    orbita_plugin_buffer_v1 diagnostic{diagnosticBytes.data(), diagnosticBytes.size(), 0};
    const auto status = (*iterator)->api->create(
        instanceId.c_str(), config.c_str(), &deviceImpl->instance, &diagnostic);
    if (status != ORBITA_PLUGIN_OK || !deviceImpl->instance) {
        const auto message = bufferText(
            diagnosticBytes.data(), diagnosticBytes.size(), diagnostic.size);
        throw std::runtime_error(message.empty()
            ? "Cannot create equipment instance " + instanceId : message);
    }
    return std::shared_ptr<EquipmentDevice>(new EquipmentDevice(std::move(deviceImpl)));
}

const std::vector<std::string>& EquipmentPluginManager::diagnostics() const noexcept
{
    return impl_->diagnostics;
}

void EquipmentRegistry::bind(
    std::string capability,
    std::shared_ptr<EquipmentDevice> device)
{
    if (!device || !device->descriptor().capabilities.count(capability)) {
        throw std::invalid_argument("Cannot bind a device without requested capability");
    }
    defaults_[std::move(capability)] = {std::move(device), {}, {}};
}

void EquipmentRegistry::bind(
    std::string capability,
    InvokeFunction invoke,
    SafeStopFunction safeStop)
{
    if (capability.empty() || !invoke) {
        throw std::invalid_argument("Cannot bind an empty built-in capability");
    }
    defaults_[std::move(capability)] = {nullptr, std::move(invoke), std::move(safeStop)};
}

void EquipmentRegistry::bindResource(
    std::string resourceId,
    std::shared_ptr<EquipmentDevice> device)
{
    if (!device) throw std::invalid_argument("Cannot bind an empty equipment resource");
    bindResource(std::move(resourceId), device->descriptor().capabilities, std::move(device));
}

void EquipmentRegistry::bindResource(
    std::string resourceId,
    std::set<std::string> capabilities,
    std::shared_ptr<EquipmentDevice> device)
{
    if (resourceId.empty() || capabilities.empty() || !device) {
        throw std::invalid_argument(
            "Equipment resource requires id, capabilities and device");
    }
    if (resources_.count(resourceId)) {
        throw std::invalid_argument("Equipment resource is already bound: " + resourceId);
    }
    for (const auto& capability : capabilities) {
        if (!device->descriptor().capabilities.count(capability)) {
            throw std::invalid_argument(
                "Equipment resource " + resourceId + " declares unsupported capability "
                + capability);
        }
    }
    resources_.emplace(std::move(resourceId), ResourceBinding{
        std::move(capabilities), std::move(device), {}, {}});
}

void EquipmentRegistry::bindResource(
    std::string resourceId,
    std::set<std::string> capabilities,
    ResourceInvokeFunction invoke,
    SafeStopFunction safeStop)
{
    if (resourceId.empty() || capabilities.empty() || !invoke) {
        throw std::invalid_argument(
            "Built-in equipment resource requires id, capabilities and invoke callback");
    }
    if (resources_.count(resourceId)) {
        throw std::invalid_argument("Equipment resource is already bound: " + resourceId);
    }
    resources_.emplace(std::move(resourceId), ResourceBinding{
        std::move(capabilities), nullptr, std::move(invoke), std::move(safeStop)});
}

bool EquipmentRegistry::hasResource(const std::string& resourceId) const
{
    return resources_.count(resourceId) != 0;
}

bool EquipmentRegistry::resourceHasCapability(
    const std::string& resourceId,
    const std::string& capability) const
{
    const auto binding = resources_.find(resourceId);
    return binding != resources_.end()
        && binding->second.capabilities.count(capability) != 0;
}

std::string EquipmentRegistry::invokeResource(
    const std::string& resourceId,
    const std::string& capability,
    const std::string& operation,
    const std::map<std::string, std::string>& arguments)
{
    const auto binding = resources_.find(resourceId);
    if (binding == resources_.end()) {
        throw std::runtime_error("Equipment resource is not bound: " + resourceId);
    }
    if (!binding->second.capabilities.count(capability)) {
        throw std::invalid_argument(
            "Resource " + resourceId + " does not provide capability " + capability);
    }
    if (binding->second.device) {
        return binding->second.device->invoke(capability, operation, arguments);
    }
    return binding->second.invoke(capability, operation, arguments);
}

std::vector<EquipmentResourceDescriptor> EquipmentRegistry::resources() const
{
    std::vector<EquipmentResourceDescriptor> result;
    result.reserve(resources_.size());
    for (const auto& [id, binding] : resources_) {
        result.push_back({id, binding.capabilities, !binding.device});
    }
    return result;
}

void EquipmentRegistry::clearPhysical() noexcept
{
    for (const auto& [capability, binding] : defaults_) {
        (void)capability;
        if (binding.device) binding.device->safeStop();
    }
    for (const auto& [resourceId, binding] : resources_) {
        (void)resourceId;
        if (binding.device) binding.device->safeStop();
    }

    for (auto it = defaults_.begin(); it != defaults_.end();) {
        it = it->second.device ? defaults_.erase(it) : std::next(it);
    }
    for (auto it = resources_.begin(); it != resources_.end();) {
        it = it->second.device ? resources_.erase(it) : std::next(it);
    }
}

void EquipmentRegistry::clear()
{
    safeStopAll();
    defaults_.clear();
    resources_.clear();
}

bool EquipmentRegistry::hasCapability(const std::string& capability) const
{
    return defaults_.count(capability) != 0;
}

std::string EquipmentRegistry::invoke(
    const std::string& capability,
    const std::string& operation,
    const std::map<std::string, std::string>& arguments)
{
    const auto binding = defaults_.find(capability);
    if (binding == defaults_.end()) {
        throw std::runtime_error("Capability is not bound: " + capability);
    }
    if (binding->second.device) {
        return binding->second.device->invoke(capability, operation, arguments);
    }
    return binding->second.invoke(operation, arguments);
}

void EquipmentRegistry::safeStopAll() noexcept
{
    for (const auto& [capability, binding] : defaults_) {
        (void)capability;
        if (binding.device) binding.device->safeStop();
        else if (binding.safeStop) {
            try { binding.safeStop(); } catch (...) {}
        }
    }
    for (const auto& [resourceId, binding] : resources_) {
        (void)resourceId;
        if (binding.device) binding.device->safeStop();
        else if (binding.safeStop) {
            try { binding.safeStop(); } catch (...) {}
        }
    }
}

std::vector<std::string> EquipmentRegistry::capabilities() const
{
    std::vector<std::string> result;
    result.reserve(defaults_.size());
    for (const auto& [capability, binding] : defaults_) {
        (void)binding;
        result.push_back(capability);
    }
    return result;
}

} // namespace orbita::stand
