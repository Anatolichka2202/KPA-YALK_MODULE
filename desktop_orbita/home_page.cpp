#include "home_page.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QPushButton* makeHomeAction(const QString& title, const QString& description, QWidget* parent)
{
    auto* button = new QPushButton(title + QStringLiteral("\n") + description, parent);
    button->setMinimumHeight(104);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background:#1a222c; color:#e6edf5; border:1px solid #344557;"
        " border-radius:6px; padding:18px 24px; text-align:left;"
        " font-size:18px; font-weight:700; }"
        "QPushButton:hover { background:#223142; border-color:#5d87ad; }"
        "QPushButton:pressed { background:#16202a; }"));
    return button;
}

} // namespace

HomePage::HomePage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ktmaHomePage"));
    setStyleSheet(QStringLiteral("#ktmaHomePage { background:#0e1115; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(64, 52, 64, 52);
    layout->setSpacing(18);

    auto* title = new QLabel(QStringLiteral("КТМА"), this);
    title->setStyleSheet(QStringLiteral("color:#f2f6fa; font-size:32px; font-weight:700;"));
    layout->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral("Выберите рабочий контур"), this);
    subtitle->setStyleSheet(QStringLiteral("color:#9aa7b5; font-size:15px;"));
    layout->addWidget(subtitle);
    layout->addSpacing(20);

    auto* production = makeHomeAction(
        QStringLiteral("ПРОИЗВОДСТВО"),
        QStringLiteral("Сборка, этапы и результаты изделия"), this);
    auto* tu = makeHomeAction(
        QStringLiteral("ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ"),
        QStringLiteral("Нормативная проверка УБСИ"), this);
    auto* administration = makeHomeAction(
        QStringLiteral("АДМИНИСТРИРОВАНИЕ"),
        QStringLiteral("Регистратор, состав изделий и настройки"), this);

    layout->addWidget(production);
    layout->addWidget(tu);
    layout->addWidget(administration);
    layout->addStretch();

    connect(production, &QPushButton::clicked, this, &HomePage::productionRequested);
    connect(tu, &QPushButton::clicked, this, &HomePage::tuRequested);
    connect(administration, &QPushButton::clicked, this, &HomePage::administrationRequested);
}
