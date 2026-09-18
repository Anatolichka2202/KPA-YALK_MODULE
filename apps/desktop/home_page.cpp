#include "home_page.h"

#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

HomePage::HomePage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ktmaHomePage"));
    setStyleSheet(QStringLiteral("#ktmaHomePage{background:#08131d;}"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(48, 48, 48, 48);
    auto* label = new QLabel(QStringLiteral("MilTechStation · УБСИ · ТУ"), this);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("color:#8ea6b7;font-size:15px;font-weight:600;"));
    layout->addStretch();
    layout->addWidget(label);
    layout->addStretch();

    // This branch is a TU-only delivery. Enter the only supported workflow as
    // soon as the event loop starts; production/admin remain compiled solely as
    // shared implementation dependencies and are not part of the operator UI.
    QTimer::singleShot(0, this, [this] { emit tuRequested(); });
}
