#include "home_page.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QPushButton* actionButton(const QString& text, QWidget* parent, bool primary = false)
{
    auto* button = new QPushButton(text, parent);
    button->setMinimumHeight(48);
    button->setCursor(Qt::PointingHandCursor);
    if (primary) button->setObjectName(QStringLiteral("homePrimaryAction"));
    return button;
}

} // namespace

HomePage::HomePage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("stationHomePage"));
    setStyleSheet(QStringLiteral(
        "#stationHomePage{background:#08131d;color:#eaf4fb;}"
        "QFrame[card='true']{background:#0e1e2c;border:1px solid #264257;border-radius:10px;}"
        "QLabel[kicker='true']{color:#67d8eb;font-size:12px;font-weight:800;}"
        "QLabel[title='true']{color:#eaf4fb;font-size:22px;font-weight:800;}"
        "QLabel[muted='true']{color:#8ea6b7;}"
        "QPushButton{background:#132a3d;color:#eaf4fb;border:1px solid #264257;"
        "border-radius:7px;padding:10px 16px;font-size:13px;font-weight:700;}"
        "QPushButton:hover{background:#17334a;border-color:#58a5ff;}"
        "QPushButton#homePrimaryAction{background:#2e7de9;border-color:#58a5ff;font-size:15px;}"));

    auto* root = new QVBoxLayout(this);
    rootLayout_ = root;
    root->setContentsMargins(54, 46, 54, 46);
    root->setSpacing(22);

    auto* brand = new QLabel(QStringLiteral("MILTECHSTATION"), this);
    brand->setProperty("kicker", true);
    root->addWidget(brand);

    auto* title = new QLabel(QStringLiteral("Универсальная станция испытаний"), this);
    title->setProperty("title", true);
    title->setStyleSheet(QStringLiteral("font-size:32px;font-weight:850;"));
    root->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral(
        "Выберите поставку или свободный режим станции."), this);
    subtitle->setProperty("muted", true);
    subtitle->setWordWrap(true);
    subtitle->setMaximumWidth(1100);
    root->addWidget(subtitle);

    auto* genericCard = new QFrame(this);
    genericCard_ = genericCard;
    genericCard->setProperty("card", true);
    auto* genericLayout = new QVBoxLayout(genericCard);
    genericLayout->setContentsMargins(24, 22, 24, 22);
    genericLayout->setSpacing(10);

    auto* genericKicker = new QLabel(QStringLiteral("MILTECHSTATION · УНИВЕРСАЛЬНЫЙ КОНТУР"), genericCard);
    genericKicker->setProperty("kicker", true);
    genericLayout->addWidget(genericKicker);

    auto* genericTitle = new QLabel(QStringLiteral("Свободный режим"), genericCard);
    genericTitle->setProperty("title", true);
    genericLayout->addWidget(genericTitle);

    auto* genericText = new QLabel(QStringLiteral(
        "Инженерское рабочее пространство MilTechStation: выбирайте доступные источники и адреса, "
        "наблюдайте live-данные и выполняйте разрешённые командами плагинов действия."), genericCard);
    genericText->setProperty("muted", true);
    genericText->setWordWrap(true);
    genericLayout->addWidget(genericText);

    auto* genericAction = actionButton(QStringLiteral("ОТКРЫТЬ СВОБОДНЫЙ РЕЖИМ"), genericCard, true);
    genericAction->setObjectName(QStringLiteral("genericFreeAction"));
    genericAction->setAccessibleDescription(QStringLiteral(
        "Открывает независимое инженерное рабочее пространство MilTechStation; "
        "не открывает выбор YAML-сценария КТМА."));
    genericLayout->addWidget(genericAction, 0, Qt::AlignLeft);
    root->addWidget(genericCard);

    auto* section = new QLabel(QStringLiteral("ПОСТАВКА"), this);
    section->setObjectName(QStringLiteral("legacyWorkflowSection"));
    section->setProperty("kicker", true);
    root->addWidget(section);

    auto* ktmaCard = new QFrame(this);
    ktmaCard_ = ktmaCard;
    ktmaCard->setProperty("card", true);
    auto* ktmaLayout = new QVBoxLayout(ktmaCard);
    ktmaLayout->setContentsMargins(24, 22, 24, 22);
    ktmaLayout->setSpacing(10);

    auto* ktmaTitle = new QLabel(QStringLiteral("КТМА"), ktmaCard);
    ktmaTitle->setProperty("title", true);
    ktmaLayout->addWidget(ktmaTitle);

    auto* ktmaText = new QLabel(QStringLiteral(
        "Готовые проверки и производственные маршруты оборудования КТМА."), ktmaCard);
    ktmaText->setProperty("muted", true);
    ktmaText->setWordWrap(true);
    ktmaLayout->addWidget(ktmaText);

    auto* openKtma = actionButton(QStringLiteral("ОТКРЫТЬ КТМА"), ktmaCard, true);
    openKtma->setObjectName(QStringLiteral("openKtmaDelivery"));
    ktmaLayout->addWidget(openKtma, 0, Qt::AlignLeft);
    root->addWidget(ktmaCard);

    stationAdminCard_ = new QFrame(this);
    stationAdminCard_->setProperty("card", true);
    auto* adminLayout = new QVBoxLayout(stationAdminCard_);
    auto* adminTitle = new QLabel(QStringLiteral("Администрирование"), stationAdminCard_);
    adminTitle->setProperty("title", true);
    adminLayout->addWidget(adminTitle);
    auto* adminAction = actionButton(QStringLiteral("НЕ РЕАЛИЗОВАНО"), stationAdminCard_);
    adminAction->setObjectName(QStringLiteral("stationAdminAction"));
    adminAction->setEnabled(false);
    adminAction->setAccessibleDescription(QStringLiteral(
        "Администрирование станции пока не реализовано и не открывает технические настройки."));
    adminLayout->addWidget(adminAction, 0, Qt::AlignLeft);
    root->addWidget(stationAdminCard_);
    root->addStretch(1);

    connect(openKtma, &QPushButton::clicked, this, [this] { showKtmaMenu(true); });
    connect(genericAction, &QPushButton::clicked, this, &HomePage::freeWorkspaceRequested);
}

void HomePage::setProjectWorkflows(const QVector<HomeWorkflowEntry>& workflows)
{
    if (!rootLayout_) return;
    if (genericCard_) genericCard_->show();
    if (ktmaCard_) ktmaCard_->show();
    if (auto* section = findChild<QLabel*>(QStringLiteral("legacyWorkflowSection")))
        section->hide();

    if (projectWorkflowCard_) {
        rootLayout_->removeWidget(projectWorkflowCard_);
        delete projectWorkflowCard_;
        projectWorkflowCard_ = nullptr;
    }

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("projectWorkflowCard"));
    card->setProperty("card", true);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(10);

    auto* back = actionButton(QStringLiteral("← Главная"), card);
    back->setObjectName(QStringLiteral("backFromKtma"));
    layout->addWidget(back, 0, Qt::AlignLeft);
    connect(back, &QPushButton::clicked, this, [this] { showKtmaMenu(false); });

    auto* heading = new QLabel(QStringLiteral("КТМА · ПРОВЕРКИ"), card);
    heading->setProperty("kicker", true);
    layout->addWidget(heading);
    auto* hint = new QLabel(QStringLiteral(
        "ТУ 5.6 — фиксированный маршрут УБСИ. Производственные пакеты выбираются внутри производственной сессии."), card);
    hint->setProperty("muted", true);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    for (const auto& workflow : workflows) {
        if (workflow.kind == QStringLiteral("free")) continue;
        if (!workflow.available) continue;
        QString title = workflow.title;
        if (workflow.kind == QStringLiteral("tu"))
            title = QStringLiteral("ПРОВЕРКА УБСИ ПО ТУ");
        else if (workflow.kind == QStringLiteral("production"))
            title = QStringLiteral("ПРОИЗВОДСТВО УБСИ");
        else if (workflow.kind == QStringLiteral("free"))
            title = QStringLiteral("СВОБОДНАЯ ИНЖЕНЕРНАЯ ПРОВЕРКА");
        auto* button = actionButton(title, card, workflow.kind == QStringLiteral("tu"));
        button->setObjectName(QStringLiteral("projectWorkflow_%1").arg(workflow.id));
        layout->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, id = workflow.id] { emit workflowRequested(id); });
    }

    for (const auto& title : {
             QStringLiteral("Постклиматическая проверка — не реализовано"),
             QStringLiteral("Проверка в климате — не реализовано"),
             QStringLiteral("Свободная проверка КТМА — не реализовано")}) {
        auto* placeholder = actionButton(title, card);
        placeholder->setEnabled(false);
        layout->addWidget(placeholder);
    }

    projectWorkflowCard_ = card;
    rootLayout_->insertWidget(rootLayout_->indexOf(genericCard_), projectWorkflowCard_);
    showKtmaMenu(false);
}

void HomePage::showKtmaMenu(bool visible)
{
    if (projectWorkflowCard_) projectWorkflowCard_->setVisible(visible);
    if (genericCard_) genericCard_->setVisible(!visible);
    if (ktmaCard_) ktmaCard_->setVisible(!visible);
    if (stationAdminCard_) stationAdminCard_->setVisible(!visible);
    if (auto* section = findChild<QLabel*>(QStringLiteral("legacyWorkflowSection")))
        section->setVisible(!visible);
}
