#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include "orbita.h"
#include "metadata_service.h"

class ReadoutWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ReadoutWidget(QWidget *parent = nullptr);

    void setMetadataService(MetadataService* db);
    void setChannel(const orbita::ChannelSpec& spec);
    void updateValue(double value);

private:
    void updateUI();

    MetadataService* m_db = nullptr;
    orbita::ChannelSpec m_spec;
    double m_currentValue = 0.0;

    QLabel* m_numLabel;
    QLabel* m_nameLabel;
    QLabel* m_catLabel;
    QLabel* m_valueLabel;
    QLabel* m_statusLabel;
    QProgressBar* m_toleranceBar;
    QLabel* m_rangeLabel;

    // Допуски (заглушка, позже из БД)
    int m_low = 200;
    int m_high = 800;
    int m_nominal = 500;
};
