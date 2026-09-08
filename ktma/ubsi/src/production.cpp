#include "ktma/ubsi/production.h"

#include <array>
#include <map>
#include <stdexcept>

namespace ktma::ubsi {
namespace {

constexpr std::array<const char*, 4> kRequiredComposition = {
    "YALK-96", "YTP", "YVP", "YP-P"
};

bool contains(const std::vector<std::string>& values, const std::string& value)
{
    for (const auto& item : values) if (item == value) return true;
    return false;
}

} // namespace

const char* toString(ProductionPackage package) noexcept
{
    switch (package) {
    case ProductionPackage::FullUbsi: return "FULL_UBSI";
    case ProductionPackage::PowerConsumption: return "POWER_CONSUMPTION";
    case ProductionPackage::Yalk: return "YALK";
    case ProductionPackage::Ytp: return "YTP";
    case ProductionPackage::Yvp: return "YVP";
    }
    return "UNKNOWN";
}

const char* toString(ProductionRunStatus status) noexcept
{
    switch (status) {
    case ProductionRunStatus::InProgress: return "IN_PROGRESS";
    case ProductionRunStatus::Norm: return "NORM";
    case ProductionRunStatus::NotNorm: return "NOT_NORM";
    case ProductionRunStatus::StandError: return "STAND_ERROR";
    case ProductionRunStatus::Incomplete: return "INCOMPLETE";
    case ProductionRunStatus::Stopped: return "STOPPED";
    }
    return "STAND_ERROR";
}

ProductionPackage productionPackageFromCode(const std::string& code)
{
    if (code == "PROD_FULL" || code == "FULL_UBSI") return ProductionPackage::FullUbsi;
    if (code == "PROD_POWER" || code == "POWER_CONSUMPTION") return ProductionPackage::PowerConsumption;
    if (code == "PROD_YALK" || code == "YALK") return ProductionPackage::Yalk;
    if (code == "PROD_YTP" || code == "YTP") return ProductionPackage::Ytp;
    if (code == "PROD_YVP" || code == "YVP") return ProductionPackage::Yvp;
    throw std::invalid_argument("unknown UBSI production package: " + code);
}

std::string scenarioCodeForPackage(ProductionPackage package)
{
    switch (package) {
    case ProductionPackage::FullUbsi: return "ULK_COMBINED_CHECK";
    case ProductionPackage::PowerConsumption: return "PROD_POWER";
    case ProductionPackage::Yalk: return "YALK_FULL_5_6";
    case ProductionPackage::Ytp: return "YTP_FULL_5_6";
    case ProductionPackage::Yvp: return "PROD_YVP";
    }
    throw std::invalid_argument("unknown UBSI production package");
}

std::vector<std::string> affectedComponentTypes(ProductionPackage package)
{
    switch (package) {
    case ProductionPackage::FullUbsi:
        return {"YALK-96", "YTP", "YVP", "YP-P"};
    case ProductionPackage::PowerConsumption:
        return {"YP-P"};
    case ProductionPackage::Yalk:
        return {"YALK-96"};
    case ProductionPackage::Ytp:
        return {"YTP"};
    case ProductionPackage::Yvp:
        // ЯВП measured output is read through YALK 89..96, therefore both
        // cells belong to the affected snapshot even though YVP is the DUT.
        return {"YVP", "YALK-96"};
    }
    throw std::invalid_argument("unknown UBSI production package");
}

ProductionRunContext buildProductionRunContext(
    const registrar::ProductReport& report,
    registrar::Stage stage,
    ProductionPackage package)
{
    if (report.product.id.empty() || report.product.serialNumber.empty()) {
        throw std::invalid_argument("production requires a registered UBSI product");
    }
    if (report.product.productType != "UBSI") {
        throw std::invalid_argument("production context supports UBSI only");
    }

    std::map<std::string, registrar::ComponentBinding> active;
    for (const auto& component : report.components) {
        if (!component.active) continue;
        if (active.count(component.componentType)) {
            throw std::logic_error("multiple active components of type " + component.componentType);
        }
        active.emplace(component.componentType, component);
    }

    std::vector<std::string> missing;
    for (const auto* type : kRequiredComposition) {
        if (!active.count(type)) missing.emplace_back(type);
    }
    if (!missing.empty()) {
        std::string message = "production composition incomplete; missing: ";
        for (std::size_t index = 0; index < missing.size(); ++index) {
            if (index) message += ", ";
            message += missing[index];
        }
        throw std::logic_error(message);
    }

    const auto affected = affectedComponentTypes(package);
    ProductionRunContext context;
    context.productId = report.product.id;
    context.productSerial = report.product.serialNumber;
    context.stage = stage;
    context.package = package;
    context.scenarioCode = scenarioCodeForPackage(package);

    for (const auto* type : kRequiredComposition) {
        const auto& component = active.at(type);
        context.composition.push_back({
            component.componentId,
            component.componentType,
            component.serialNumber,
            contains(affected, component.componentType)});
    }
    return context;
}

} // namespace ktma::ubsi
