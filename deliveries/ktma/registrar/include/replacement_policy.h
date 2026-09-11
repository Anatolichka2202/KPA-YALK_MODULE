#pragma once

#include <string>
#include <vector>

namespace ktma::registrar {

// Единственная policy повторной verification после замены ячейки.
// Коды стабильны для UI, orchestration и production report.
inline std::vector<std::string> replacementVerificationPackages(const std::string& componentType)
{
    if (componentType == "YALK-96") return {"PROD_YALK_FULL"};
    if (componentType == "YTP") return {"PROD_YTP_FULL"};
    // ЯВП теперь использует отдельный ROKT-режим адаптера. Старый дополнительный
    // пакет PROD_YALK_88_96 больше не описывает текущий transport ЯВП.
    if (componentType == "YVP") return {"PROD_YVP_FULL"};
    if (componentType == "YP-P") return {"PROD_POWER_CONSUMPTION"};
    return {};
}

} // namespace ktma::registrar
