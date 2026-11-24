#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

enum class BrpResult {
    Pending, Waiting, Running, Passed, Failed, Unavailable, Skipped, Interrupted
};

struct BrpMeasurement {
    QString name;
    QString value;
    QString unit;
};

struct BrpRunStep {
    int row;
    QString requirement;
    QString name;
    QString address;
    BrpResult result = BrpResult::Pending;
    QString detail;
    QVector<BrpMeasurement> measurements;
};

struct BrpRunReport {
    QString id;
    QString serial;
    QString operatorName;
    bool simulation = false;
    QDateTime started;
    QDateTime finished;
    QVector<BrpRunStep> steps;
};

QString brpResultText(BrpResult result);
QString writeBrpRunReport(const BrpRunReport& run);
