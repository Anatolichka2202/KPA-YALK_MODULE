#pragma once
#include "channel_status.h"   // chstatus::Tolerance, chstatus::forAddress
#include <QString>
#include <QHash>

class MetadataService;

// Слоистый резолвер допусков: override > config(.tol) > БД.
class ToleranceResolver {
public:
    void setDb(MetadataService* db) { db_ = db; }

    // Конфиг-слой из .tol (заменяет весь слой). Возвращает число загруженных строк (-1 при ошибке файла).
    int loadConfigFile(const QString& tolPath);
    void clearConfig() { config_.clear(); }

    // Сессионный override-слой
    void setOverride(const QString& address, const chstatus::Tolerance& t) { override_.insert(key(address), t); }
    void clearOverride(const QString& address) { override_.remove(key(address)); }
    void clearAllOverrides() { override_.clear(); }

    // Эффективный допуск: override ?? config ?? БД. set=false если нигде нет.
    chstatus::Tolerance resolve(const QString& address) const;

private:
    static QString key(const QString& address);
    MetadataService* db_ = nullptr;
    QHash<QString, chstatus::Tolerance> config_;
    QHash<QString, chstatus::Tolerance> override_;
};
