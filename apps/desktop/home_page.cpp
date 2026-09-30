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
        "Сценарий определяет последовательность операций и используемый тракт. "
        "Оборудование и ячейки стенда не принадлежат одному изделию: их можно повторно использовать в разных проектах."), this);
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

    auto* genericKicker = new QLabel(QStringLiteral("ОБЩАЯ СТАНЦИЯ"), genericCard);
    genericKicker->setProperty("kicker", true);
    genericLayout->addWidget(genericKicker);

    auto* genericTitle = new QLabel(QStringLiteral("Проверить изделие"), genericCard);
    genericTitle->setProperty("title", true);
    genericLayout->addWidget(genericTitle);

    auto* genericText = new QLabel(QStringLiteral(
        "Опишите объект, выберите или отредактируйте сценарий, задайте макет отчёта и выполните проверку. "
        "Свободная проверка не создаёт запись в реестре специализированной поставки."), genericCard);
    genericText->setProperty("muted", true);
    genericText->setWordWrap(true);
    genericLayout->addWidget(genericText);

    auto* genericAction = actionButton(QStringLiteral("ПРОВЕРИТЬ ИЗДЕЛИЕ"), genericCard, true);
    genericLayout->addWidget(genericAction, 0, Qt::AlignLeft);
    root->addWidget(genericCard);

    auto* section = new QLabel(QStringLiteral("СПЕЦИАЛИЗИРОВАННЫЕ ПОСТАВКИ"), this);
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
        "Специализированная поставка КТМА: БСИ, УБСИ, РПУ и связанные тракты. "
        "Формальная проверка по ТУ и производственный контур используют реестр поставки. "
        "Например, УБСИ может проверяться как напрямую, так и через БСИ по тракту «Орбита» — это задаёт сценарий."), ktmaCard);
    ktmaText->setProperty("muted", true);
    ktmaText->setWordWrap(true);
    ktmaLayout->addWidget(ktmaText);

    auto* ktmaActions = new QHBoxLayout;
    ktmaActions->setSpacing(10);
    auto* tu = actionButton(QStringLiteral("ПРОВЕРКА ПО ТУ"), ktmaCard);
    auto* production = actionButton(QStringLiteral("ПРОИЗВОДСТВО"), ktmaCard);
    auto* administration = actionButton(QStringLiteral("АДМИНИСТРИРОВАНИЕ ПОСТАВКИ"), ktmaCard);
    ktmaActions->addWidget(tu);
    ktmaActions->addWidget(production);
    ktmaActions->addWidget(administration);
    ktmaActions->addStretch(1);
    ktmaLayout->addLayout(ktmaActions);
    root->addWidget(ktmaCard);
    root->addStretch(1);

    connect(genericAction, &QPushButton::clicked, this, &HomePage::genericCheckRequested);
    connect(tu, &QPushButton::clicked, this, &HomePage::tuRequested);
    connect(production, &QPushButton::clicked, this, &HomePage::productionRequested);
    connect(administration, &QPushButton::clicked, this, &HomePage::administrationRequested);
}

void HomePage::setProjectWorkflows(const QVector<HomeWorkflowEntry>& workflows)
{
    if (!rootLayout_) return;
    if (genericCard_) genericCard_->hide();
    if (ktmaCard_) ktmaCard_->hide();
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

    auto* heading = new QLabel(QStringLiteral("ПРОЦЕССЫ ПРОЕКТА"), card);
    heading->setProperty("kicker", true);
    layout->addWidget(heading);
    auto* hint = new QLabel(QStringLiteral(
        "Доступные проверки определяются выбранным проектом."), card);
    hint->setProperty("muted", true);
    layout->addWidget(hint);

    for (const auto& workflow : workflows) {
        const QString title = workflow.available
            ? workflow.title
            : QStringLiteral("%1 · недоступно").arg(workflow.title);
        auto* button = actionButton(title, card);
        button->setObjectName(QStringLiteral("projectWorkflow_%1").arg(workflow.id));
        button->setEnabled(workflow.available);
        button->setAccessibleDescription(workflow.unavailableReason);
        if (!workflow.unavailableReason.isEmpty())
            button->setToolTip(workflow.unavailableReason);
        layout->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, id = workflow.id] { emit workflowRequested(id); });
    }

    auto* stationAdmin = actionButton(QStringLiteral("АДМИНИСТРИРОВАНИЕ СТАНЦИИ"), card);
    stationAdmin->setObjectName(QStringLiteral("stationAdminAction"));
    stationAdmin->setToolTip(QStringLiteral(
        "Просмотр проекта, профиля оборудования, соединений и состояния ресурсов. Транспортом напрямую не управляет."));
    layout->addWidget(stationAdmin);
    connect(stationAdmin, &QPushButton::clicked,
            this, &HomePage::stationAdminRequested);

    projectWorkflowCard_ = card;
    rootLayout_->insertWidget(3, projectWorkflowCard_);
}
