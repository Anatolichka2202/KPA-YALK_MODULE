#include "address_sets_widget.h"

#include "watch_set_widget.h"

#include <QHBoxLayout>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <utility>

AddressSetsWidget::AddressSetsWidget(MetadataService* db, QWidget* parent)
    : QWidget(parent)
    , db_(db)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* caption = new QLabel(
        QStringLiteral("Параллельные наборы параметров. Все включённые параметры "
                       "поступают из одного live-потока; вкладки не запускают "
                       "отдельные декодеры. В файл сохраняются только включённые параметры."), this);
    caption->setWordWrap(true);
    caption->setObjectName(QStringLiteral("addressSetsExplanation"));
    caption->setStyleSheet(QStringLiteral("color:#b6c8d8; padding:4px;"));
    layout->addWidget(caption);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("parallelAddressSets"));
    tabs_->setDocumentMode(true);
    layout->addWidget(tabs_, 1);

    auto* actions = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("Добавить набор"), this);
    auto* rename = new QPushButton(QStringLiteral("Переименовать"), this);
    auto* remove = new QPushButton(QStringLiteral("Убрать набор"), this);
    auto* save = new QPushButton(QStringLiteral("Сохранить наборы…"), this);
    auto* load = new QPushButton(QStringLiteral("Загрузить наборы…"), this);
    add->setObjectName(QStringLiteral("addAddressSet"));
    rename->setObjectName(QStringLiteral("renameAddressSet"));
    remove->setObjectName(QStringLiteral("removeAddressSet"));
    add->setAccessibleDescription(QStringLiteral("Создать ещё один независимый набор параметров"));
    actions->addWidget(add);
    actions->addWidget(rename);
    actions->addWidget(remove);
    actions->addStretch(1);
    actions->addWidget(load);
    actions->addWidget(save);
    layout->addLayout(actions);

    createEditorForSet(0);
    tabs_->setCurrentIndex(0);

    connect(add, &QPushButton::clicked, this, &AddressSetsWidget::addAddressSet);
    connect(rename, &QPushButton::clicked, this, &AddressSetsWidget::renameAddressSet);
    connect(remove, &QPushButton::clicked, this, &AddressSetsWidget::removeAddressSet);
    connect(save, &QPushButton::clicked, this, &AddressSetsWidget::saveView);
    connect(load, &QPushButton::clicked, this, &AddressSetsWidget::loadView);
    connect(tabs_, &QTabWidget::currentChanged, this, &AddressSetsWidget::setCurrentAddressSet);
}

void AddressSetsWidget::setFromSpecs(const std::vector<orbita::ChannelSpec>& specs)
{
    auto* editor = qobject_cast<WatchSetWidget*>(tabs_->currentWidget());
    if (editor)
        editor->setFromSpecs(specs);
}

void AddressSetsWidget::addParams(const std::vector<orbita::ChannelSpec>& specs)
{
    auto* editor = qobject_cast<WatchSetWidget*>(tabs_->currentWidget());
    if (editor)
        editor->addParams(specs);
}

int AddressSetsWidget::setCount() const noexcept
{
    return model_.count();
}

std::vector<orbita::ChannelSpec> AddressSetsWidget::combinedSpecs() const
{
    return model_.combinedSpecs();
}

std::vector<AddressSetDefinition> AddressSetsWidget::definitions() const
{
    std::vector<AddressSetDefinition> result;
    for (int index = 0; index < model_.count(); ++index)
        result.push_back(model_.at(index));
    return result;
}

void AddressSetsWidget::addAddressSet()
{
    const int index = model_.addSet();
    createEditorForSet(index);
    tabs_->setCurrentIndex(index);
    emitCombinedChanged();
}

void AddressSetsWidget::removeAddressSet()
{
    const int index = tabs_->currentIndex();
    if (!model_.removeSet(index)) {
        QMessageBox::information(this, QStringLiteral("Наборы параметров"),
            QStringLiteral("Должен остаться хотя бы один набор параметров."));
        return;
    }

    QWidget* editor = tabs_->widget(index);
    tabs_->removeTab(index);
    delete editor;
    tabs_->setCurrentIndex(model_.activeIndex());
    emitCombinedChanged();
}

void AddressSetsWidget::renameAddressSet()
{
    const int index = tabs_->currentIndex();
    if (index < 0)
        return;
    bool accepted = false;
    const QString title = QInputDialog::getText(this, QStringLiteral("Имя набора"),
        QStringLiteral("Имя:"), QLineEdit::Normal, model_.at(index).title, &accepted);
    if (!accepted || title.trimmed().isEmpty())
        return;
    model_.renameSet(index, title);
    refreshTabTitle(index);
    emit addressSetsChanged(definitions());
}

void AddressSetsWidget::saveView()
{
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить наборы параметров"),
        QString(), QStringLiteral("Наборы параметров (*.json)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    const auto data = model_.serialize();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        QMessageBox::critical(this, QStringLiteral("Сохранение"),
            QStringLiteral("Не удалось сохранить наборы: %1").arg(file.errorString()));
    }
}

void AddressSetsWidget::loadView()
{
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Загрузить наборы параметров"),
        QString(), QStringLiteral("Наборы параметров (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, QStringLiteral("Загрузка"),
            QStringLiteral("Не удалось открыть файл: %1").arg(file.errorString()));
        return;
    }
    AddressSetModel imported;
    QString error;
    if (!imported.restore(file.readAll(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Загрузка"), error);
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Загрузка наборов"),
        QStringLiteral("Заменить текущие наборы параметров загруженными?")) != QMessageBox::Yes)
        return;

    // На время перестроения не отправляем частично восстановленную подписку
    // декодеру: он получит единый список после загрузки всех вкладок.
    const QSignalBlocker block(tabs_);
    while (tabs_->count()) {
        auto* page = tabs_->widget(0);
        tabs_->removeTab(0);
        delete page;
    }
    model_ = std::move(imported);
    for (int i = 0; i < model_.count(); ++i) {
        createEditorForSet(i);
        auto* editor = qobject_cast<WatchSetWidget*>(tabs_->widget(i));
        const QSignalBlocker editorBlock(editor);
        editor->setFromSpecs(model_.at(i).specs);
        refreshTabTitle(i);
    }
    tabs_->setCurrentIndex(model_.activeIndex());
    emitCombinedChanged();
}

void AddressSetsWidget::setCurrentAddressSet(int index)
{
    if (index < 0 || index >= model_.count())
        return;
    model_.setActiveIndex(index);
}

void AddressSetsWidget::onCurrentSetChanged(const std::vector<orbita::ChannelSpec>& specs)
{
    auto* editor = qobject_cast<WatchSetWidget*>(sender());
    const int index = tabs_->indexOf(editor);
    if (index < 0)
        return;

    model_.setActiveIndex(index);
    model_.replaceActive(specs);
    refreshTabTitle(index);
    emitCombinedChanged();
}

void AddressSetsWidget::createEditorForSet(int index)
{
    auto* editor = new WatchSetWidget(db_, tabs_);
    editor->setObjectName(QStringLiteral("addressSetEditor_%1").arg(index + 1));
    tabs_->insertTab(index, editor, model_.at(index).title);
    connect(editor, &WatchSetWidget::watchSetChanged,
            this, &AddressSetsWidget::onCurrentSetChanged);
    connect(editor, &WatchSetWidget::configSaved,
            this, &AddressSetsWidget::configSaved);
}

void AddressSetsWidget::refreshTabTitle(int index)
{
    const auto& set = model_.at(index);
    tabs_->setTabText(index, QStringLiteral("%1 · %2")
        .arg(set.title).arg(static_cast<int>(set.specs.size())));
}

void AddressSetsWidget::emitCombinedChanged()
{
    emit combinedWatchSetChanged(model_.combinedSpecs());
    emit addressSetsChanged(definitions());
}
