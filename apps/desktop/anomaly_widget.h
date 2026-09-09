#pragma once

#include <QWidget>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <vector>
#include "orbita.h"
#include "metadata_service.h"

class ToleranceResolver;

class AnomalyWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AnomalyWidget(QWidget *parent = nullptr);
    void setMetadataService(MetadataService* db);
    void setToleranceResolver(ToleranceResolver* r);
    void setChannels(const std::vector<orbita::ChannelSpec>& specs);
    void updateValues(const QMap<QString, double>& values);
    void setSelectedChannel(int index);

signals:
    void channelSelected(int index);

private:
    void updateAnomalies();

    MetadataService* m_db = nullptr;
    ToleranceResolver* m_resolver = nullptr;
    std::vector<orbita::ChannelSpec> m_specs;
    QMap<QString, double> m_values;
    int m_selectedIndex = -1;

    QLabel* m_normLabel;
    QLabel* m_anomLabel;
    QListWidget* m_anomalyList;
};
