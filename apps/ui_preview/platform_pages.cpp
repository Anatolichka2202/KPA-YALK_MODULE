#include "platform_pages.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>

#include <utility>

namespace miltech::ui {
namespace {

QLabel* text(const QString& value, const char* role = "body")
{
    auto* widget = new QLabel(value);
    widget->setWordWrap(true);
    widget->setProperty("role", role);
    return widget;
}

QFrame* panel(const QString& header, const QString& description)
{
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);
    layout->addWidget(text(header, "section"));
    if (!description.isEmpty()) layout->addWidget(text(description, "muted"));
    return frame;
}

QWidget* scrollPage(const QString& eyebrow,
                    const QString& title,
                    const QString& description,
                    QVBoxLayout** body)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    *body = new QVBoxLayout(content);
    (*body)->setContentsMargins(23, 22, 23, 22);
    (*body)->setSpacing(16);
    (*body)->addWidget(text(eyebrow, "eyebrow"));
    (*body)->addWidget(text(title, "pageTitle"));
    (*body)->addWidget(text(description, "muted"));
    scroll->setWidget(content);
    return scroll;
}

} // namespace

TuConfiguratorPage::TuConfiguratorPage(QString deliveryId, QWidget* parent)
    : QWidget(parent), deliveryId_(std::move(deliveryId))
{
    setObjectName(QStringLiteral("ui.configurator"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout* body = nullptr;
    layout->addWidget(scrollPage(
        QStringLiteral("КОНФИГУРАТОР / ТУ"),
        QStringLiteral("Конструктор ТУ-сборки"),
        QStringLiteral("Состав методики, операторских экранов и будущего отдельного приложения."),
        &body));

    auto* identity = panel(QStringLiteral("1 · Идентификатор сборки"),
                           QStringLiteral("Поля изменяют только предварительный UI-манифест. "
                                          "Создание приложения, публикация и работа с методиками не подключены."));
    auto* form = new QFormLayout;
    form->setSpacing(11);
    packageId_ = new QLineEdit(QStringLiteral("tu_%1_demo").arg(deliveryId_));
    packageId_->setObjectName(QStringLiteral("ui.tu.packageId"));
    packageId_->setAccessibleName(QStringLiteral("Идентификатор создаваемой ТУ-сборки"));
    title_ = new QLineEdit(QStringLiteral("ТУ · проверка изделия"));
    title_->setObjectName(QStringLiteral("ui.tu.title"));
    version_ = new QLineEdit(QStringLiteral("0.1.0"));
    version_->setObjectName(QStringLiteral("ui.tu.version"));
    form->addRow(QStringLiteral("ID приложения"), packageId_);
    form->addRow(QStringLiteral("Название"), title_);
    form->addRow(QStringLiteral("Версия проекта"), version_);
    static_cast<QVBoxLayout*>(identity->layout())->addLayout(form);
    body->addWidget(identity);

    auto* scenario = panel(QStringLiteral("2 · Методика"),
                           QStringLiteral("В production версия должна быть утверждена, "
                                          "а набор ресурсов и критерии — провалидированы ядром."));
    methodology_ = new QComboBox;
    methodology_->setObjectName(QStringLiteral("ui.tu.methodology"));
    methodology_->setAccessibleName(QStringLiteral("Сценарий для будущего ТУ-приложения"));
    if (deliveryId_ == QStringLiteral("ktma")) {
        methodology_->addItem(QStringLiteral("УБСИ · нормальные условия"), QStringLiteral("tu_normal"));
        methodology_->addItem(QStringLiteral("УБСИ · климатическая проверка"), QStringLiteral("tu_climate"));
        methodology_->addItem(QStringLiteral("УБСИ · отрицательная температура"), QStringLiteral("tu_climate_minus"));
    } else {
        methodology_->addItem(QStringLiteral("Эталонная инженерная проверка · демо"),
                              QStringLiteral("generic"));
    }
    scenario->layout()->addWidget(methodology_);
    scenario->layout()->addWidget(text(
        QStringLiteral("Редактор шагов, утверждение методики и проверка зависимостей должны предоставляться сервисом конфигуратора, а не этим виджетом."),
        "muted"));
    body->addWidget(scenario);

    auto* screens = panel(QStringLiteral("3 · Ограниченный интерфейс"),
                          QStringLiteral("Подготовка, выполнение и заключение обязательны. "
                                         "Дополнительные представления собираются из разрешённых модулей."));
    details_ = new QCheckBox(QStringLiteral("Таблица измерений"));
    details_->setObjectName(QStringLiteral("ui.tu.screen.measurements"));
    details_->setChecked(true);
    plot_ = new QCheckBox(QStringLiteral("Графики и тренды"));
    plot_->setObjectName(QStringLiteral("ui.tu.screen.trends"));
    events_ = new QCheckBox(QStringLiteral("Журнал операторских событий"));
    events_->setObjectName(QStringLiteral("ui.tu.screen.events"));
    events_->setChecked(true);
    screens->layout()->addWidget(details_);
    screens->layout()->addWidget(plot_);
    screens->layout()->addWidget(events_);
    body->addWidget(screens);

    auto* draftPanel = panel(QStringLiteral("4 · Предпросмотр состава сборки"),
                             QStringLiteral("Данные ниже — эскиз UI, не валидированный пакет ТУ и не файл выпуска."));
    validation_ = text({}, "warning");
    validation_->setObjectName(QStringLiteral("ui.tu.validation"));
    draftPanel->layout()->addWidget(validation_);
    draft_ = new QPlainTextEdit;
    draft_->setObjectName(QStringLiteral("ui.tu.draft"));
    draft_->setReadOnly(true);
    draft_->setLineWrapMode(QPlainTextEdit::NoWrap);
    draft_->setMinimumHeight(190);
    draft_->setAccessibleName(QStringLiteral("Предпросмотр декларативной конфигурации ТУ-интерфейса"));
    draftPanel->layout()->addWidget(draft_);
    packageButton_ = new QPushButton(QStringLiteral("Собрать и упаковать отдельное EXE"));
    packageButton_->setObjectName(QStringLiteral("ui.tu.packageExe"));
    packageButton_->setEnabled(false);
    packageButton_->setToolTip(QStringLiteral(
        "Требуется ядро конфигуратора, проверка методики и pipeline упаковки. UI Preview не выполняет сборку."));
    draftPanel->layout()->addWidget(packageButton_);
    draftPanel->layout()->addWidget(text(
        QStringLiteral("В выпуске потребуются валидированный сценарий, фиксированные разрешения, "
                       "зависимости, проверка комплекта и воспроизводимая сборка."),
        "muted"));
    body->addWidget(draftPanel);
    body->addStretch();

    connect(packageId_, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
    connect(title_, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
    connect(version_, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
    connect(methodology_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { refreshPreview(); });
    for (QCheckBox* option : {details_, plot_, events_}) {
        connect(option, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    }
    refreshPreview();
}

void TuConfiguratorPage::refreshPreview()
{
    const QRegularExpression idPattern(QStringLiteral("^[a-z][a-z0-9_]{2,63}$"));
    const QRegularExpression versionPattern(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    const bool validIdentity = idPattern.match(packageId_->text()).hasMatch()
        && !title_->text().trimmed().isEmpty()
        && versionPattern.match(version_->text()).hasMatch();
    validation_->setText(validIdentity
        ? QStringLiteral("Состав экранов сформирован · методика и упаковка НЕ проверены")
        : QStringLiteral("Проверьте идентификатор, название и номер версии · предпросмотр не валиден"));
    QJsonArray screens;
    for (const QString& screen : {QStringLiteral("tu.prepare"),
                                  QStringLiteral("tu.run"),
                                  QStringLiteral("tu.result")})
        screens.append(screen);
    QJsonArray extras;
    if (details_->isChecked()) extras.append(QStringLiteral("measurements"));
    if (plot_->isChecked()) extras.append(QStringLiteral("trend"));
    if (events_->isChecked()) extras.append(QStringLiteral("events"));
    QJsonObject object;
    object.insert(QStringLiteral("format"), QStringLiteral("ui-preview-draft-only"));
    object.insert(QStringLiteral("delivery"), deliveryId_);
    object.insert(QStringLiteral("application_id"), packageId_->text());
    object.insert(QStringLiteral("title"), title_->text());
    object.insert(QStringLiteral("version"), version_->text());
    object.insert(QStringLiteral("workflow_id"), methodology_->currentData().toString());
    object.insert(QStringLiteral("operator_screens"), screens);
    object.insert(QStringLiteral("optional_views"), extras);
    object.insert(QStringLiteral("backend_validated"), false);
    object.insert(QStringLiteral("executable_built"), false);
    draft_->setPlainText(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)));
}

StationAdministrationPage::StationAdministrationPage(QString deliveryName,
                                                     QStringList equipment,
                                                     QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ui.administration"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* body = nullptr;
    layout->addWidget(scrollPage(QStringLiteral("СЕРВИС / СТАНЦИЯ"),
                                 QStringLiteral("Администрирование"),
                                 QStringLiteral("Управление жизненным циклом конфигураций и доступом. "
                                                "Здесь представлены только интерфейсные модели."),
                                 &body));

    tabs_ = new QTabWidget;
    tabs_->setObjectName(QStringLiteral("ui.admin.tabs"));
    tabs_->setAccessibleName(QStringLiteral("Разделы администрирования станции"));

    auto* station = new QWidget;
    auto* stationLayout = new QVBoxLayout(station);
    stationLayout->addWidget(panel(
        QStringLiteral("Станция"),
        QStringLiteral("Профиль: не загружен · диагностика: не проводилась. "
                       "Текущий макет поставки: %1").arg(deliveryName)));
    stationLayout->addWidget(panel(
        QStringLiteral("Конфигурации подключений"),
        QStringLiteral("Предстоит связать интерфейс с StandProfile и конфигурацией ресурсов "
                       "через read-only API. Изменения профиля требуют проверки и публикации.")));
    stationLayout->addStretch();
    tabs_->addTab(station, QStringLiteral("Станция"));

    auto* equipmentPage = new QWidget;
    auto* equipmentLayout = new QVBoxLayout(equipmentPage);
    auto* equipmentTable = new QTableWidget(equipment.size(), 2);
    equipmentTable->setObjectName(QStringLiteral("ui.admin.resources"));
    equipmentTable->setHorizontalHeaderLabels(
        {QStringLiteral("Компонент UI-фикстуры"), QStringLiteral("Диагностика")});
    equipmentTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    equipmentTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    equipmentTable->verticalHeader()->setVisible(false);
    equipmentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int n = 0; n < equipment.size(); ++n) {
        equipmentTable->setItem(n, 0, new QTableWidgetItem(equipment.at(n)));
        equipmentTable->setItem(n, 1, new QTableWidgetItem(QStringLiteral("Не опрошено")));
    }
    equipmentLayout->addWidget(equipmentTable);
    equipmentLayout->addWidget(text(
        QStringLiteral("Реестр плагинов, привязки оборудования и маршруты сигналов будут получены от ядра станции."),
        "warning"));
    tabs_->addTab(equipmentPage, QStringLiteral("Оборудование"));

    auto* deliveries = new QWidget;
    auto* deliveriesLayout = new QVBoxLayout(deliveries);
    deliveriesLayout->addWidget(panel(
        QStringLiteral("Пакеты поставок и приложения"),
        QStringLiteral("Реестр установленных пакетов, версии, контроль целостности, "
                       "активация профиля, история публикаций и архив ТУ-сборок.")));
    auto* install = new QPushButton(QStringLiteral("Установить пакет"));
    install->setObjectName(QStringLiteral("ui.admin.install"));
    install->setEnabled(false);
    deliveriesLayout->addWidget(install);
    deliveriesLayout->addWidget(text(
        QStringLiteral("В UI Preview установка, обновление и удаление пакетов отключены."),
        "warning"));
    deliveriesLayout->addStretch();
    tabs_->addTab(deliveries, QStringLiteral("Поставки"));

    auto* access = new QWidget;
    auto* accessLayout = new QVBoxLayout(access);
    accessLayout->addWidget(panel(
        QStringLiteral("Пользователи и роли"),
        QStringLiteral("Оператор · Инженер · Администратор — проектная UX-модель; "
                       "подлинный источник учётных записей и RBAC пока не подключён.")));
    accessLayout->addWidget(panel(
        QStringLiteral("Журнал административных действий"),
        QStringLiteral("В будущей системе: кто, когда, что изменил, "
                       "какая была прежняя версия конфигурации и чем подтверждено изменение.")));
    auto* users = new QPushButton(QStringLiteral("Управление доступом"));
    users->setObjectName(QStringLiteral("ui.admin.permissions"));
    users->setEnabled(false);
    accessLayout->addWidget(users);
    accessLayout->addWidget(text(
        QStringLiteral("UI-скрытие кнопок не является проверкой прав: доступ обязано проверять ядро."),
        "warning"));
    accessLayout->addStretch();
    tabs_->addTab(access, QStringLiteral("Доступ и аудит"));

    body->addWidget(tabs_, 1);
}

} // namespace miltech::ui
