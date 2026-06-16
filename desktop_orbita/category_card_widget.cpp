#include "category_card_widget.h"
#include "tolerance_resolver.h"
#include "channel_status.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

CategoryCardWidget::CategoryCardWidget(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(6);

    QHBoxLayout* header = new QHBoxLayout;
    m_nameLabel = new QLabel;
    m_nameLabel->setStyleSheet("font-weight: 600; font-size: 14px;");
    m_countLabel = new QLabel;
    m_countLabel->setStyleSheet("color: #7e8a98; font-size: 11px;");
    header->addWidget(m_nameLabel);
    header->addStretch();
    header->addWidget(m_countLabel);
    layout->addLayout(header);

    m_barChart = new BarChartWidget;
    m_barChart->setMinimumHeight(80);
    layout->addWidget(m_barChart);

    QHBoxLayout* footer = new QHBoxLayout;
    m_avgLabel = new QLabel;
    m_avgLabel->setStyleSheet("color: #aeb8c4; font-size: 14px; font-weight: 500;");
    m_anomalyLabel = new QLabel;
    m_anomalyLabel->setStyleSheet("font-size: 11px;");
    footer->addWidget(m_avgLabel);
    footer->addStretch();
    footer->addWidget(m_anomalyLabel);
    layout->addLayout(footer);

    setStyleSheet("background: #12151a; border: 1px solid #232a33; border-radius: 9px; padding: 12px;");
    connect(m_barChart, &BarChartWidget::barClicked, this, &CategoryCardWidget::channelSelected);
    connect(m_barChart, &BarChartWidget::barDoubleClicked, this, &CategoryCardWidget::channelDoubleClicked);
}

void CategoryCardWidget::setCategory(const QString& name, const std::vector<int>& indices, const std::vector<orbita::ChannelSpec>& allSpecs)
{
    m_nameLabel->setText(name);
    m_indices = indices;
    m_specs = allSpecs;
    m_countLabel->setText(QString::number(indices.size()) + " кан");

    // Передаём каналы в BarChartWidget
    std::vector<orbita::ChannelSpec> subSpecs;
    for (int idx : indices) subSpecs.push_back(allSpecs[idx]);
    m_barChart->setChannels(subSpecs);
    updateValues(m_values);
}

void CategoryCardWidget::setMetadataService(MetadataService* db)
{
    m_db = db;
    m_barChart->setMetadataService(db);
}

void CategoryCardWidget::setToleranceResolver(ToleranceResolver* r)
{
    m_resolver = r;
    m_barChart->setToleranceResolver(r);
}

void CategoryCardWidget::updateValues(const QMap<QString, double>& values)
{
    m_values = values;
    m_barChart->updateValues(values);
    // Вычислить среднее
    double sum = 0;
    int count = 0;
    for (int idx : m_indices) {
        const auto& spec = m_specs[idx];
        double v = values.value(QString::fromStdString(spec.address), 0.0);
        sum += v;
        count++;
    }
    double avg = count ? sum / count : 0;
    m_avgLabel->setText(QString("средний %1").arg(avg, 0, 'f', 0));

    // Подсчёт аномалий (нет допуска → не аномалия)
    int anom = 0;
    for (int idx : m_indices) {
        const auto& spec = m_specs[idx];
        double v = values.value(QString::fromStdString(spec.address), 0.0);
        chstatus::Tolerance tol = (m_resolver ? m_resolver->resolve(QString::fromStdString(spec.address)) : chstatus::forAddress(m_db, spec.address));
        if (chstatus::isAnomaly(chstatus::evaluate(v, tol))) anom++;
    }
    m_anomalyLabel->setText(anom ? QString("%1 вне нормы").arg(anom) : "все в норме");
    m_anomalyLabel->setStyleSheet(anom ? "color: #e6b878;" : "color: #7fc79a;");
}

void CategoryCardWidget::setSelectedChannel(int index)
{
    m_barChart->setSelectedBar(index);
}
