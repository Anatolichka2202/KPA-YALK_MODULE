#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <qcustomplot.h>
#include <deque>
#include <QTimer>
#include "orbita.h"
#include "metadata_service.h"

class DetailView : public QWidget
{
    Q_OBJECT
public:
    explicit DetailView(QWidget *parent = nullptr);
    ~DetailView();

    void setMetadataService(MetadataService* db);
    void setChannel(const orbita::ChannelSpec& spec);
    void updateValue(double value);
    void setHistorySize(int size = 60);
    void setSelected(bool selected = true); // для выделения
    const orbita::ChannelSpec& currentSpec() const { return m_spec; }

signals:
    void backToMain();
    void navigateChannel(int delta); // +1 или -1

private slots:
    void onPrevClicked();
    void onNextClicked();
    void onBackClicked();

private:
    void setupUI();
    void setupPlot();
    void updateUI();
    void updatePlot();

    orbita::ChannelSpec m_spec;
    MetadataService* m_db = nullptr;
    std::deque<double> m_history;
    int m_historySize = 60;
    double m_currentValue = 0.0;
    bool m_isValid = false;

    // Допуски (заглушка, позже можно брать из БД)
    int m_low = 200;
    int m_high = 800;
    int m_nominal = 500;

    // UI элементы
    QLabel* m_numLabel;
    QLabel* m_nameLabel;
    QLabel* m_catLabel;
    QLabel* m_valueLabel;
    QLabel* m_statusLabel;
    QProgressBar* m_toleranceBar;
    QLabel* m_rangeLabel;
    QLabel* m_statsLabel;
    QCustomPlot* m_plot;
    QCPGraph* m_graph;
    QCPGraph* m_lowLine;
    QCPGraph* m_highLine;
    QCPGraph* m_nomLine;

    QPushButton* m_backBtn;
    QPushButton* m_prevBtn;
    QPushButton* m_nextBtn;

    QTimer* m_updateTimer; // для плавного обновления графика
};
