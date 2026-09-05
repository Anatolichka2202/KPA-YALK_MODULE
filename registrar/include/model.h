#ifndef MODEL_H
#define MODEL_H

#pragma once

#include <string>
#include <vector>

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

struct ComponentBinding
{
    std::string productId;
    std::string componentId;
    std::string componentType;
    std::string serialNumber;
    bool active = false;
    std::string installedAt;
    std::string removedAt;
    std::string removalReason;
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
    Cancelled,
    Incomplete
};

struct StageAttempt
{
    std::string id;
    std::string productId;
    std::string componentId;
    Stage stage = Stage::InitialElectrical;
    Verdict verdict = Verdict::InProgress;
    std::string openedAt;
    std::string finishedAt;
    std::string runId;
};

struct ProductReport
{
    Product product;
    std::vector<ComponentBinding> components;
    std::vector<StageAttempt> stageAttempts;
    Verdict verdict = Verdict::Incomplete;
};

const char* toString(Stage stage);
const char* toString(Verdict verdict);
Stage stageFromString(const std::string& value);
Verdict verdictFromString(const std::string& value);

} // namespace ktma::registrar

#endif // MODEL_H
