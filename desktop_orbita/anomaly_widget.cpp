#include "anomaly_widget.h"
#include "channel_status.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>

AnomalyWidget::AnomalyWidget(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    // Сводка
    QHBoxLayout* summary = new QHBoxLayout;
    m_normLabel = new QLabel("0");
    m_normLabel->setStyleSheet("font-size: 20px; font-weight: 600; color: #7fc79a;");
    QLabel* normText = new QLabel("в норме");
    normText->setStyleSheet("color: #5e7e6a; font-size: 10.5px;");
    QWidget* normWidget = new QWidget;
    QVBoxLayout* normLayout = new QVBoxLayout(normWidget);
    normLayout->addWidget(m_normLabel);
    normLayout->addWidget(normText);

    m_anomLabel = new QLabel("0");
    m_anomLabel->setStyleSheet("font-size: 20px; font-weight: 600; color: #e6b878;");
    QLabel* anomText = new QLabel("вне нормы");
    anomText->setStyleSheet("color: #8a755a; font-size: 10.5px;");
    QWidget* anomWidget = new QWidget;
    QVBoxLayout* anomLayout = new QVBoxLayout(anomWidget);
    anomLayout->addWidget(m_anomLabel);
    anomLayout->addWidget(anomText);

    summary->addWidget(normWidget);
    summary->addWidget(anomWidget);
    summary->addStretch();
    layout->addLayout(summary);

    // Список аномалий
    m_anomalyList = new QListWidget;
    m_anomalyList->setStyleSheet("QListWidget { background: transparent; border: none; } QListWidget::item { padding: 6px 8px; }");
    layout->addWidget(m_anomalyList);

    connect(m_anomalyList, &QListWidget::itemClicked, [this](QListWidgetItem* item) {
        int idx = item->data(Qt::UserRole).toInt();
        emit channelSelected(idx);
    });
}

void AnomalyWidget::setMetadataService(MetadataService* db)
{
    m_db = db;
}

void AnomalyWidget::setChannels(const std::vector<orbita::ChannelSpec>& specs)
{
    m_specs = specs;
    updateAnomalies();
}

void AnomalyWidget::updateValues(const QMap<QString, double>& values)
{
    m_values = values;
    updateAnomalies();
}

void AnomalyWidget::setSelectedChannel(int index)
{
    m_selectedIndex = index;
    // Можно подсветить в списке
}

void AnomalyWidget::updateAnomalies()
{
    int norm = 0, anom = 0;
    m_anomalyList->clear();

    for (int i = 0; i < m_specs.size(); ++i) {
        const auto& spec = m_specs[i];
        double val = m_values.value(QString::fromStdString(spec.address), 0.0);
        chstatus::Tolerance tol = chstatus::forAddress(m_db, spec.address);
        chstatus::Level lvl = chstatus::evaluate(val, tol);
        if (chstatus::isAnomaly(lvl)) {
            anom++;
            QString name = QString::fromStdString(spec.name.empty() ? spec.address : spec.name);
            QListWidgetItem* item = new QListWidgetItem(QString("КАН %1  %2  %3").arg(i+1).arg(name).arg(val, 0, 'f', 0));
            item->setData(Qt::UserRole, i);
            item->setForeground(chstatus::textColor(lvl));
            m_anomalyList->addItem(item);
        } else {
            norm++;
        }
    }
    m_normLabel->setText(QString::number(norm));
    m_anomLabel->setText(QString::number(anom));
}
