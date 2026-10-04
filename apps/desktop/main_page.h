#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include "channel_list_widget.h"
#include "readout_widget.h"
#include "anomaly_widget.h"
#include "bar_chart_widget.h"
#include "table_widget.h"
#include "category_grid_widget.h"
#include "address_set_model.h"

class ToleranceResolver;
class QLabel;
class AddressSetsOverview;

class MainPage : public QWidget
{
    Q_OBJECT
public:
    explicit MainPage(QWidget *parent = nullptr);

    void setOrbita(orbita::Orbita* orbita); // не обязательно, можно передавать данные через методы
    void setMetadataService(MetadataService* db);
    void setToleranceResolver(ToleranceResolver* r);
    void setChannels(const std::vector<orbita::ChannelSpec>& specs);
    void updateData(const orbita::Snapshot& snap);
    void setSelectedChannel(int index);
    void setLayout(int layout); // 0=A, 1=B, 2=C
    // Переключает заголовок самостоятельного инженерного контура. Метод не
    // ограничивает возможности плагинов и не изменяет набор адресов.
    void setFreeWorkspace(bool enabled);
    void setAddressSets(const std::vector<AddressSetDefinition>& sets);

signals:
    void channelSelected(int index);
    void channelDoubleClicked(int index);

private slots:
    void onLayoutButtonClicked(int layout);

private:
    void setupLayoutA();
    void setupLayoutB();
    void setupLayoutC();

    QStackedWidget* m_layoutStack;
    QWidget* m_layoutA;
    QWidget* m_layoutB;
    QWidget* m_layoutC;

    // Компоновка A
    ChannelListWidget* m_channelList;
    ReadoutWidget* m_readout;
    AnomalyWidget* m_anomaly;
    BarChartWidget* m_barChartA; // для диаграммы в A (может быть та же, что в B, но пока разделим)

    // Компоновка B
    BarChartWidget* m_barChartB;
    TableWidget* m_table;

    // Компоновка C
    CategoryGridWidget* m_categoryGrid;

    MetadataService* m_db = nullptr;
    ToleranceResolver* m_resolver = nullptr;
    std::vector<orbita::ChannelSpec> m_specs;
    int m_selectedIndex = -1;

    // Кнопки переключения компоновок
    QHBoxLayout* m_layoutButtonsLayout;
    QPushButton* m_btnA;
    QPushButton* m_btnB;
    QPushButton* m_btnC;
    QWidget* m_freeWorkspaceHeader = nullptr;
    QLabel* m_freeWorkspaceSummary = nullptr;
    AddressSetsOverview* m_addressSetsOverview = nullptr;
    bool m_freeWorkspaceActive = false;
};
