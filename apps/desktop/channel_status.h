#pragma once
/**
 * @file channel_status.h
 * @brief Единый движок статуса канала по допуску (норма / у предела / вне / не задан).
 *
 * Принцип контракта: столбцы одного нейтрального цвета (сталь-синий), акцент
 * (амбер/красный) — ТОЛЬКО при выходе за допуск. Нет допуска → нейтраль, без тревоги.
 *
 * Пока БД не хранит допуски (hasTolerance=false), все каналы → Unset → спокойный синий.
 * Когда появятся реальные lo/nominal/hi (БД-эталоны или rules-файл) — цвета оживут сами.
 */

#include <QColor>
#include <QString>
#include <optional>
#include <string>
#include "metadata_service.h"

namespace chstatus {

struct Tolerance { double lo = 0, nominal = 0, hi = 0; bool set = false; };

enum class Level { Unset, Norm, Near, Out };

inline Tolerance fromInfo(const std::optional<ParamInfo>& info) {
    Tolerance t;
    if (info && info->hasTolerance) {
        t.lo = info->lo; t.nominal = info->nominal; t.hi = info->hi; t.set = true;
    }
    return t;
}

inline Tolerance forAddress(MetadataService* db, const std::string& address) {
    if (!db) return {};
    return fromInfo(db->lookup(QString::fromStdString(address)));
}

inline Level evaluate(double code, const Tolerance& t) {
    if (!t.set) return Level::Unset;
    if (code < t.lo || code > t.hi) return Level::Out;
    const double margin = (t.hi - t.lo) * 0.08;
    if (code <= t.lo + margin || code >= t.hi - margin) return Level::Near;
    return Level::Norm;
}

inline bool isAnomaly(Level l) { return l == Level::Near || l == Level::Out; }

// Цвет столбца/шкалы: нейтраль и норма — сталь-синий; акцент только на near/out.
inline QColor barColor(Level l) {
    switch (l) {
        case Level::Out:  return QColor(0xcf, 0x5b, 0x52);
        case Level::Near: return QColor(0xd9, 0x9a, 0x4a);
        default:          return QColor(0x5e, 0x93, 0xb8);
    }
}

inline QString text(Level l) {
    switch (l) {
        case Level::Out:  return QStringLiteral("Вне допуска");
        case Level::Near: return QStringLiteral("У предела");
        case Level::Norm: return QStringLiteral("Норма");
        default:          return QStringLiteral("—");
    }
}

inline QColor textColor(Level l) {
    switch (l) {
        case Level::Out:  return QColor(0xe8, 0x90, 0x89);
        case Level::Near: return QColor(0xe6, 0xb8, 0x78);
        case Level::Norm: return QColor(0x7f, 0xc7, 0x9a);
        default:          return QColor(0x7e, 0x8a, 0x98);  // нейтрально-серый
    }
}

} // namespace chstatus
