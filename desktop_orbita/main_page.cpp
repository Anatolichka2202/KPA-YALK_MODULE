#include "main_page.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>

MainPage::MainPage(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Панель переключения компоновок
    m_layoutButtonsLayout = new QHBoxLayout;
    m_layoutButtonsLayout->setContentsMargins(8, 6, 8, 6);
    m_layoutButtonsLayout->addStretch();
    m_btnA = new QPushButton("A");
    m_btnB = new QPushButton("B");
    m_btnC = new QPushButton("C");
    m_btnA->setCheckable(true);
    m_btnB->setCheckable(true);
    m_btnC->setCheckable(true);
    m_btnA->setChecked(true);
    connect(m_btnA, &QPushButton::clicked, [this]() { onLayoutButtonClicked(0); });
    connect(m_btnB, &QPushButton::clicked, [this]() { onLayoutButtonClicked(1); });
    connect(m_btnC, &QPushButton::clicked, [this]() { onLayoutButtonClicked(2); });
    m_layoutButtonsLayout->addWidget(m_btnA);
    m_layoutButtonsLayout->addWidget(m_btnB);
    m_layoutButtonsLayout->addWidget(m_btnC);
    m_layoutButtonsLayout->addStretch();
    mainLayout->addLayout(m_layoutButtonsLayout);

    // Стек компоновок
    m_layoutStack = new QStackedWidget;
    mainLayout->addWidget(m_layoutStack);

    // Создаём три компоновки
    setupLayoutA();
    setupLayoutB();
    setupLayoutC();

    // По умолчанию A
    m_layoutStack->setCurrentIndex(0);

    // Подключаем сигналы выбора от дочерних виджетов
    connect(m_channelList, &ChannelListWidget::channelSelected, this, &MainPage::channelSelected);
    connect(m_channelList, &ChannelListWidget::channelDoubleClicked, this, &MainPage::channelDoubleClicked);
    connect(m_barChartA, &BarChartWidget::barClicked, this, &MainPage::channelSelected);
    connect(m_barChartA, &BarChartWidget::barDoubleClicked, this, &MainPage::channelDoubleClicked);
    connect(m_barChartB, &BarChartWidget::barClicked, this, &MainPage::channelSelected);
    connect(m_barChartB, &BarChartWidget::barDoubleClicked, this, &MainPage::channelDoubleClicked);
    connect(m_table, &TableWidget::channelSelected, this, &MainPage::channelSelected);
    connect(m_table, &TableWidget::channelDoubleClicked, this, &MainPage::channelDoubleClicked);
    connect(m_anomaly, &AnomalyWidget::channelSelected, this, &MainPage::channelSelected);
    connect(m_categoryGrid, &CategoryGridWidget::channelSelected, this, &MainPage::channelSelected);
    connect(m_categoryGrid, &CategoryGridWidget::channelDoubleClicked, this, &MainPage::channelDoubleClicked);
}

void MainPage::setupLayoutA()
{
    m_layoutA = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(m_layoutA);
    layout->setContentsMargins(0, 0, 0, 0);

    // Левая панель: список каналов
    m_channelList = new ChannelListWidget;
    m_channelList->setMinimumWidth(200);
    layout->addWidget(m_channelList);

    // Центральная область: Readout + диаграмма
    QWidget* center = new QWidget;
    QVBoxLayout* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(4, 4, 4, 4);
    m_readout = new ReadoutWidget;
    m_readout->setMinimumHeight(120);
    centerLayout->addWidget(m_readout);
    m_barChartA = new BarChartWidget;
    centerLayout->addWidget(m_barChartA, 1);
    layout->addWidget(center, 1);

    // Правая панель: контроль допусков
    m_anomaly = new AnomalyWidget;
    m_anomaly->setMinimumWidth(200);
    layout->addWidget(m_anomaly);

    m_layoutStack->addWidget(m_layoutA);
}

void MainPage::setupLayoutB()
{
    m_layoutB = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(m_layoutB);
    layout->setContentsMargins(0, 0, 0, 0);

    m_barChartB = new BarChartWidget;
    m_barChartB->setMinimumHeight(200);
    layout->addWidget(m_barChartB, 1);

    m_table = new TableWidget;
    layout->addWidget(m_table, 1);

    m_layoutStack->addWidget(m_layoutB);
}

void MainPage::setupLayoutC()
{
    m_layoutC = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(m_layoutC);
    layout->setContentsMargins(0, 0, 0, 0);

    m_categoryGrid = new CategoryGridWidget;
    layout->addWidget(m_categoryGrid);

    m_layoutStack->addWidget(m_layoutC);
}

void MainPage::onLayoutButtonClicked(int layout)
{
    m_layoutStack->setCurrentIndex(layout);
    m_btnA->setChecked(layout == 0);
    m_btnB->setChecked(layout == 1);
    m_btnC->setChecked(layout == 2);
}

void MainPage::setMetadataService(MetadataService* db)
{
    m_db = db;
    m_channelList->setMetadataService(db);
    m_readout->setMetadataService(db);
    m_anomaly->setMetadataService(db);
    m_barChartA->setMetadataService(db);
    m_barChartB->setMetadataService(db);
    m_table->setMetadataService(db);
    m_categoryGrid->setMetadataService(db);
}

void MainPage::setChannels(const std::vector<orbita::ChannelSpec>& specs)
{
    m_specs = specs;
    m_channelList->setChannels(specs);
    m_anomaly->setChannels(specs);
    m_barChartA->setChannels(specs);
    m_barChartB->setChannels(specs);
    m_table->setChannels(specs);
    m_categoryGrid->setChannels(specs);
}

void MainPage::updateData(const orbita::Snapshot& snap)
{
    // Преобразуем snap.values в QMap<QString, double>
    QMap<QString, double> values;
    for (const auto& v : snap.values)
        values[QString::fromStdString(v.address)] = v.value;

    m_channelList->updateValues(values);
    m_anomaly->updateValues(values);
    m_barChartA->updateValues(values);
    m_barChartB->updateValues(values);
    m_table->updateValues(values);
    m_categoryGrid->updateValues(values);

    // Обновляем readout, если выбранный канал есть
    if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_specs.size()) {
        const auto& spec = m_specs[m_selectedIndex];
        double val = values.value(QString::fromStdString(spec.address), 0.0);
        m_readout->setChannel(spec);
        m_readout->updateValue(val);
    }

    // Синхронизируем выделение
    setSelectedChannel(m_selectedIndex);
}

void MainPage::setSelectedChannel(int index)
{
    m_selectedIndex = index;
    m_channelList->setSelectedChannel(index);
    m_anomaly->setSelectedChannel(index);
    m_barChartA->setSelectedBar(index);
    m_barChartB->setSelectedBar(index);
    m_table->setSelectedChannel(index);
    m_categoryGrid->setSelectedChannel(index);
}

void MainPage::setLayout(int layout)
{
    onLayoutButtonClicked(layout);
}
