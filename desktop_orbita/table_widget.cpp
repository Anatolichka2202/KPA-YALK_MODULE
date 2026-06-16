#include "table_widget.h"
#include "channel_status.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QLabel>

TableWidget::TableWidget(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_table = new QTableWidget;
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({"№", "Канал", "Категория", "Код", "Допуск", "Уровень", "Статус"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table);

    connect(m_table, &QTableWidget::itemClicked, [this](QTableWidgetItem* item) {
        if (item) {
            int idx = item->data(Qt::UserRole).toInt();
            emit channelSelected(idx);
        }
    });
    connect(m_table, &QTableWidget::itemDoubleClicked, [this](QTableWidgetItem* item) {
        if (item) {
            int idx = item->data(Qt::UserRole).toInt();
            emit channelDoubleClicked(idx);
        }
    });
}

void TableWidget::setMetadataService(MetadataService* db)
{
    m_db = db;
}

void TableWidget::setChannels(const std::vector<orbita::ChannelSpec>& specs)
{
    m_specs = specs;
    updateTable();
}

void TableWidget::updateValues(const QMap<QString, double>& values)
{
    m_values = values;
    updateTable();
}

void TableWidget::setSelectedChannel(int index)
{
    m_selectedIndex = index;
    // Выделяем строку
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem* item = m_table->item(r, 0);
        if (item) {
            int idx = item->data(Qt::UserRole).toInt();
            m_table->setCurrentCell(r, 0);
            if (idx == index) {
                m_table->selectRow(r);
                m_table->scrollToItem(item);
                break;
            }
        }
    }
}

void TableWidget::updateTable()
{
    int n = m_specs.size();
    m_table->setRowCount(n);
    for (int i = 0; i < n; ++i) {
        const auto& spec = m_specs[i];
        double val = m_values.value(QString::fromStdString(spec.address), 0.0);
        QString name = QString::fromStdString(spec.name.empty() ? spec.address : spec.name);
        QString cat;
        if (m_db) {
            auto info = m_db->lookup(QString::fromStdString(spec.address));
            if (info) cat = info->category;
        }

        // №
        QTableWidgetItem* numItem = new QTableWidgetItem(QString::number(i+1));
        numItem->setData(Qt::UserRole, i);
        m_table->setItem(i, 0, numItem);

        // Канал
        m_table->setItem(i, 1, new QTableWidgetItem(name));

        // Категория
        m_table->setItem(i, 2, new QTableWidgetItem(cat));

        // Код
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(val, 'f', 0)));

        chstatus::Tolerance tol = chstatus::forAddress(m_db, spec.address);
        chstatus::Level lvl = chstatus::evaluate(val, tol);

        // Допуск
        m_table->setItem(i, 4, new QTableWidgetItem(
            tol.set ? QString("%1–%2–%3").arg(tol.lo).arg(tol.nominal).arg(tol.hi)
                    : QStringLiteral("—")));

        // Уровень (QProgressBar)
        QProgressBar* bar = new QProgressBar;
        bar->setRange(0, 1023);
        bar->setValue((int)val);
        bar->setTextVisible(false);
        bar->setStyleSheet(QString("QProgressBar { background-color: #1c222a; border-radius: 3px; height: 6px; } "
                                   "QProgressBar::chunk { background-color: %1; border-radius: 3px; }")
                               .arg(chstatus::barColor(lvl).name()));
        m_table->setCellWidget(i, 5, bar);

        // Статус
        QLabel* statusLabel = new QLabel(chstatus::text(lvl));
        statusLabel->setStyleSheet(QString("color: %1;").arg(chstatus::textColor(lvl).name()));
        m_table->setCellWidget(i, 6, statusLabel);
    }
    m_table->resizeColumnsToContents();
}
