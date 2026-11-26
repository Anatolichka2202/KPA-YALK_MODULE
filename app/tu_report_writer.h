#pragma once

#include "model/run_types.h"

#include <QFileInfoList>
#include <QJsonObject>
#include <QString>

// Durable pre-report snapshot. Every run has its own immutable identifier;
// a crash must not erase completed measurements.
QString writeTuCheckpoint(const tu::ScenarioRunResult& result);
QFileInfoList listTuCheckpoints();
QJsonObject readTuCheckpoint(const QString& path);

QString writeTuReport(const tu::ScenarioRunResult& result, const QString& operatorName);
