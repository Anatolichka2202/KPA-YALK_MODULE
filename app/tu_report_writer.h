#pragma once

#include "model/run_types.h"

#include <QString>

QString writeTuReport(const tu::ScenarioRunResult& result);
void updateTuReportOperator(const QString& htmlPath, const QString& operatorName);
