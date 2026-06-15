#include "parameter_browser.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDebug>

ParameterBrowser::ParameterBrowser(ParameterDatabase* db, QWidget *parent)
    : QWidget(parent), db_(db)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({"Параметр", "Адрес", "Категория"});
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(tree_);

    addButton_ = new QPushButton("Добавить выбранные в конфигурацию");
    layout->addWidget(addButton_);

    connect(addButton_, &QPushButton::clicked, this, &ParameterBrowser::onAddSelected);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, &ParameterBrowser::onItemDoubleClicked);

    rebuildTree();
}

void ParameterBrowser::rebuildTree() {
    if (!db_) return;
    tree_->clear();
    QStringList categories = db_->getAllCategories();
    for (const QString& cat : categories) {
        QTreeWidgetItem* catItem = new QTreeWidgetItem(tree_);
        catItem->setText(0, cat);
        catItem->setFlags(catItem->flags() & ~Qt::ItemIsSelectable);
        QHash<QString, QString> params = db_->getParametersByCategory(cat);
        for (auto it = params.begin(); it != params.end(); ++it) {
            QTreeWidgetItem* paramItem = new QTreeWidgetItem(catItem);
            paramItem->setText(0, it.value());              // имя параметра
            paramItem->setText(1, it.key());                // нормализованный адрес
            paramItem->setText(2, cat);
            paramItem->setData(0, Qt::UserRole, it.key());  // сохраняем адрес
        }
        catItem->setExpanded(true);
    }
    tree_->resizeColumnToContents(0);
    tree_->resizeColumnToContents(1);
}

void ParameterBrowser::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    if (!item->parent()) return; // не категория
    QString addr = item->data(0, Qt::UserRole).toString();
    QString name = item->text(0);
    if (!addr.isEmpty())
        emit parametersSelected({addr}, {name});
}

void ParameterBrowser::onAddSelected() {
    QList<QString> addresses, names;
    QList<QTreeWidgetItem*> selected = tree_->selectedItems();
    for (QTreeWidgetItem* item : selected) {
        if (item->parent()) {
            addresses << item->data(0, Qt::UserRole).toString();
            names << item->text(0);
        }
    }
    if (!addresses.isEmpty())
        emit parametersSelected(addresses, names);
}
