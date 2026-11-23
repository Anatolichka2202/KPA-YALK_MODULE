#pragma once

// UI-only manifest of visible screens and widgets.
// This is NOT the runtime project package loader or an authorization contract.

#include <QString>
#include <QStringList>
#include <QVector>

namespace miltech::ui {

struct ScreenSpec {
    QString id;
    QString title;
    QString group;
};

struct UiComposition {
    QString id;
    QString title;
    QString kind;
    QString selectorTitle;
    QString demoName;
    QString demoDut;
    QStringList demoEquipment;
    QStringList demoCapabilities;
    bool referenceSample = false;
    QVector<ScreenSpec> screens;
    QStringList workspaceComponents;
    QString error;

    bool valid() const noexcept { return error.isEmpty() && !screens.isEmpty(); }
    bool isTu() const noexcept { return kind == QStringLiteral("tu"); }
    bool hasComponent(const QString& id) const { return workspaceComponents.contains(id); }
    int indexOf(const QString& screenId) const {
        for (int n = 0; n < screens.size(); ++n)
            if (screens.at(n).id == screenId) return n;
        return -1;
    }
};

UiComposition loadPreviewComposition(const QString& filename);
QVector<UiComposition> loadPreviewCompositions(const QString& directory);

} // namespace miltech::ui
