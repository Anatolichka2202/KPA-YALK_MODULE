#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QMap>
#include <vector>
#include "orbita.h"
#include "metadata_service.h"

class TableWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TableWidget(QWidget *parent = nullptr);
    void setMetadataService(MetadataService* db);
    void setChannels(const std::vector<orbita::ChannelSpec>& specs);
    void updateValues(const QMap<QString, double>& values);
    void setSelectedChannel(int index);

signals:
    void channelSelected(int index);
    void channelDoubleClicked(int index);

private:
    void updateTable();

    QTableWidget* m_table;
    MetadataService* m_db = nullptr;
    std::vector<orbita::ChannelSpec> m_specs;
    QMap<QString, double> m_values;
    int m_selectedIndex = -1;
};
