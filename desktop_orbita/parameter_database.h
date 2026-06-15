#pragma once
#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>

class QSqlDatabase;

class ParameterDatabase : public QObject {
    Q_OBJECT
public:
    explicit ParameterDatabase(QObject *parent = nullptr);
    ~ParameterDatabase();

    bool open(const QString& dbPath = QString());
    void refresh();

    QString getName(const QString& normalizedAddress) const;
    QString getCategory(const QString& normalizedAddress) const;
    QStringList getAllCategories() const;
    QHash<QString, QString> getParametersByCategory(const QString& category) const;

private:
    void loadMetadata();

    std::unique_ptr<QSqlDatabase> db_;
    QHash<QString, QString> nameMap_;
    QHash<QString, QString> categoryMap_;
};
