#include "tolerance_resolver.h"
#include "encoding_utils.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

QString ToleranceResolver::key(const QString& address) {
    return QString::fromStdString(encoding::normalizeAddress(address.toStdString()));
}

chstatus::Tolerance ToleranceResolver::resolve(const QString& address) const {
    const QString k = key(address);
    auto ito = override_.find(k);
    if (ito != override_.end()) return ito.value();
    auto itc = config_.find(k);
    if (itc != config_.end()) return itc.value();
    if (db_) return chstatus::forAddress(db_, address.toStdString());
    return {};
}

int ToleranceResolver::loadConfigFile(const QString& tolPath) {
    QFile f(tolPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;
    config_.clear();
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    int count = 0;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 3) continue;             // нужно минимум: address lo hi
        bool okLo=false, okHi=false;
        double lo = parts[1].toDouble(&okLo);
        double hi = parts[2].toDouble(&okHi);
        if (!okLo || !okHi) continue;
        chstatus::Tolerance t; t.lo = lo; t.hi = hi; t.set = true;
        if (parts.size() >= 4) { bool okN=false; double n = parts[3].toDouble(&okN); t.nominal = okN ? n : (lo+hi)/2.0; }
        else t.nominal = (lo+hi)/2.0;
        config_.insert(key(parts[0]), t);
        ++count;
    }
    return count;
}
