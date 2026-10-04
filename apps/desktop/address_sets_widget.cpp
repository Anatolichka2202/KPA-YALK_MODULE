#include "address_sets_widget.h"

#include "watch_set_widget.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

AddressSetsWidget::AddressSetsWidget(MetadataService* db, QWidget* parent)
    : QWidget(parent)
    , db_(db)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* caption = new QLabel(
        QStringLiteral("Параллельные наборы адресов. Все включённые адреса "
                       "поступают из одного live-потока; вкладки не запускают "
                       "отдельные декодеры."), this);
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
    add->setObjectName(QStringLiteral("addAddressSet"));
    rename->setObjectName(QStringLiteral("renameAddressSet"));
    remove->setObjectName(QStringLiteral("removeAddressSet"));
    add->setAccessibleDescription(QStringLiteral("Создать ещё один независимый набор адресов"));
    actions->addWidget(add);
    actions->addWidget(rename);
    actions->addWidget(remove);
    actions->addStretch(1);
    layout->addLayout(actions);

    createEditorForSet(0);
    tabs_->setCurrentIndex(0);

    connect(add, &QPushButton::clicked, this, &AddressSetsWidget::addAddressSet);
    connect(rename, &QPushButton::clicked, this, &AddressSetsWidget::renameAddressSet);
    connect(remove, &QPushButton::clicked, this, &AddressSetsWidget::removeAddressSet);
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
        QMessageBox::information(this, QStringLiteral("Наборы адресов"),
            QStringLiteral("Должен остаться хотя бы один набор адресов."));
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
