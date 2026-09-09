#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QMap>
#include <vector>
#include "orbita.h"
#include "metadata_service.h"

class ChannelListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChannelListWidget(QWidget *parent = nullptr);

    void setMetadataService(MetadataService* db);
    void setChannels(const std::vector<orbita::ChannelSpec>& specs);
    void updateValues(const QMap<QString, double>& values);
    void setSelectedChannel(int index);
    void clear();

signals:
    void channelSelected(int index);
    void channelDoubleClicked(int index);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
    void rebuildTree();
    void updateItems();

    QTreeWidget* m_tree;
    MetadataService* m_db = nullptr;
    std::vector<orbita::ChannelSpec> m_specs;
    QMap<QString, double> m_values;
    int m_selectedIndex = -1;

    // Для быстрого доступа к элементам по индексу канала
    QMap<int, QTreeWidgetItem*> m_itemMap;
};
