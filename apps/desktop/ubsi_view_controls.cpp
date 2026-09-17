#include "ubsi_measurement_views.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPushButton>
#include <QWidget>

#include <algorithm>

namespace {

using ubsi::ui::ChannelPlane;
using ubsi::ui::ContactPlane;
using ubsi::ui::OverloadPlane;

constexpr int kControlHeight = 24;
constexpr int kResetWidth = 128;
constexpr int kScaleWidth = 128;
constexpr int kGap = 6;
constexpr int kMargin = 8;

QString controlStyle()
{
    return QStringLiteral(
        "QPushButton{background:#132a3d;color:#eaf4fb;border:1px solid #264257;"
        "border-radius:5px;padding:2px 8px;font-size:10px;font-weight:600;}"
        "QPushButton:hover{background:#17334a;border-color:#58a5ff;}"
        "QPushButton:pressed{background:#0e1e2c;}");
}

QPushButton* makeControl(const QString& text, const QString& name, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(kControlHeight);
    button->setStyleSheet(controlStyle());
    button->show();
    button->raise();
    return button;
}

class PrototypeViewControls final : public QObject
{
public:
    explicit PrototypeViewControls(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget) return QObject::eventFilter(watched, event);

        const auto type = event->type();
        if (type == QEvent::Polish || type == QEvent::Show) attach(widget);
        if (type == QEvent::Resize || type == QEvent::Show) layout(widget);
        return QObject::eventFilter(watched, event);
    }

private:
    static void attach(QWidget* widget)
    {
        if (auto* plane = dynamic_cast<ChannelPlane*>(widget)) {
            if (plane->objectName() != QStringLiteral("yalkChannelHistogram")
                && plane->objectName() != QStringLiteral("ytpChannelHistogram")) {
                return;
            }
            if (plane->property("prototypeControlsAttached").toBool()) return;
            plane->setProperty("prototypeControlsAttached", true);

            const bool yalk = plane->objectName() == QStringLiteral("yalkChannelHistogram");
            auto* full = makeControl(
                QStringLiteral("Полная шкала"),
                yalk ? QStringLiteral("yalkFullScaleButton")
                     : QStringLiteral("ytpFullScaleButton"),
                plane);
            auto* reset = makeControl(
                QStringLiteral("Сбросить масштаб"),
                yalk ? QStringLiteral("yalkResetZoomButton")
                     : QStringLiteral("ytpResetZoomButton"),
                plane);

            QObject::connect(full, &QPushButton::clicked, plane, [plane, full] {
                plane->toggleFullScale();
                full->setText(plane->fullScale()
                    ? QStringLiteral("Рабочий диапазон")
                    : QStringLiteral("Полная шкала"));
            });
            QObject::connect(reset, &QPushButton::clicked, plane, [plane, full] {
                plane->resetView();
                full->setText(QStringLiteral("Полная шкала"));
            });
            layout(plane);
            return;
        }

        if (auto* plane = dynamic_cast<ContactPlane*>(widget)) {
            if (plane->property("prototypeControlsAttached").toBool()) return;
            plane->setProperty("prototypeControlsAttached", true);
            auto* reset = makeControl(QStringLiteral("Сбросить масштаб"),
                                      QStringLiteral("yalkContactResetZoomButton"), plane);
            QObject::connect(reset, &QPushButton::clicked, plane, [plane] { plane->resetView(); });
            layout(plane);
            return;
        }

        if (auto* plane = dynamic_cast<OverloadPlane*>(widget)) {
            if (plane->property("prototypeControlsAttached").toBool()) return;
            plane->setProperty("prototypeControlsAttached", true);
            auto* reset = makeControl(QStringLiteral("Сбросить масштаб"),
                                      QStringLiteral("yalkOverloadResetZoomButton"), plane);
            QObject::connect(reset, &QPushButton::clicked, plane, [plane] { plane->resetView(); });
            layout(plane);
        }
    }

    static void layout(QWidget* widget)
    {
        if (widget->width() <= 0) return;

        if (auto* plane = dynamic_cast<ChannelPlane*>(widget)) {
            auto* full = plane->findChild<QPushButton*>(
                plane->objectName() == QStringLiteral("yalkChannelHistogram")
                    ? QStringLiteral("yalkFullScaleButton")
                    : QStringLiteral("ytpFullScaleButton"));
            auto* reset = plane->findChild<QPushButton*>(
                plane->objectName() == QStringLiteral("yalkChannelHistogram")
                    ? QStringLiteral("yalkResetZoomButton")
                    : QStringLiteral("ytpResetZoomButton"));
            if (!full || !reset) return;
            reset->setGeometry(std::max(0, plane->width() - kMargin - kResetWidth),
                               3, kResetWidth, kControlHeight);
            full->setGeometry(std::max(0, reset->x() - kGap - kScaleWidth),
                              3, kScaleWidth, kControlHeight);
            full->raise();
            reset->raise();
            return;
        }

        const QString name = dynamic_cast<ContactPlane*>(widget)
            ? QStringLiteral("yalkContactResetZoomButton")
            : dynamic_cast<OverloadPlane*>(widget)
                ? QStringLiteral("yalkOverloadResetZoomButton") : QString();
        if (name.isEmpty()) return;
        if (auto* reset = widget->findChild<QPushButton*>(name)) {
            reset->setGeometry(std::max(0, widget->width() - kMargin - kResetWidth),
                               3, kResetWidth, kControlHeight);
            reset->raise();
        }
    }
};

void installPrototypeViewControls()
{
    auto* app = QCoreApplication::instance();
    if (!app) return;
    auto* controller = new PrototypeViewControls(app);
    app->installEventFilter(controller);
}

Q_COREAPP_STARTUP_FUNCTION(installPrototypeViewControls)

} // namespace
