#include "home_page.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QPushButton* modeButton(const QString& title, const QString& description, QWidget* parent)
{
    auto* button = new QPushButton(parent);
    button->setMinimumHeight(150);
    button->setCursor(Qt::PointingHandCursor);
    button->setText(title + QStringLiteral("\n\n") + description);
    button->setStyleSheet(QStringLiteral(
        "QPushButton{background:#102333;color:#eaf4fb;border:1px solid #264257;"
        "border-radius:8px;padding:22px 24px;text-align:left;font-size:17px;font-weight:700;}"
        "QPushButton:hover{background:#132a3d;border-color:#58a5ff;}"
        "QPushButton:pressed{background:#0e1e2c;}"));
    return button;
}

} // namespace

HomePage::HomePage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ktmaHomePage"));
    setStyleSheet(QStringLiteral("#ktmaHomePage{background:#08131d;}"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(56, 56, 56, 56);
    outer->setSpacing(0);
    outer->addStretch(1);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("homeCard"));
    card->setMaximumWidth(980);
    card->setStyleSheet(QStringLiteral(
        "#homeCard{background:#0c1b28;border:1px solid #1a3346;border-radius:8px;}"));

    auto* content = new QVBoxLayout(card);
    content->setContentsMargins(42, 42, 42, 42);
    content->setSpacing(14);

    auto* brand = new QLabel(QStringLiteral("MilTechStation / КТМА"), card);
    brand->setStyleSheet(QStringLiteral(
        "color:#8ea6b7;font-size:13px;font-weight:600;letter-spacing:1px;"));
    content->addWidget(brand);

    auto* title = new QLabel(QStringLiteral("УБСИ"), card);
    title->setStyleSheet(QStringLiteral(
        "color:#eaf4fb;font-size:34px;font-weight:750;margin-top:2px;"));
    content->addWidget(title);

    auto* subtitle = new QLabel(
        QStringLiteral("Выберите рабочий контур"), card);
    subtitle->setStyleSheet(QStringLiteral("color:#8ea6b7;font-size:14px;"));
    content->addWidget(subtitle);
    content->addSpacing(18);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(18);

    auto* production = modeButton(
        QStringLiteral("ПРОИЗВОДСТВО"),
        QStringLiteral("Сессия изделий, этапы проверки и результаты"), card);
    production->setObjectName(QStringLiteral("homeProduction"));
    production->setStyleSheet(QStringLiteral(
        "QPushButton{background:#102b27;color:#eaf4fb;border:1px solid #267152;"
        "border-radius:8px;padding:22px 24px;text-align:left;font-size:17px;font-weight:700;}"
        "QPushButton:hover{background:#15372f;border-color:#35cf79;}"
        "QPushButton:pressed{background:#0e241f;}"));

    auto* tu = modeButton(
        QStringLiteral("ТУ"),
        QStringLiteral("Приёмо-сдаточная проверка зарегистрированного УБСИ"), card);
    tu->setObjectName(QStringLiteral("homeTu"));

    auto* administration = modeButton(
        QStringLiteral("АДМИНИСТРИРОВАНИЕ"),
        QStringLiteral("Регистрация, состав изделия и история"), card);
    administration->setObjectName(QStringLiteral("homeAdministration"));

    grid->addWidget(production, 0, 0);
    grid->addWidget(tu, 0, 1);
    grid->addWidget(administration, 0, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    content->addLayout(grid);

    auto* note = new QLabel(
        QStringLiteral("Операторский HMI · КТМА / УБСИ"), card);
    note->setStyleSheet(QStringLiteral("color:#61788a;font-size:11px;margin-top:8px;"));
    content->addWidget(note);

    auto* row = new QGridLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(card, 0, 0);
    row->setColumnStretch(0, 1);
    outer->addLayout(row);
    outer->addStretch(1);

    connect(production, &QPushButton::clicked, this, &HomePage::productionRequested);
    connect(tu, &QPushButton::clicked, this, &HomePage::tuRequested);
    connect(administration, &QPushButton::clicked, this, &HomePage::administrationRequested);
}
