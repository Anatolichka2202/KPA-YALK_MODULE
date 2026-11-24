#include "metadata_service.h"
#include "encoding_utils.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {

QString componentKey(const QString& address)
{
    const auto normalized = QString::fromStdString(encoding::normalizeAddress(address.toStdString()));
    if (!normalized.startsWith(QLatin1Char('M'))) return normalized;
    qsizetype index = 1;
    while (index < normalized.size() && normalized[index].isDigit()) ++index;
    return normalized.mid(index);
}

QString databaseKey(const QSqlQuery& row)
{
    auto stream = row.value(1).toString().trimmed();
    if (stream.isEmpty()) return {};
    if (stream.front().isDigit()) stream.prepend(QLatin1Char('P'));
    QString key = stream;
    static constexpr char fields[] = {'A', 'B', 'C', 'D', 'E', 'X', 'T'};
    for (int i = 0; i < 7; ++i) {
        const auto value = row.value(i + 2).toString().trimmed();
        if (!value.isEmpty()) key += QLatin1Char(fields[i]) + value.rightJustified(2, QLatin1Char('0'));
    }
    const auto bit = row.value(9).toString().trimmed();
    if (key.endsWith(QStringLiteral("T05")) && !bit.isEmpty())
        key += QLatin1Char('P') + bit.rightJustified(2, QLatin1Char('0'));
    return componentKey(key);
}

} // namespace

bool MetadataService::open(const QString& path)
{
    names_.clear();
    const auto connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool success = false;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery query(db);
            success = query.exec(QStringLiteral(
                "SELECT p.name, a.stream, a.a, a.b, a.c, a.d, a.e, a.x, a.t, a.p "
                "FROM parameters p JOIN addresses a ON a.param_id = p.id "
                "ORDER BY a.is_zu ASC, a.id ASC"));
            while (success && query.next()) {
                const auto key = databaseKey(query);
                if (!key.isEmpty() && !names_.contains(key))
                    names_.insert(key, query.value(0).toString());
            }
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return success;
}

QString MetadataService::name(const QString& address) const
{
    return names_.value(componentKey(address));
}
