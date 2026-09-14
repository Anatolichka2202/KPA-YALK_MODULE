#include "orbita_stand/component_runtime.h"
#include "orbita_stand/config.h"
#include "orbita_stand/sample_source.h"

#include <QCoreApplication>
#include <QDir>

#include <iostream>
#include <memory>
#include <stdexcept>

#ifndef ORBITA_SOURCE_DIR
#define ORBITA_SOURCE_DIR "."
#endif

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class FakeComponent final : public IStationComponent {
public:
    explicit FakeComponent(bool* stopped) : stopped_(stopped) {}

    std::string_view componentKind() const noexcept override { return "fake"; }
    void safeStop() noexcept override
    {
        if (stopped_) *stopped_ = true;
    }

private:
    bool* stopped_ = nullptr;
};

void genericRuntimeContract()
{
    bool stopped = false;
    ComponentRuntime runtime;
    runtime.registerKindFactory("fake",
        [&stopped](const ComponentProfile& definition) -> std::unique_ptr<IStationComponent> {
            if (definition.provider != "test.fake") {
                throw std::runtime_error("unexpected fake provider");
            }
            return std::make_unique<FakeComponent>(&stopped);
        });

    StandProfile profile;
    profile.id = "test";
    profile.version = "1";
    profile.components.push_back(ComponentProfile{
        "component-a", "fake", "test.fake", true, {"role.primary"}, {}});
    profile.components.push_back(ComponentProfile{
        "component-disabled", "fake", "test.fake", false, {"role.disabled"}, {}});

    runtime.instantiate(profile);
    require(runtime.findById("component-a") != nullptr, "component was not instantiated");
    require(runtime.findByBinding("role.primary") != nullptr, "binding was not registered");
    require(runtime.findByBinding("role.disabled") == nullptr, "disabled component was instantiated");
    require(runtime.components().size() == 1, "unexpected component count");

    runtime.clear();
    require(stopped, "clear must safe-stop instantiated components");
    require(runtime.findById("component-a") == nullptr, "clear must remove instances");
}

void duplicateBindingIsRejected()
{
    ComponentRuntime runtime;
    runtime.registerKindFactory("fake",
        [](const ComponentProfile&) -> std::unique_ptr<IStationComponent> {
            return std::make_unique<FakeComponent>(nullptr);
        });

    StandProfile profile;
    profile.id = "duplicate";
    profile.version = "1";
    profile.components.push_back(ComponentProfile{
        "a", "fake", "test.fake", true, {"role.same"}, {}});
    profile.components.push_back(ComponentProfile{
        "b", "fake", "test.fake", true, {"role.same"}, {}});

    bool rejected = false;
    try {
        runtime.instantiate(profile);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "ambiguous component binding must be rejected");
    require(runtime.components().empty(), "failed instantiate must roll back runtime state");
}

void ktmaSampleSourceContract()
{
    const auto profilePath = QDir(QString::fromUtf8(ORBITA_SOURCE_DIR))
        .filePath(QStringLiteral("data/profiles/stand_ktma.yaml"));
    const auto profile = loadStandProfile(profilePath.toStdString());

    ComponentRuntime runtime;
    registerSampleSourceComponents(runtime);
    runtime.instantiate(profile, {"sample_source"});

    auto* source = runtime.findAs<ISampleSource>("telemetry.orbita.sample_source");
    require(source != nullptr, "KTMA sample source was not instantiated by component runtime");
    require(!source->isOpen(), "component construction must not open physical hardware");

    const auto& descriptors = runtime.components();
    require(descriptors.size() == 1, "only selected sample_source kind must be instantiated");
    require(descriptors.front().provider == "miltech.sample.e2010",
            "delivery provider was not preserved by component runtime");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        genericRuntimeContract();
        duplicateBindingIsRejected();
        ktmaSampleSourceContract();
        std::cout << "component runtime contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "component runtime contract failed: " << error.what() << '\n';
        return 1;
    }
}
