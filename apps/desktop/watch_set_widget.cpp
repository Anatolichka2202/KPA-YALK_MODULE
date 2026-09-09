#include "watch_set_widget.h"
#include "metadata_service.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QDir>
#include <algorithm>

WatchSetWidget::WatchSetWidget(MetadataService* db, QWidget* parent)
    : QWidget(parent), db_(db)
{
    auto* layout = new QVBoxLayout(this);

    table_ = new QTableWidget;
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({"Канал", "Параметр", "Тип"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);

    auto* btnRow = new QHBoxLayout;
    auto* removeBtn = new QPushButton("Убрать");
    auto* clearBtn  = new QPushButton("Очистить");
    auto* saveBtn   = new QPushButton("Сохранить как конфиг…");
    btnRow->addWidget(removeBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();
    btnRow->addWidget(saveBtn);
    layout->addLayout(btnRow);

    connect(removeBtn, &QPushButton::clicked, this, &WatchSetWidget::onRemoveSelected);
    connect(clearBtn,  &QPushButton::clicked, this, &WatchSetWidget::onClear);
    connect(saveBtn,   &QPushButton::clicked, this, &WatchSetWidget::onSave);
    connect(table_,    &QTableWidget::itemChanged, this, &WatchSetWidget::onItemChanged);
}

bool WatchSetWidget::contains(const std::string& address) const {
    for (const auto& e : entries_)
        if (e.spec.address == address) return true;
    return false;
}

void WatchSetWidget::setFromSpecs(const std::vector<orbita::ChannelSpec>& specs) {
    entries_.clear();
    for (const auto& s : specs)
        entries_.push_back({s, true});
    rebuildTable();
    emitChanged();
}

void WatchSetWidget::addParams(const std::vector<orbita::ChannelSpec>& specs) {
    bool added = false;
    for (const auto& s : specs) {
        if (s.address.empty() || contains(s.address)) continue;
        entries_.push_back({s, true});
        added = true;
    }
    if (added) {
        rebuildTable();
        emitChanged();
    }
}

void WatchSetWidget::rebuildTable() {
    updating_ = true;
    table_->setRowCount(static_cast<int>(entries_.size()));
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& e = entries_[i];

        auto* addrItem = new QTableWidgetItem(QString::fromStdString(e.spec.address));
        addrItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        addrItem->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);

        QString type;
        if (db_) {
            auto info = db_->lookup(QString::fromStdString(e.spec.address));
            if (info) {
                type = info->signalType;
                if (info->isZu) type += " · ЗУ";
            }
        }

        table_->setItem(i, 0, addrItem);
        table_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(e.spec.name)));
        table_->setItem(i, 2, new QTableWidgetItem(type));
    }
    table_->resizeColumnToContents(0);
    updating_ = false;
}

void WatchSetWidget::onItemChanged(QTableWidgetItem* item) {
    if (updating_ || item->column() != 0) return;
    int row = item->row();
    if (row < 0 || row >= static_cast<int>(entries_.size())) return;
    entries_[row].enabled = (item->checkState() == Qt::Checked);
    emitChanged();
}

void WatchSetWidget::onRemoveSelected() {
    auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    std::vector<int> idx;
    for (const auto& r : rows) idx.push_back(r.row());
    std::sort(idx.rbegin(), idx.rend());
    for (int r : idx)
        if (r >= 0 && r < static_cast<int>(entries_.size()))
            entries_.erase(entries_.begin() + r);
    rebuildTable();
    emitChanged();
}

void WatchSetWidget::onClear() {
    if (entries_.empty()) return;
    entries_.clear();
    rebuildTable();
    emitChanged();
}

void WatchSetWidget::onSave() {
    if (entries_.empty()) return;
    QString dir = QCoreApplication::applicationDirPath() + "/address";
    QDir().mkpath(dir);
    QString path = QFileDialog::getSaveFileName(this, "Сохранить набор как конфиг",
                                                dir + "/новый.txt", "Text files (*.txt)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (const auto& e : entries_) {
        out << QString::fromStdString(e.spec.address);
        if (!e.spec.name.empty())
            out << '\t' << QString::fromStdString(e.spec.name);
        out << '\n';
    }
    f.close();
    emit configSaved();
}

void WatchSetWidget::emitChanged() {
    std::vector<orbita::ChannelSpec> enabled;
    for (const auto& e : entries_)
        if (e.enabled) enabled.push_back(e.spec);
    emit watchSetChanged(enabled);
}
