#include "category_grid_widget.h"
#include <QVBoxLayout>
#include <QDebug>

CategoryGridWidget::CategoryGridWidget(QWidget *parent)
    : QWidget(parent)
    , m_grid(nullptr)
    , m_container(nullptr)
    , m_scroll(nullptr)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_container = new QWidget;
    m_grid = new QGridLayout(m_container);
    m_grid->setSpacing(12);
    m_grid->setContentsMargins(12, 12, 12, 12);

    m_scroll->setWidget(m_container);
    layout->addWidget(m_scroll);
}

void CategoryGridWidget::setMetadataService(MetadataService* db)
{
    m_db = db;
    for (auto card : m_cards)
        card->setMetadataService(db);
    if (!m_specs.empty())
        rebuildGrid();
}

void CategoryGridWidget::setChannels(const std::vector<orbita::ChannelSpec>& specs)
{
    m_specs = specs;
    rebuildGrid();
}

void CategoryGridWidget::rebuildGrid()
{
    // Очищаем
    for (auto card : m_cards) {
        m_grid->removeWidget(card);
        card->deleteLater();
    }
    m_cards.clear();

    if (!m_db || m_specs.empty()) return;

    // Группируем по категориям
    QMap<QString, QList<int>> catMap;
    for (int i = 0; i < (int)m_specs.size(); ++i) {
        QString cat = m_db->getCategory(QString::fromStdString(m_specs[i].address));
        if (cat.isEmpty()) cat = "Прочие";
        catMap[cat].append(i);
    }

    int row = 0, col = 0;
    const int maxCols = 2; // 2 колонки

    for (auto it = catMap.begin(); it != catMap.end(); ++it) {
        CategoryCardWidget* card = new CategoryCardWidget;
        card->setMetadataService(m_db);
        QList<int> qlist = it.value();
        std::vector<int> indices(qlist.begin(), qlist.end());
        card->setCategory(it.key(), indices, m_specs);

        connect(card, &CategoryCardWidget::channelSelected, this, &CategoryGridWidget::channelSelected);
        connect(card, &CategoryCardWidget::channelDoubleClicked, this, &CategoryGridWidget::channelDoubleClicked);

        m_grid->addWidget(card, row, col);
        m_cards[it.key()] = card;

        col++;
        if (col >= maxCols) {
            col = 0;
            row++;
        }
    }

    // Обновить значения
    updateValues(m_values);
}

void CategoryGridWidget::updateValues(const QMap<QString, double>& values)
{
    m_values = values;
    for (auto card : m_cards)
        card->updateValues(values);
}

void CategoryGridWidget::setSelectedChannel(int index)
{
    for (auto card : m_cards)
        card->setSelectedChannel(index);
}
