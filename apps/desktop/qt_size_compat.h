#pragma once

#include <QtCore/qtypes.h>

// Transitional compile shim for the new HMI views. Qt 6 uses qsizetype for
// container sizes while a few prototype-view expressions still use integer
// literals. Keep this local to desktop targets; the view source will use
// explicit casts once the clean port is consolidated.
namespace std {
inline qsizetype min(int lhs, qsizetype rhs) noexcept
{
    return static_cast<qsizetype>(lhs) < rhs ? static_cast<qsizetype>(lhs) : rhs;
}
inline qsizetype max(int lhs, qsizetype rhs) noexcept
{
    return static_cast<qsizetype>(lhs) > rhs ? static_cast<qsizetype>(lhs) : rhs;
}
}
