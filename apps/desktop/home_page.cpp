#include "home_page.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

HomePage::HomePage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ktmaHomePage"));
    setStyleSheet(QStringLiteral(
        "#ktmaHomePage { background:#08131d; }"
        "QPushButton { background:#0e1e2c; color:#eaf4fb; border:1px solid #264257; border-radius:10px; "
        "padding:24px; font-size:22px; font-weight:700; text-align:center; }"
        "QPushButton:hover { border-color:#58a5ff; background:#132a3d; }"
        "QPushButton#primary { background:qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2d7fe9, stop:1 #2266c4); border-color:#4e98f0; }"
    ));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setAlignment(Qt::AlignCenter);

    auto* card = new QFrame(this);
    card->setFixedWidth(980);
    card->setStyleSheet(QStringLiteral(
        "QFrame { "
        "background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(16,35,51,250), stop:1 rgba(10,25,37,250)); "
        "border: 1px solid #264257; "
        "border-radius: 12px; "
        "padding: 42px; "
        "}"
    ));

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setSpacing(0);
    cardLayout->setAlignment(Qt::AlignCenter);

    auto* brandKicker = new QLabel(QStringLiteral("MILTECH STATION / КТМА"), card);
    brandKicker->setAlignment(Qt::AlignCenter);
    brandKicker->setStyleSheet(QStringLiteral("color:#61d5e8; font-size:14px; letter-spacing:0.16em; text-transform:uppercase; margin-bottom:8px;"));
    cardLayout->addWidget(brandKicker);

    auto* title = new QLabel(QStringLiteral("УБСИ"), card);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("color:#eaf4fb; font-size:42px; font-weight:700; margin: 8px 0 4px;"));
    cardLayout->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral("Операторский интерфейс испытаний"), card);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("color:#8ea6b7; font-size:16px; margin: 0 0 30px;"));
    cardLayout->addWidget(subtitle);

    auto* buttonGrid = new QGridLayout;
    buttonGrid->setSpacing(18);

    auto* btnProd = new QPushButton(QStringLiteral("ПРОИЗВОДСТВО"), card);
    auto* btnTu = new QPushButton(QStringLiteral("ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ"), card);
    auto* btnAdmin = new QPushButton(QStringLiteral("АДМИНИСТРИРОВАНИЕ"), card);

    // Setup button styles for grid
    auto setupModeBtn = [&](QPushButton* btn, const QString& title, const QString& desc) {
        btn->setMinimumHeight(150);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { "
            "background: #0e1e2c; color: #eaf4fb; border: 1px solid #264257; border-radius: 10px; "
            "text-align: left; padding: 20px; "
            "}"
            "QPushButton:hover { border-color: #58a5ff; background: #132a3d; }"
        ));
        // We can't easily put a <strong> and <span> in a QPushButton without custom painting or HTML.
        // For now, using HTML if supported, or just a single label.
        btn->setText(QString("<b>%1</b><br><span style='color:#8ea6b7; font-size:14px; font-weight:normal;'>%2</span>").arg(title, desc));
    };

    setupModeBtn(btnProd, "ПРОИЗВОДСТВО", "Очередь изделий, подготовка и производственный прогон.");
    setupModeBtn(btnTu, "ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ", "Один маршрут ТУ: готовность стенда, автоматическая проверка, ФИО перед отчётом.");
    setupModeBtn(btnAdmin, "АДМИНИСТРИРОВАНИЕ", "Регистрация УБСИ, состав, замены, переносы и история.");

    buttonGrid->addWidget(btnProd, 0, 0);
    buttonGrid->addWidget(btnTu, 0, 1);
    buttonGrid->addWidget(btnAdmin, 0, 2);

    cardLayout->addLayout(buttonGrid);
    rootLayout->addWidget(card);


    connect(btnProd, &QPushButton::clicked, this, [this] { emit productionRequested(); });
    connect(btnTu, &QPushButton::clicked, this, [this] { emit tuRequested(); });
    connect(btnAdmin, &QPushButton::clicked, this, [this] { emit administrationRequested(); });
}
