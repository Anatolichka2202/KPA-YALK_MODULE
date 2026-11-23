#include "hmi_delivery.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <utility>

namespace miltech::ui {
namespace {

bool goodId(const QString& value)
{
    static const QRegularExpression expression(QStringLiteral("^[a-z][a-z0-9_-]{0,63}$"));
    return expression.match(value).hasMatch();
}

bool goodTitle(const QString& title)
{
    return !title.trimmed().isEmpty() && title.size() <= 100;
}

void errorTo(QString* error, const QString& message)
{
    if (error) *error = message;
}

} // namespace

HmiDeliveryCollection::HmiDeliveryCollection(QString deliveryId, QObject* parent)
    : QObject(parent), deliveryId_(std::move(deliveryId))
{
}

HmiDeliveryCollection::~HmiDeliveryCollection()
{
    for (const auto& item : pages_) delete item.document;
}

HmiPageRecord HmiDeliveryCollection::pageAt(int index) const
{
    return index >= 0 && index < pages_.size() ? pages_.at(index) : HmiPageRecord{};
}

QString HmiDeliveryCollection::uniqueId() const
{
    for (int i = 1; i < 10000; ++i) {
        const QString id = QStringLiteral("view_%1").arg(i);
        bool exists = false;
        for (const auto& page : pages_) {
            if (page.id == id) {
                exists = true;
                break;
            }
        }
        if (!exists) return id;
    }
    return {};
}

void HmiDeliveryCollection::replacePages(QVector<HmiPageRecord> replacement)
{
    QVector<HmiPageRecord> old = std::move(pages_);
    pages_ = std::move(replacement);
    selectedIndex_ = 0;
    emit pagesChanged();
    emit currentPageChanged(selectedIndex_);
    for (const auto& page : old) delete page.document;
}

bool HmiDeliveryCollection::initialize(const QVector<HmiPageSpec>& templates, QString* error)
{
    if (templates.isEmpty() || templates.size() > 12) {
        errorTo(error, QStringLiteral("Поставка не содержит HMI-страниц или превышен предел 12"));
        return false;
    }
    QVector<HmiPageRecord> next;
    QSet<QString> seen;
    for (const auto& entry : templates) {
        if (!goodId(entry.id) || seen.contains(entry.id) || !goodTitle(entry.title)) {
            errorTo(error, QStringLiteral("Некорректный идентификатор/название HMI"));
            for (const auto& p : next) delete p.document;
            return false;
        }
        QFile file(entry.source);
        auto* document = new HmiDocument(deliveryId_, this);
        QString issue;
        if (!file.open(QIODevice::ReadOnly)
            || !document->loadFromJson(file.readAll(), &issue)) {
            errorTo(error, QStringLiteral("Ошибка шаблона HMI «%1»: %2").arg(entry.title,
                     issue.isEmpty() ? QStringLiteral("файл не найден") : issue));
            delete document;
            for (const auto& p : next) delete p.document;
            return false;
        }
        seen.insert(entry.id);
        next.push_back({entry.id, entry.title, document});
    }
    replacePages(std::move(next));
    if (error) error->clear();
    return true;
}

QString HmiDeliveryCollection::addPage(QString title)
{
    if (pages_.size() >= 12 || !goodTitle(title)) return {};
    const QString id = uniqueId();
    if (id.isEmpty()) return {};
    auto* document = new HmiDocument(deliveryId_, this);
    document->add(QStringLiteral("label"));
    pages_.push_back({id, title.trimmed(), document});
    selectedIndex_ = pages_.size() - 1;
    emit pagesChanged();
    emit currentPageChanged(selectedIndex_);
    return id;
}

QString HmiDeliveryCollection::duplicatePage(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex >= pages_.size() || pages_.size() >= 12) return {};
    const HmiPageRecord source = pages_.at(sourceIndex);
    const QString newId = uniqueId();
    if (newId.isEmpty()) return {};
    auto* document = new HmiDocument(deliveryId_, this);
    if (!document->loadFromJson(source.document->toJson())) {
        delete document;
        return {};
    }
    pages_.push_back({newId, source.title + QStringLiteral(" · копия"), document});
    selectedIndex_ = pages_.size() - 1;
    emit pagesChanged();
    emit currentPageChanged(selectedIndex_);
    return newId;
}

bool HmiDeliveryCollection::renamePage(int index, const QString& title)
{
    if (index < 0 || index >= pages_.size() || !goodTitle(title)) return false;
    pages_[index].title = title.trimmed();
    emit pagesChanged();
    emit currentPageChanged(selectedIndex_);
    return true;
}

bool HmiDeliveryCollection::deletePage(int index)
{
    if (pages_.size() <= 1 || index < 0 || index >= pages_.size()) return false;
    HmiDocument* document = pages_.at(index).document;
    pages_.removeAt(index);
    if (index < selectedIndex_) --selectedIndex_;
    if (selectedIndex_ >= pages_.size()) selectedIndex_ = pages_.size() - 1;
    emit pagesChanged();
    emit currentPageChanged(selectedIndex_);
    delete document;
    return true;
}

bool HmiDeliveryCollection::setCurrentIndex(int index)
{
    if (index < 0 || index >= pages_.size()) return false;
    if (selectedIndex_ == index) return true;
    selectedIndex_ = index;
    emit currentPageChanged(index);
    return true;
}

QByteArray HmiDeliveryCollection::toJson() const
{
    QJsonArray pages;
    for (const auto& page : pages_) {
        QJsonObject record;
        record.insert(QStringLiteral("id"), page.id);
        record.insert(QStringLiteral("title"), page.title);
        record.insert(QStringLiteral("document"),
                      QJsonDocument::fromJson(page.document->toJson()).object());
        pages.append(record);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("format"), QStringLiteral("miltech.ui.hmi.delivery-draft"));
    root.insert(QStringLiteral("delivery_id"), deliveryId_);
    root.insert(QStringLiteral("pages"), pages);
    root.insert(QStringLiteral("runtime_connected"), false);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool HmiDeliveryCollection::loadFromJson(const QByteArray& json, QString* error)
{
    QJsonParseError issue{};
    const auto parsed = QJsonDocument::fromJson(json, &issue);
    if (issue.error != QJsonParseError::NoError || !parsed.isObject()) {
        errorTo(error, QStringLiteral("Неверный JSON набора HMI"));
        return false;
    }
    const auto root = parsed.object();
    if (root.value(QStringLiteral("schema")).toInt() != 1
        || root.value(QStringLiteral("format")).toString()
            != QStringLiteral("miltech.ui.hmi.delivery-draft")
        || root.value(QStringLiteral("delivery_id")).toString() != deliveryId_
        || !root.value(QStringLiteral("pages")).isArray()) {
        errorTo(error, QStringLiteral("Неверный формат или поставка набора HMI"));
        return false;
    }
    const auto array = root.value(QStringLiteral("pages")).toArray();
    if (array.isEmpty() || array.size() > 12) {
        errorTo(error, QStringLiteral("Набор должен содержать 1–12 HMI-страниц"));
        return false;
    }
    QVector<HmiPageRecord> next;
    QSet<QString> seen;
    for (const auto& value : array) {
        if (!value.isObject()) {
            errorTo(error, QStringLiteral("Неверная запись HMI-страницы"));
            for (const auto& page : next) delete page.document;
            return false;
        }
        const auto record = value.toObject();
        const QString id = record.value(QStringLiteral("id")).toString();
        const QString title = record.value(QStringLiteral("title")).toString();
        if (!goodId(id) || seen.contains(id) || !goodTitle(title)
            || !record.value(QStringLiteral("document")).isObject()) {
            errorTo(error, QStringLiteral("Ошибка описания HMI-страницы"));
            for (const auto& page : next) delete page.document;
            return false;
        }
        auto* document = new HmiDocument(deliveryId_, this);
        QString description;
        if (!document->loadFromJson(QJsonDocument(record.value(QStringLiteral("document")).toObject())
                                        .toJson(), &description)) {
            errorTo(error, QStringLiteral("Ошибка HMI «%1»: %2").arg(title, description));
            delete document;
            for (const auto& page : next) delete page.document;
            return false;
        }
        seen.insert(id);
        next.push_back({id, title, document});
    }
    replacePages(std::move(next));
    if (error) error->clear();
    return true;
}

HmiDeliveryEditor::HmiDeliveryEditor(HmiDeliveryCollection* pages, QWidget* parent)
    : QWidget(parent), pages_(pages)
{
    setObjectName(QStringLiteral("ui.hmi.deliveryEditor"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(15, 14, 15, 14);
    root->setSpacing(9);
    auto* heading = new QLabel(QStringLiteral("HMI-представления · поставка %1").arg(pages_->deliveryId()));
    heading->setProperty("role", "pageTitle");
    root->addWidget(heading);

    auto* line = new QHBoxLayout;
    line->addWidget(new QLabel(QStringLiteral("Экран:")));
    pageSelector_ = new QComboBox;
    pageSelector_->setObjectName(QStringLiteral("ui.hmi.pageSelector"));
    pageSelector_->setAccessibleName(QStringLiteral("Выбранное HMI-представление"));
    line->addWidget(pageSelector_, 1);
    root->addLayout(line);

    auto* pageActions = new QHBoxLayout;
    add_ = new QPushButton(QStringLiteral("Новый"));
    add_->setObjectName(QStringLiteral("ui.hmi.newPage"));
    pageActions->addWidget(add_);
    duplicate_ = new QPushButton(QStringLiteral("Дублировать"));
    duplicate_->setObjectName(QStringLiteral("ui.hmi.duplicatePage"));
    pageActions->addWidget(duplicate_);
    remove_ = new QPushButton(QStringLiteral("Удалить"));
    remove_->setObjectName(QStringLiteral("ui.hmi.deletePage"));
    pageActions->addWidget(remove_);
    pageActions->addStretch();
    root->addLayout(pageActions);

    auto* actions = new QHBoxLayout;
    actions->addWidget(new QLabel(QStringLiteral("Название:")));
    name_ = new QLineEdit;
    name_->setObjectName(QStringLiteral("ui.hmi.pageName"));
    name_->setAccessibleName(QStringLiteral("Название текущего HMI-представления"));
    actions->addWidget(name_, 1);
    rename_ = new QPushButton(QStringLiteral("Переименовать"));
    rename_->setObjectName(QStringLiteral("ui.hmi.renamePage"));
    actions->addWidget(rename_);
    root->addLayout(actions);

    auto* fileActions = new QHBoxLayout;
    load_ = new QPushButton(QStringLiteral("Открыть комплект"));
    load_->setObjectName(QStringLiteral("ui.hmi.loadCollection"));
    fileActions->addWidget(load_);
    save_ = new QPushButton(QStringLiteral("Сохранить комплект"));
    save_->setObjectName(QStringLiteral("ui.hmi.saveCollection"));
    fileActions->addWidget(save_);
    fileActions->addStretch();
    root->addLayout(fileActions);

    stack_ = new QStackedWidget;
    stack_->setObjectName(QStringLiteral("ui.hmi.pageStack"));
    root->addWidget(stack_, 1);
    note_ = new QLabel(QStringLiteral("Только проект интерфейса · физическая станция не подключена"));
    note_->setProperty("role", "warning");
    note_->setWordWrap(true);
    root->addWidget(note_);

    connect(pages_, &HmiDeliveryCollection::pagesChanged,
            this, &HmiDeliveryEditor::rebuild);
    connect(pages_, &HmiDeliveryCollection::currentPageChanged,
            this, &HmiDeliveryEditor::updateSelection);
    connect(pageSelector_, qOverload<int>(&QComboBox::currentIndexChanged),
            pages_, &HmiDeliveryCollection::setCurrentIndex);
    connect(add_, &QPushButton::clicked, this, [this] {
        if (pages_->addPage(QStringLiteral("Новый экран %1").arg(pages_->pageCount() + 1)).isEmpty())
            notification(QStringLiteral("Лимит 12 экранов; добавление не удалось"));
    });
    connect(duplicate_, &QPushButton::clicked, this, [this] {
        if (pages_->duplicatePage(pages_->currentIndex()).isEmpty())
            notification(QStringLiteral("Не удалось продублировать экран"));
    });
    connect(rename_, &QPushButton::clicked, this, [this] {
        if (!pages_->renamePage(pages_->currentIndex(), name_->text()))
            notification(QStringLiteral("Название должно содержать от 1 до 100 символов"));
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (!pages_->deletePage(pages_->currentIndex()))
            notification(QStringLiteral("Последний HMI-экран удалить нельзя"));
    });
    connect(save_, &QPushButton::clicked, this, [this] {
        const QString filename = QFileDialog::getSaveFileName(
            this, QStringLiteral("Сохранить комплект HMI"),
            pages_->deliveryId() + QStringLiteral("_hmi_collection.json"),
            QStringLiteral("HMI JSON (*.json)"));
        if (filename.isEmpty()) return;
        QSaveFile file(filename);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(pages_->toJson()) < 0 || !file.commit()) {
            notification(QStringLiteral("Ошибка сохранения комплекта"));
            return;
        }
        notification(QStringLiteral("Комплект HMI сохранён"));
    });
    connect(load_, &QPushButton::clicked, this, [this] {
        const QString filename = QFileDialog::getOpenFileName(
            this, QStringLiteral("Открыть комплект HMI"), {},
            QStringLiteral("HMI JSON (*.json)"));
        if (filename.isEmpty()) return;
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) {
            notification(QStringLiteral("Ошибка чтения или размер файла превышен"));
            return;
        }
        QString error;
        if (!pages_->loadFromJson(file.readAll(), &error)) {
            notification(QStringLiteral("Импорт отклонён: %1").arg(error));
            return;
        }
        notification(QStringLiteral("Набор HMI загружен"));
    });
    rebuild();
}

HmiDesignerPage* HmiDeliveryEditor::currentDesigner() const
{
    auto* scroll = qobject_cast<QScrollArea*>(stack_->currentWidget());
    return scroll ? qobject_cast<HmiDesignerPage*>(scroll->widget()) : nullptr;
}

void HmiDeliveryEditor::notification(const QString& message)
{
    note_->setText(message);
}

void HmiDeliveryEditor::rebuild()
{
    while (stack_->count() > 0) {
        QWidget* page = stack_->widget(0);
        stack_->removeWidget(page);
        delete page;
    }
    {
        QSignalBlocker blocker(pageSelector_);
        pageSelector_->clear();
        for (int i = 0; i < pages_->pageCount(); ++i) {
            const auto record = pages_->pageAt(i);
            pageSelector_->addItem(record.title, record.id);
            auto* scroll = new QScrollArea;
            scroll->setObjectName(QStringLiteral("ui.hmi.designerScroll"));
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            auto* designer = new HmiDesignerPage(record.document);
            designer->setMinimumWidth(920);
            scroll->setWidget(designer);
            stack_->addWidget(scroll);
        }
    }
    updateSelection();
}

void HmiDeliveryEditor::updateSelection()
{
    const int index = pages_->currentIndex();
    {
        QSignalBlocker blocker(pageSelector_);
        pageSelector_->setCurrentIndex(index);
    }
    if (index >= 0 && index < stack_->count()) stack_->setCurrentIndex(index);
    name_->setText(pages_->pageAt(index).title);
    remove_->setEnabled(pages_->pageCount() > 1);
    duplicate_->setEnabled(pages_->pageCount() < 12);
    add_->setEnabled(pages_->pageCount() < 12);
}

HmiDeliveryViewer::HmiDeliveryViewer(HmiDeliveryCollection* pages, QWidget* parent)
    : QFrame(parent), pages_(pages)
{
    setObjectName(QStringLiteral("card"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);
    auto* top = new QHBoxLayout;
    auto* heading = new QLabel(QStringLiteral("HMI-представление"));
    heading->setProperty("role", "section");
    top->addWidget(heading);
    top->addStretch();
    selector_ = new QComboBox;
    selector_->setObjectName(QStringLiteral("ui.hmi.viewerSelector"));
    selector_->setAccessibleName(QStringLiteral("Просматриваемое HMI-представление"));
    top->addWidget(selector_);
    root->addLayout(top);
    auto* warning = new QLabel(QStringLiteral(
        "UI-ПРЕДСТАВЛЕНИЕ · РЕАЛЬНЫЕ ИЗМЕРЕНИЯ И КОМАНДЫ НЕ ПОДКЛЮЧЕНЫ"));
    warning->setProperty("role", "warning");
    warning->setWordWrap(true);
    root->addWidget(warning);
    views_ = new QStackedWidget;
    views_->setObjectName(QStringLiteral("ui.hmi.viewerStack"));
    root->addWidget(views_, 1);
    connect(pages_, &HmiDeliveryCollection::pagesChanged,
            this, &HmiDeliveryViewer::rebuild);
    connect(pages_, &HmiDeliveryCollection::currentPageChanged,
            this, &HmiDeliveryViewer::updateSelection);
    connect(selector_, qOverload<int>(&QComboBox::currentIndexChanged),
            pages_, &HmiDeliveryCollection::setCurrentIndex);
    rebuild();
}

int HmiDeliveryViewer::visiblePageIndex() const
{
    return views_->currentIndex();
}

HmiReadOnlyPanel* HmiDeliveryViewer::currentPanel() const
{
    return qobject_cast<HmiReadOnlyPanel*>(views_->currentWidget());
}

void HmiDeliveryViewer::rebuild()
{
    while (views_->count() > 0) {
        QWidget* item = views_->widget(0);
        views_->removeWidget(item);
        delete item;
    }
    {
        QSignalBlocker blocker(selector_);
        selector_->clear();
        for (int index = 0; index < pages_->pageCount(); ++index) {
            const auto page = pages_->pageAt(index);
            selector_->addItem(page.title, page.id);
            views_->addWidget(new HmiReadOnlyPanel(page.document));
        }
    }
    updateSelection();
}

void HmiDeliveryViewer::updateSelection()
{
    const int index = pages_->currentIndex();
    {
        QSignalBlocker blocker(selector_);
        selector_->setCurrentIndex(index);
    }
    if (index >= 0 && index < views_->count()) views_->setCurrentIndex(index);
}

} // namespace miltech::ui
