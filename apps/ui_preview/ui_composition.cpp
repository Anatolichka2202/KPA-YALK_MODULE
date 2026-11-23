#include "ui_composition.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace miltech::ui {
namespace {

const QSet<QString>& availableFullScreens()
{
    static const QSet<QString> ids = {
        QStringLiteral("overview"),
        QStringLiteral("workspace"),
        QStringLiteral("equipment"),
        QStringLiteral("scenarios"),
        QStringLiteral("results"),
        QStringLiteral("configurator"),
        QStringLiteral("administration")
    };
    return ids;
}

const QSet<QString>& availableTuScreens()
{
    static const QSet<QString> ids = {
        QStringLiteral("tu.prepare"),
        QStringLiteral("tu.run"),
        QStringLiteral("tu.result")
    };
    return ids;
}

const QSet<QString>& availableComponents()
{
    static const QSet<QString> ids = {
        QStringLiteral("parameters"),
        QStringLiteral("events"),
        QStringLiteral("trend"),
        QStringLiteral("topology")
    };
    return ids;
}

UiComposition parseComposition(const QByteArray& bytes)
{
    UiComposition composition;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        composition.error = QStringLiteral("Ошибка UI-манифеста: %1").arg(error.errorString());
        return composition;
    }

    const auto object = document.object();
    if (object.value(QStringLiteral("schema")).toInt() != 1) {
        composition.error = QStringLiteral("Неподдерживаемая версия UI-манифеста");
        return composition;
    }

    composition.id = object.value(QStringLiteral("id")).toString();
    composition.title = object.value(QStringLiteral("title")).toString();
    composition.kind = object.value(QStringLiteral("kind")).toString();
    if (composition.id.isEmpty() || composition.title.isEmpty()
        || (composition.kind != QStringLiteral("full")
            && composition.kind != QStringLiteral("tu"))) {
        composition.error = QStringLiteral("Не заданы идентификатор, тип или название UI-сборки");
        return composition;
    }

    const auto screenArray = object.value(QStringLiteral("screens")).toArray();
    QSet<QString> seen;
    const auto& allowed = composition.isTu() ? availableTuScreens() : availableFullScreens();
    for (const QJsonValue& screenValue : screenArray) {
        if (!screenValue.isObject()) {
            composition.error = QStringLiteral("Ожидалось описание экрана");
            return composition;
        }
        const QJsonObject screen = screenValue.toObject();
        const ScreenSpec spec{
            screen.value(QStringLiteral("id")).toString(),
            screen.value(QStringLiteral("title")).toString(),
            screen.value(QStringLiteral("group")).toString()
        };
        if (!allowed.contains(spec.id) || seen.contains(spec.id)
            || spec.title.isEmpty()
            || (spec.group != QStringLiteral("primary")
                && spec.group != QStringLiteral("service"))) {
            composition.error = QStringLiteral("Недопустимый или повторяющийся экран: %1").arg(spec.id);
            return composition;
        }
        seen.insert(spec.id);
        composition.screens.push_back(spec);
    }

    if (composition.screens.isEmpty()) {
        composition.error = QStringLiteral("UI-сборка не содержит экранов");
        return composition;
    }
    if (composition.isTu()) {
        if (seen != availableTuScreens()) {
            composition.error = QStringLiteral("Операторская ТУ-сборка обязана содержать подготовку, испытание и результат");
            return composition;
        }
    } else if (!seen.contains(QStringLiteral("overview"))
               || !seen.contains(QStringLiteral("workspace"))) {
        composition.error = QStringLiteral("Для полной станции обязательны обзор и рабочее пространство");
        return composition;
    }

    QSet<QString> seenWidgets;
    for (const QJsonValue& value : object.value(QStringLiteral("workspaceComponents")).toArray()) {
        const QString component = value.toString();
        if (composition.isTu() || !availableComponents().contains(component)
            || seenWidgets.contains(component)) {
            composition.error = QStringLiteral("Недопустимый компонент UI: %1").arg(component);
            return composition;
        }
        seenWidgets.insert(component);
        composition.workspaceComponents.push_back(component);
    }
    return composition;
}

} // namespace

UiComposition loadPreviewComposition(int variant)
{
    QString filename;
    switch (variant) {
    case 0: filename = QStringLiteral(":/ui/manifests/ktma.json"); break;
    case 1: filename = QStringLiteral(":/ui/manifests/reference.json"); break;
    case 2: filename = QStringLiteral(":/ui/manifests/tu_ubsi.json"); break;
    default: return UiComposition{{}, {}, {}, {}, {}, QStringLiteral("Неизвестный тип UI")};
    }

    QFile file(filename);
    if (!file.open(QFile::ReadOnly))
        return UiComposition{{}, {}, {}, {}, {},
                             QStringLiteral("Не удалось открыть UI-манифест: %1").arg(filename)};
    return parseComposition(file.readAll());
}

} // namespace miltech::ui
