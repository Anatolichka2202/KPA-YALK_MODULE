#include "detail_view.h"
#include "tolerance_resolver.h"
#include "plot_theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFontDatabase>
#include <QDebug>

DetailView::DetailView(QWidget *parent)
    : QWidget(parent)
    , m_plot(nullptr)
    , m_graph(nullptr)
    , m_lowLine(nullptr)
    , m_highLine(nullptr)
    , m_nomLine(nullptr)
    , m_updateTimer(new QTimer(this))
{
    setupUI();
    setupPlot();
    m_history.assign(m_historySize, 0.0);

    connect(m_backBtn, &QPushButton::clicked, this, &DetailView::onBackClicked);
    connect(m_prevBtn, &QPushButton::clicked, this, &DetailView::onPrevClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &DetailView::onNextClicked);

    connect(m_updateTimer, &QTimer::timeout, this, &DetailView::updatePlot);
    m_updateTimer->start(100); // обновление графика 10 Гц
}

DetailView::~DetailView()
{
    if (m_updateTimer)
        m_updateTimer->stop();
}

void DetailView::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // Верхняя панель: назад, номер, имя
    QHBoxLayout* topLayout = new QHBoxLayout;
    m_backBtn = new QPushButton("← Назад");
    m_backBtn->setFixedWidth(80);
    topLayout->addWidget(m_backBtn);

    topLayout->addStretch();

    m_numLabel = new QLabel("КАН --");
    m_numLabel->setStyleSheet("font-family: 'IBM Plex Mono'; color: #5b6573; font-size: 12px;");
    topLayout->addWidget(m_numLabel);

    m_nameLabel = new QLabel("Нет канала");
    m_nameLabel->setStyleSheet("font-weight: 600; font-size: 16px;");
    topLayout->addWidget(m_nameLabel);

    topLayout->addStretch();
    mainLayout->addLayout(topLayout);

    // Категория
    m_catLabel = new QLabel;
    m_catLabel->setStyleSheet("color: #7e8a98; font-size: 12px;");
    mainLayout->addWidget(m_catLabel);

    // Основной блок: значение + статус + допуск
    QHBoxLayout* valueLayout = new QHBoxLayout;
    QVBoxLayout* leftValueLayout = new QVBoxLayout;
    m_valueLabel = new QLabel("---");
    m_valueLabel->setStyleSheet("font-family: 'IBM Plex Mono'; font-size: 48px; font-weight: 600;");
    leftValueLayout->addWidget(m_valueLabel);

    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet("padding: 4px 10px; border-radius: 6px;");
    leftValueLayout->addWidget(m_statusLabel);

    valueLayout->addLayout(leftValueLayout);
    valueLayout->addStretch();

    // Статистика справа
    QVBoxLayout* statsLayout = new QVBoxLayout;
    m_statsLabel = new QLabel("мин: --  сред: --  макс: --  размах: --");
    m_statsLabel->setStyleSheet("color: #aeb8c4; font-family: 'IBM Plex Mono'; font-size: 12px;");
    statsLayout->addWidget(m_statsLabel);

    // Кнопки навигации
    QHBoxLayout* navLayout = new QHBoxLayout;
    m_prevBtn = new QPushButton("◀");
    m_nextBtn = new QPushButton("▶");
    m_prevBtn->setFixedWidth(40);
    m_nextBtn->setFixedWidth(40);
    navLayout->addWidget(m_prevBtn);
    navLayout->addWidget(m_nextBtn);
    navLayout->addStretch();
    statsLayout->addLayout(navLayout);

    valueLayout->addLayout(statsLayout);
    mainLayout->addLayout(valueLayout);

    // Шкала допуска
    m_toleranceBar = new QProgressBar;
    m_toleranceBar->setRange(0, 1023);
    m_toleranceBar->setTextVisible(false);
    m_toleranceBar->setStyleSheet(
        "QProgressBar { background-color: #1c222a; border-radius: 4px; height: 8px; } "
        "QProgressBar::chunk { background-color: #5e93b8; border-radius: 4px; }"
        );
    mainLayout->addWidget(m_toleranceBar);

    m_rangeLabel = new QLabel("допуск: -- – -- – --");
    m_rangeLabel->setStyleSheet("color: #7e8a98; font-family: 'IBM Plex Mono'; font-size: 12px;");
    mainLayout->addWidget(m_rangeLabel);

    // График
    m_plot = new QCustomPlot;
    m_plot->setMinimumHeight(200);
    mainLayout->addWidget(m_plot);
}

void DetailView::setupPlot()
{
    if (!m_plot) return;

    plottheme::apply(m_plot);
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->yAxis->setRange(0, 1023);
    m_plot->xAxis->setLabel("Такты (отсчёты)");
    m_plot->xAxis->setRange(0, m_historySize);

    // Основной график
    m_graph = m_plot->addGraph();
    m_graph->setPen(QPen(QColor(0x5e, 0x93, 0xb8), 2));

    // Линии допуска
    m_lowLine = m_plot->addGraph();
    m_lowLine->setPen(QPen(QColor(0xcf, 0x5b, 0x52), 1, Qt::DashLine));

    m_highLine = m_plot->addGraph();
    m_highLine->setPen(QPen(QColor(0xcf, 0x5b, 0x52), 1, Qt::DashLine));

    m_nomLine = m_plot->addGraph();
    m_nomLine->setPen(QPen(QColor(0x5e, 0x93, 0xb8), 1, Qt::DashLine));

    m_plot->replot();
}

void DetailView::setMetadataService(MetadataService* db)
{
    m_db = db;
}

void DetailView::setToleranceResolver(ToleranceResolver* r)
{
    m_resolver = r;
}

void DetailView::setChannel(const orbita::ChannelSpec& spec)
{
    m_spec = spec;
    m_numLabel->setText(QString("КАН %1").arg(
        // Номер канала из адреса можно извлечь, но пока просто индекс
        // В реальности нужно передавать индекс отдельно
        "?"
        ));
    m_nameLabel->setText(QString::fromStdString(spec.name.empty() ? spec.address : spec.name));

    m_tol = (m_resolver ? m_resolver->resolve(QString::fromStdString(spec.address)) : chstatus::forAddress(m_db, spec.address));
    if (m_db) {
        auto info = m_db->lookup(QString::fromStdString(spec.address));
        m_catLabel->setText(info ? info->category : QString());
    }

    // Сбрасываем историю
    m_history.assign(m_historySize, 0.0);
    m_currentValue = 0.0;
    m_isValid = false;
    updateUI();
}

void DetailView::updateValue(double value)
{
    if (!m_isValid) {
        // Первое значение – заполняем всю историю им
        m_history.assign(m_historySize, value);
        m_isValid = true;
    } else {
        m_history.pop_front();
        m_history.push_back(value);
    }
    m_currentValue = value;
    updateUI();
}

void DetailView::setHistorySize(int size)
{
    if (size < 2) size = 2;
    m_historySize = size;
    while ((int)m_history.size() > size)
        m_history.pop_front();
    while ((int)m_history.size() < size)
        m_history.push_front(0.0);
    if (m_plot) {
        m_plot->xAxis->setRange(0, size);
        m_plot->replot();
    }
}

void DetailView::updateUI()
{
    if (m_spec.address.empty() || !m_isValid) return;

    // Значение
    m_valueLabel->setText(QString::number(m_currentValue, 'f', 0));

    // Шкала допуска
    m_toleranceBar->setValue((int)m_currentValue);
    chstatus::Level lvl = chstatus::evaluate(m_currentValue, m_tol);

    if (m_tol.set)
        m_rangeLabel->setText(QString("допуск: %1 – %2 – %3")
                                  .arg(m_tol.lo).arg(m_tol.nominal).arg(m_tol.hi));
    else
        m_rangeLabel->setText("допуск не задан");

    m_statusLabel->setText(chstatus::text(lvl));
    m_statusLabel->setStyleSheet(QString("padding: 4px 10px; border-radius: 6px; color: %1;")
                                     .arg(chstatus::textColor(lvl).name()));

    m_toleranceBar->setStyleSheet(
        QString("QProgressBar { background-color: #1c222a; border-radius: 4px; height: 8px; } "
                "QProgressBar::chunk { background-color: %1; border-radius: 4px; }")
            .arg(chstatus::barColor(lvl).name()));

    // Статистика
    if (m_history.size() > 1) {
        double mn = *std::min_element(m_history.begin(), m_history.end());
        double mx = *std::max_element(m_history.begin(), m_history.end());
        double sum = std::accumulate(m_history.begin(), m_history.end(), 0.0);
        double avg = sum / m_history.size();
        m_statsLabel->setText(QString("мин: %1  сред: %2  макс: %3  размах: %4")
                                  .arg(mn, 0, 'f', 0)
                                  .arg(avg, 0, 'f', 0)
                                  .arg(mx, 0, 'f', 0)
                                  .arg(mx - mn, 0, 'f', 0));
    }

    // График обновляется по таймеру
}

void DetailView::updatePlot()
{
    if (!m_plot || !m_isValid || m_history.empty()) return;

    QVector<double> keys(m_history.size()), values(m_history.size());
    for (int i = 0; i < (int)m_history.size(); ++i) {
        keys[i] = i;
        values[i] = m_history[i];
    }

    m_graph->setData(keys, values);

    // Линии допуска — только если допуск задан, иначе скрываем (пустые данные)
    if (m_tol.set) {
        QVector<double> lowData(m_history.size(), m_tol.lo);
        QVector<double> highData(m_history.size(), m_tol.hi);
        QVector<double> nomData(m_history.size(), m_tol.nominal);
        m_lowLine->setData(keys, lowData);
        m_highLine->setData(keys, highData);
        m_nomLine->setData(keys, nomData);
    } else {
        m_lowLine->data()->clear();
        m_highLine->data()->clear();
        m_nomLine->data()->clear();
    }

    // Авто-масштаб Y
    m_plot->rescaleAxes();
    m_plot->yAxis->setRangeLower(0);
    m_plot->yAxis->setRangeUpper(qMax(1023.0, m_plot->yAxis->range().upper * 1.05));

    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DetailView::onBackClicked()
{
    emit backToMain();
}

void DetailView::onPrevClicked()
{
    emit navigateChannel(-1);
}

void DetailView::onNextClicked()
{
    emit navigateChannel(1);
}
