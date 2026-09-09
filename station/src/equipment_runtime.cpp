#include "orbita_stand/equipment_runtime.h"
#include "orbita_stand/equipment_plugin.h"

#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringList>

#include <algorithm>
#include <cstring>
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

std::string bufferText(const std::vector<char>& buffer, std::size_t size)
{
    return {buffer.data(), std::min(size, buffer.size())};
}

struct LoadedPlugin {
    std::shared_ptr<QLibrary> library;
    const orbita_equipment_api_v1* api = nullptr;
    PluginDescriptor descriptor;
};

} // namespace

std::string encodePluginArguments(const std::map<std::string, std::string>& arguments)
{
    std::string output;
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
        key.clear(); item.clear(); readingKey = true;
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
};

EquipmentDevice::EquipmentDevice(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EquipmentDevice::~EquipmentDevice()
{
    if (impl_ && impl_->instance && impl_->plugin && impl_->plugin->api) {
        impl_->plugin->api->safe_stop(impl_->instance);
        impl_->plugin->api->destroy(impl_->instance);
    }
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
    const std::string request = encodePluginArguments(arguments);
    std::vector<char> bytes(4096);
    orbita_plugin_buffer_v1 response{bytes.data(), bytes.size(), 0};
    auto status = impl_->plugin->api->invoke(
        impl_->instance, capability.c_str(), operation.c_str(), request.c_str(), &response);
    if (status == ORBITA_PLUGIN_BUFFER_TOO_SMALL && response.size > bytes.size()) {
        bytes.resize(response.size);
        response = {bytes.data(), bytes.size(), 0};
        status = impl_->plugin->api->invoke(
            impl_->instance, capability.c_str(), operation.c_str(), request.c_str(), &response);
    }
    const auto text = bufferText(bytes, response.size);
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
    if (impl_ && impl_->instance) impl_->plugin->api->safe_stop(impl_->instance);
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
            continue; // Не каждая DLL каталога является плагином Orbita.
        }
        const auto* api = entry();
        if (!api || api->abi_version != ORBITA_EQUIPMENT_ABI_V1
            || api->struct_size < sizeof(orbita_equipment_api_v1)
            || !api->plugin_id || !api->create || !api->destroy || !api->invoke
            || !api->cancel || !api->safe_stop) {
            impl_->diagnostics.push_back(file.fileName().toStdString() + ": несовместимый Equipment ABI");
            library->unload();
            continue;
        }
        if (std::any_of(impl_->loaded.begin(), impl_->loaded.end(), [api](const auto& plugin) {
                return plugin->descriptor.id == api->plugin_id;
            })) {
            impl_->diagnostics.push_back(file.fileName().toStdString() + ": повторяющийся plugin_id " + api->plugin_id);
            library->unload();
            continue;
        }
        auto plugin = std::make_shared<LoadedPlugin>();
        plugin->library = std::move(library);
        plugin->api = api;
        plugin->descriptor.id = api->plugin_id;
        plugin->descriptor.displayName = api->display_name_utf8 ? api->display_name_utf8 : api->plugin_id;
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
    if (iterator == impl_->loaded.end()) throw std::runtime_error("Equipment plugin not loaded: " + pluginId);
    auto deviceImpl = std::make_unique<EquipmentDevice::Impl>();
    deviceImpl->plugin = *iterator;
    deviceImpl->instanceId = instanceId;
    const std::string config = encodePluginArguments(configuration);
    std::vector<char> diagnosticBytes(2048);
    orbita_plugin_buffer_v1 diagnostic{diagnosticBytes.data(), diagnosticBytes.size(), 0};
    const auto status = (*iterator)->api->create(
        instanceId.c_str(), config.c_str(), &deviceImpl->instance, &diagnostic);
    if (status != ORBITA_PLUGIN_OK || !deviceImpl->instance) {
        const auto message = bufferText(diagnosticBytes, diagnostic.size);
        throw std::runtime_error(message.empty() ? "Cannot create equipment instance " + instanceId : message);
    }
    return std::shared_ptr<EquipmentDevice>(new EquipmentDevice(std::move(deviceImpl)));
}

const std::vector<std::string>& EquipmentPluginManager::diagnostics() const noexcept
{
    return impl_->diagnostics;
}

void EquipmentRegistry::bind(std::string capability, std::shared_ptr<EquipmentDevice> device)
{
    if (!device || !device->descriptor().capabilities.count(capability)) {
        throw std::invalid_argument("Cannot bind a device without requested capability");
    }
    bindings_[std::move(capability)] = std::move(device);
}
void EquipmentRegistry::bind(
    std::string capability, InvokeFunction invoke, SafeStopFunction safeStop)
{
    if (capability.empty() || !invoke) {
        throw std::invalid_argument("Cannot bind an empty built-in capability");
    }
    builtinBindings_[std::move(capability)] = {
        std::move(invoke), std::move(safeStop)};
}
void EquipmentRegistry::clear()
{
    safeStopAll();
    bindings_.clear();
    builtinBindings_.clear();
}
bool EquipmentRegistry::hasCapability(const std::string& capability) const
{
    return bindings_.count(capability) != 0
        || builtinBindings_.count(capability) != 0;
}
std::string EquipmentRegistry::invoke(
    const std::string& capability,
    const std::string& operation,
    const std::map<std::string, std::string>& arguments)
{
    const auto builtin = builtinBindings_.find(capability);
    if (builtin != builtinBindings_.end()) {
        return builtin->second.invoke(operation, arguments);
    }
    const auto device = bindings_.find(capability);
    if (device == bindings_.end()) throw std::runtime_error("Capability is not bound: " + capability);
    return device->second->invoke(capability, operation, arguments);
}
void EquipmentRegistry::safeStopAll() noexcept
{
    std::set<EquipmentDevice*> stopped;
    for (const auto& [capability, device] : bindings_) {
        (void)capability;
        if (device && stopped.insert(device.get()).second) device->safeStop();
    }
    for (const auto& [capability, binding] : builtinBindings_) {
        (void)capability;
        try {
            if (binding.safeStop) binding.safeStop();
        } catch (...) {
            // safeStopAll является noexcept по контракту.
        }
    }
}
std::vector<std::string> EquipmentRegistry::capabilities() const
{
    std::vector<std::string> result;
    for (const auto& [capability, device] : bindings_) {
        (void)device;
        result.push_back(capability);
    }
    for (const auto& [capability, binding] : builtinBindings_) {
        (void)binding;
        result.push_back(capability);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace orbita::stand
