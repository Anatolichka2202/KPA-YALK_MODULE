#pragma once
/**
 * @file metadata_service.h
 * @brief Доступ к нормализованной БД параметров (parameters + addresses).
 *
 * Только UI-слой (Qt). Ядро о БД не знает.
 *
 * Поиск идёт ПО КОМПОНЕНТАМ адреса (M отбрасывается): адрес из конфига
 * "M16P1A70..." сводится к ключу "P1A70..." и матчится с компонентами из БД.
 * Поэтому резолвятся и параметры, у которых в исходнике не было информативности.
 */

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QList>
#include <optional>
#include <memory>

class QSqlDatabase;

struct ParamInfo {
    QString name;
    QString category;
    QString signalType;     // analog10/contact/fast1.../bus/unknown
    QString componentKey;   // адрес без M, напр. "P1A70B12C10D10T01"
    int     informativnost = -1;  // -1 если в БД не задана (M берём из UI/конфига)
};

class MetadataService : public QObject {
    Q_OBJECT
public:
    explicit MetadataService(QObject* parent = nullptr);
    ~MetadataService();

    bool open(const QString& dbPath = QString());
    void refresh();
    bool isOpen() const;

    // ── Совместимые методы (как у прежнего ParameterDatabase) ──
    QString getName(const QString& address) const;       // по адресу (M отбрасывается)
    QString getCategory(const QString& address) const;
    QStringList getAllCategories() const;
    QHash<QString, QString> getParametersByCategory(const QString& category) const; // {M16-адрес: имя}

    // ── Богатый API ──
    std::optional<ParamInfo> lookup(const QString& address) const;
    QList<ParamInfo> allParams() const { return params_; }
    QList<ParamInfo> paramsInCategory(const QString& category) const;

    // ── Утилиты ──
    static QString buildFullAddress(const ParamInfo& p, int informativnost = 16);
    static QString normalize(const QString& raw);             // = encoding::normalizeAddress
    static QString stripInformativnost(const QString& norm);  // убрать ведущий M\d+

signals:
    void loadError(const QString& message);

private:
    void loadMetadata();

    std::unique_ptr<QSqlDatabase> db_;
    QList<ParamInfo>     params_;     // каталог (live-адреса)
    QHash<QString, int>  byKey_;      // componentKey → индекс в params_
};
