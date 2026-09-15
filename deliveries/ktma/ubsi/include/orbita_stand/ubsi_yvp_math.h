#pragma once

#include <cmath>
#include <stdexcept>

namespace orbita::stand {

inline double yvpChargePc(double capacitancePf, double inputV)
{
    if (!(capacitancePf > 0.0) || !std::isfinite(capacitancePf) || !std::isfinite(inputV))
        throw std::invalid_argument("YVP charge requires finite C>0 and U");
    return capacitancePf * inputV;
}

inline double yvpGainMvPerPc(double outputV, double chargePc)
{
    if (!(chargePc > 0.0) || !std::isfinite(chargePc) || !std::isfinite(outputV))
        throw std::invalid_argument("YVP gain requires finite Q>0 and Uout");
    return 1000.0 * outputV / chargePc;
}

inline double yvpRelativeErrorPercent(double measured, double reference)
{
    if (reference == 0.0 || !std::isfinite(reference) || !std::isfinite(measured))
        throw std::invalid_argument("Relative error requires finite non-zero reference");
    return (measured - reference) / reference * 100.0;
}

inline double yvpAfcPercent(double amplitude, double referenceAmplitude)
{
    return yvpRelativeErrorPercent(amplitude, referenceAmplitude);
}

inline double yvpAttenuationDb(double referenceAmplitude, double amplitude)
{
    if (!(referenceAmplitude > 0.0) || !(amplitude > 0.0)
        || !std::isfinite(referenceAmplitude) || !std::isfinite(amplitude))
        throw std::invalid_argument("Attenuation requires finite positive amplitudes");
    return 20.0 * std::log10(referenceAmplitude / amplitude);
}

inline double yvpStimulusVppForGain(double gainMvPerPc)
{
    if (std::abs(gainMvPerPc - 0.25) < 1e-9) return 8.0;
    if (std::abs(gainMvPerPc - 0.5) < 1e-9) return 4.0;
    if (std::abs(gainMvPerPc - 1.0) < 1e-9) return 2.0;
    if (gainMvPerPc > 1.0) return 1.0;
    throw std::invalid_argument("Unsupported YVP gain");
}

} // namespace orbita::stand
