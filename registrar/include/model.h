#ifndef MODEL_H
#define MODEL_H

#pragma once

#include <string>

namespace ktma::registrar {

struct Product
{
    std::string id;
    std::string productType;
    std::string serialNumber;
};

struct Component
{
    std::string id;
    std::string componentType;
    std::string serialNumber;
};

enum class Stage
{
    InitialElectrical,
    PostVibrationElectrical,
    PostClimateElectrical,
    FinalElectrical
};

enum class Verdict
{
    InProgress,
    Ok,
    Fail,
    Cancelled
};

} // namespace ktma::registrar

#endif // MODEL_H
