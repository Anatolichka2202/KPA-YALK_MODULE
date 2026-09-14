#include "orbita_stand/config.h"

#include <QCoreApplication>
#include <QDir>

#include <algorithm>
#include <iostream>
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

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto profilePath = QDir(QString::fromUtf8(ORBITA_SOURCE_DIR))
            .filePath(QStringLiteral("data/profiles/stand_ktma.yaml"));
        const auto profile = loadStandProfile(profilePath.toStdString());

        const auto* sampleSource = findComponentByBinding(
            profile, "telemetry.orbita.sample_source");
        require(sampleSource != nullptr, "Orbita sample source is not declared by KTMA delivery");
        require(sampleSource->kind == "sample_source", "Wrong component kind for Orbita input");
        require(sampleSource->provider == "miltech.sample.e2010", "Wrong E2010 provider id");
        require(sampleSource->configuration.at("channel") == "1", "E2010 channel was not loaded");
        require(sampleSource->configuration.at("sample_rate_khz") == "10000",
                "E2010 sample rate was not loaded");

        const auto* supplyComponent = findComponentByBinding(profile, "power.dc_supply");
        require(supplyComponent != nullptr, "Power supply is not declared as a station component");
        require(supplyComponent->kind == "equipment", "Power supply must be an equipment component");
        require(supplyComponent->provider == "orbita.akip_1160_pair",
                "Equipment provider was not preserved in canonical component model");

        const auto legacyView = std::find_if(profile.devices.begin(), profile.devices.end(),
            [](const DeviceProfile& device) { return device.id == "dc-supply"; });
        require(legacyView != profile.devices.end(),
                "Canonical equipment component was not projected to the legacy DeviceProfile view");
        require(legacyView->pluginId == "orbita.akip_1160_pair",
                "Derived DeviceProfile lost the equipment provider id");
        require(std::find(legacyView->bindCapabilities.begin(), legacyView->bindCapabilities.end(),
                          "power.dc_supply") != legacyView->bindCapabilities.end(),
                "Derived DeviceProfile lost the equipment capability binding");

        require(profile.components.size() == 7,
                "KTMA delivery must declare one sample source and six equipment components");
        require(profile.devices.size() == 6,
                "Legacy equipment view must be derived for all six KTMA equipment components");

        std::cout << "component profile contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "component profile contract failed: " << error.what() << '\n';
        return 1;
    }
}
