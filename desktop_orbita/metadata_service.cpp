#include "metadata_service.h"
#include "encoding_utils.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QCoreApplication>
#include <QSet>
#include <QDebug>

// ── helpers ──────────────────────────────────────────────────────────────
static QString pad2(const QString& v) {
    QString s = v.trimmed();
    return (s.size() == 1) ? "0" + s : s;
}

// Ключ адреса без M: "P{stream}{A..}{B..}...T{tt}[P{pp}]"
static QString buildComponentKey(const QString& stream,
                                 const QString& a, const QString& b, const QString& c,
                                 const QString& d, const QString& e, const QString& x,
                                 const QString& t, const QString& p) {
    QString s = stream.trimmed();
    s.remove(QChar(0x041F)); // П
    s.remove('P');
    if (s.isEmpty()) return {};
    QString key = "P" + s;
    const QPair<QString, QString> fields[] = {
        {"A", a}, {"B", b}, {"C", c}, {"D", d}, {"E", e}, {"X", x}
    };
    for (const auto& f : fields)
        if (!f.second.trimmed().isEmpty())
            key += f.first + f.second.trimmed();
    QString tt = t.trimmed();
    if (!tt.isEmpty()) key += "T" + pad2(tt);
    QString pp = p.trimmed();
    if (pad2(tt) == "05" && !pp.isEmpty()) key += "P" + pad2(pp);
    return key;
}

// ── lifecycle ────────────────────────────────────────────────────────────
MetadataService::MetadataService(QObject* parent)
    : QObject(parent), db_(std::make_unique<QSqlDatabase>()) {}

MetadataService::~MetadataService() = default;

bool MetadataService::open(const QString& dbPath) {
    QString path = dbPath.isEmpty()
        ? QCoreApplication::applicationDirPath() + "/parameters.db"
        : dbPath;
    *db_ = QSqlDatabase::addDatabase("QSQLITE");
    db_->setDatabaseName(path);
    if (!db_->open()) {
        emit loadError(db_->lastError().text());
        qWarning() << "MetadataService: cannot open" << path << db_->lastError().text();
        return false;
    }
    loadMetadata();
    return true;
}

bool MetadataService::isOpen() const { return db_ && db_->isOpen(); }

void MetadataService::refresh() { loadMetadata(); }

void MetadataService::loadMetadata() {
    params_.clear();
    byKey_.clear();
    if (!db_->isOpen()) return;

    QSqlQuery q(*db_);
    // Сначала live (is_zu=0), потом ЗУ — чтобы при совпадении ключа выигрывал live.
    q.prepare(
        "SELECT p.name, p.category, p.signal_type, "
        "       a.stream, a.a, a.b, a.c, a.d, a.e, a.x, a.t, a.p, a.informativnost, a.is_zu "
        "FROM parameters p JOIN addresses a ON a.param_id = p.id "
        "ORDER BY a.is_zu ASC");
    if (!q.exec()) {
        emit loadError(q.lastError().text());
        qWarning() << "MetadataService: query failed" << q.lastError().text();
        return;
    }

    int liveCount = 0, zuCount = 0;
    while (q.next()) {
        ParamInfo info;
        info.name       = q.value(0).toString();
        info.category   = q.value(1).toString();
        info.signalType = q.value(2).toString();
        info.componentKey = buildComponentKey(
            q.value(3).toString(), q.value(4).toString(), q.value(5).toString(),
            q.value(6).toString(), q.value(7).toString(), q.value(8).toString(),
            q.value(9).toString(), q.value(10).toString(), q.value(11).toString());
        info.informativnost = q.value(12).isNull() ? -1 : q.value(12).toInt();
        info.isZu = q.value(13).toInt() != 0;

        if (info.componentKey.isEmpty()) continue;
        if (byKey_.contains(info.componentKey)) continue; // первый (live) выигрывает
        byKey_.insert(info.componentKey, params_.size());
        params_.push_back(info);
        if (info.isZu) ++zuCount; else ++liveCount;
    }
    qDebug() << "MetadataService: loaded" << liveCount << "live +" << zuCount << "ЗУ addresses";
}

// ── lookup ───────────────────────────────────────────────────────────────
std::optional<ParamInfo> MetadataService::lookup(const QString& address) const {
    QString key = stripInformativnost(normalize(address));
    auto it = byKey_.find(key);
    if (it == byKey_.end()) return std::nullopt;
    return params_[it.value()];
}

QString MetadataService::getName(const QString& address) const {
    auto p = lookup(address);
    return p ? p->name : QString();
}

QString MetadataService::getCategory(const QString& address) const {
    auto p = lookup(address);
    return p ? p->category : QString();
}

QStringList MetadataService::getAllCategories() const {
    QSet<QString> cats;
    for (const auto& p : params_)
        if (!p.category.isEmpty()) cats.insert(p.category);
    QStringList list = cats.values();
    list.sort();
    return list;
}

QList<ParamInfo> MetadataService::allParams() const {
    QList<ParamInfo> out;
    for (const auto& p : params_)
        if (!p.isZu) out.push_back(p);
    return out;
}

QList<ParamInfo> MetadataService::paramsInCategory(const QString& category) const {
    QList<ParamInfo> out;
    for (const auto& p : params_)
        if (!p.isZu && p.category == category) out.push_back(p);
    return out;
}

QHash<QString, QString> MetadataService::getParametersByCategory(const QString& category) const {
    QHash<QString, QString> out;
    for (const auto& p : params_)
        if (!p.isZu && p.category == category)
            out.insert(buildFullAddress(p), p.name);  // ключ — M16-адрес для отслеживания
    return out;
}

// ── utils ────────────────────────────────────────────────────────────────
QString MetadataService::buildFullAddress(const ParamInfo& p, int informativnost) {
    int m = (informativnost > 0) ? informativnost
            : (p.informativnost > 0 ? p.informativnost : 16);
    return "M" + QString::number(m) + p.componentKey;
}

QString MetadataService::normalize(const QString& raw) {
    return QString::fromStdString(encoding::normalizeAddress(raw.toStdString()));
}

QString MetadataService::stripInformativnost(const QString& norm) {
    // убрать ведущий "M" + цифры
    int i = 0;
    if (i < norm.size() && (norm[i] == 'M' || norm[i] == 'm')) {
        ++i;
        while (i < norm.size() && norm[i].isDigit()) ++i;
        return norm.mid(i);
    }
    return norm;
}
