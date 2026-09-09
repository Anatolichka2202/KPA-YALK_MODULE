#pragma once

#include "model.h"

#include <string>
#include <vector>

namespace ktma::ubsi {

enum class ProductionPackage
{
    FullUbsi,
    PowerConsumption,
    Yalk,
    Ytp,
    Yvp
};

enum class ProductionRunStatus
{
    InProgress,
    Norm,
    NotNorm,
    StandError,
    Incomplete,
    Stopped
};

struct ProductionComponentSnapshot
{
    std::string componentId;
    std::string componentType;
    std::string serialNumber;
    bool affected = false;
};

struct ProductionRunContext
{
    std::string productId;
    std::string productSerial;
    registrar::Stage stage = registrar::Stage::Primary;
    ProductionPackage package = ProductionPackage::FullUbsi;
    std::string scenarioCode;
    std::vector<ProductionComponentSnapshot> composition;
};

const char* toString(ProductionPackage package) noexcept;
const char* toString(ProductionRunStatus status) noexcept;
ProductionPackage productionPackageFromCode(const std::string& code);
ProductionRunStatus productionRunStatusFromString(const std::string& value);
std::string scenarioCodeForPackage(ProductionPackage package);
std::vector<std::string> affectedComponentTypes(ProductionPackage package);

// Production always starts from an UBSI product, not from a selected cell.
// The four-cell composition is mandatory before any production run. The
// package only defines which cells are affected by this particular run.
ProductionRunContext buildProductionRunContext(
    const registrar::ProductReport& report,
    registrar::Stage stage,
    ProductionPackage package);

} // namespace ktma::ubsi
