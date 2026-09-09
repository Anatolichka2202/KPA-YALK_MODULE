#pragma once

#include <QWidget>
#include <QLabel>
#include "bar_chart_widget.h"
#include "metadata_service.h"

class ToleranceResolver;

class CategoryCardWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CategoryCardWidget(QWidget *parent = nullptr);
    void setCategory(const QString& name, const std::vector<int>& indices, const std::vector<orbita::ChannelSpec>& allSpecs);
    void setMetadataService(MetadataService* db);
    void setToleranceResolver(ToleranceResolver* r);
    void updateValues(const QMap<QString, double>& values);
    void setSelectedChannel(int index);

signals:
    void channelSelected(int index);
    void channelDoubleClicked(int index);

private:
    QLabel* m_nameLabel;
    QLabel* m_countLabel;
    BarChartWidget* m_barChart;
    QLabel* m_avgLabel;
    QLabel* m_anomalyLabel;

    std::vector<int> m_indices;
    std::vector<orbita::ChannelSpec> m_specs; // все спецификации
    QMap<QString, double> m_values;
    MetadataService* m_db = nullptr;
    ToleranceResolver* m_resolver = nullptr;
};
