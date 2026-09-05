#pragma once

#include "model.h"

#include <string>

namespace ktma::registrar {

// Финальный журнал регистратора: измерения не дублируются, в таблице остаются
// состав, история замен, этапы и ссылки на подробные run-отчёты.
std::string writeProductReportHtml(
    const ProductReport& report,
    const std::string& outputPath);

} // namespace ktma::registrar
