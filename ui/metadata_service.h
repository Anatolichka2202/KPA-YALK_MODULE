#pragma once

#include <QHash>
#include <QString>

// Имена из общей SQLite-БД «Орбита» используются для отображения.
// Привязки проверок и границы кода БРП задаются независимо от метаданных.
class MetadataService {
public:
    bool open(const QString& path);
    QString name(const QString& address) const;

private:
    QHash<QString, QString> names_;
};
