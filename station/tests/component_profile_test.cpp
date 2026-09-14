#include "orbita_stand/config.h"

#include <QCoreApplication>
#include <QDir>

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

        const auto* legacyEquipment = findComponentByBinding(profile, "power.dc_supply");
        require(legacyEquipment != nullptr, "Legacy devices were not mirrored into component model");
        require(legacyEquipment->kind == "equipment", "Legacy device must become equipment component");
        require(legacyEquipment->provider == "orbita.akip_1160_pair",
                "Legacy equipment provider was not preserved");

        std::cout << "component profile contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "component profile contract failed: " << error.what() << '\n';
        return 1;
    }
}
