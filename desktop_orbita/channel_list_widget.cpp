#include "channel_list_widget.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QTreeWidgetItem>

ChannelListWidget::ChannelListWidget(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget;
    m_tree->setHeaderLabels({"№", "Канал", "Значение"});
    m_tree->setIndentation(12);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, &ChannelListWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &ChannelListWidget::onItemDoubleClicked);
}

void ChannelListWidget::setMetadataService(MetadataService* db)
{
    m_db = db;
    rebuildTree();
}

void ChannelListWidget::setChannels(const std::vector<orbita::ChannelSpec>& specs)
{
    m_specs = specs;
    rebuildTree();
    updateItems();
}

void ChannelListWidget::updateValues(const QMap<QString, double>& values)
{
    m_values = values;
    updateItems();
}

void ChannelListWidget::setSelectedChannel(int index)
{
    m_selectedIndex = index;
    // Снимаем выделение со всех
    for (auto it = m_itemMap.begin(); it != m_itemMap.end(); ++it)
        it.value()->setSelected(false);
    if (m_itemMap.contains(index))
        m_itemMap[index]->setSelected(true);
}

void ChannelListWidget::clear()
{
    m_tree->clear();
    m_itemMap.clear();
    m_selectedIndex = -1;
}

void ChannelListWidget::rebuildTree()
{
    m_tree->clear();
    m_itemMap.clear();

    if (!m_db || m_specs.empty())
        return;

    // Группируем каналы по категориям
    QMap<QString, QList<int>> catMap;
    for (int i = 0; i < (int)m_specs.size(); ++i) {
        QString cat = m_db->getCategory(QString::fromStdString(m_specs[i].address));
        if (cat.isEmpty()) cat = "Прочие";
        catMap[cat].append(i);
    }

    for (auto it = catMap.begin(); it != catMap.end(); ++it) {
        QTreeWidgetItem* catItem = new QTreeWidgetItem(m_tree);
        catItem->setText(0, it.key());
        catItem->setFirstColumnSpanned(true);
        catItem->setFlags(catItem->flags() & ~Qt::ItemIsSelectable);
        QFont f = catItem->font(0);
        f.setBold(true);
        catItem->setFont(0, f);

        for (int idx : it.value()) {
            const auto& spec = m_specs[idx];
            QTreeWidgetItem* item = new QTreeWidgetItem(catItem);
            item->setText(0, QString::number(idx + 1));
            item->setText(1, QString::fromStdString(spec.name.empty() ? spec.address : spec.name));
            item->setText(2, "---");
            item->setData(0, Qt::UserRole, idx);
            m_itemMap[idx] = item;
        }
        catItem->setExpanded(true);
    }
}

void ChannelListWidget::updateItems()
{
    for (auto it = m_itemMap.begin(); it != m_itemMap.end(); ++it) {
        int idx = it.key();
        double val = m_values.value(QString::fromStdString(m_specs[idx].address), 0.0);
        it.value()->setText(2, QString::number(val, 'f', 0));
    }
    // Обновить выделение
    setSelectedChannel(m_selectedIndex);
}

void ChannelListWidget::onItemClicked(QTreeWidgetItem* item, int column)
{
    if (!item->parent()) return; // категория
    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < (int)m_specs.size()) {
        emit channelSelected(idx);
    }
}

void ChannelListWidget::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    if (!item->parent()) return;
    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < (int)m_specs.size()) {
        emit channelDoubleClicked(idx);
    }
}
