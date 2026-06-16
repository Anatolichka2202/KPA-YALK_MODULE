#pragma once
/**
 * @file watch_set_widget.h
 * @brief Активный набор отслеживаемых каналов (WatchSet) — единый источник истины UI.
 *
 * И загрузка конфига, и добавление из библиотеки пишут сюда. Любое изменение
 * эмитит watchSetChanged(enabledSpecs) -> MainWindow зовёт orbita_->setChannels().
 * Поддерживает вкл/выкл/удаление на лету и сохранение набора в .txt-конфиг.
 */

#include <QWidget>
#include <vector>
#include "orbita.h"   // orbita::ChannelSpec

class QTableWidget;
class QTableWidgetItem;
class MetadataService;

class WatchSetWidget : public QWidget {
    Q_OBJECT
public:
    explicit WatchSetWidget(MetadataService* db, QWidget* parent = nullptr);

    void setFromSpecs(const std::vector<orbita::ChannelSpec>& specs); // заменить (конфиг)
    void addParams(const std::vector<orbita::ChannelSpec>& specs);    // добавить (библиотека)

signals:
    void watchSetChanged(const std::vector<orbita::ChannelSpec>& enabledSpecs);
    void configSaved();   // после сохранения набора в файл

private slots:
    void onRemoveSelected();
    void onClear();
    void onSave();
    void onItemChanged(QTableWidgetItem* item);

private:
    struct Entry {
        orbita::ChannelSpec spec;
        bool enabled = true;
    };

    void rebuildTable();
    void emitChanged();
    bool contains(const std::string& address) const;

    std::vector<Entry> entries_;
    MetadataService*   db_;
    QTableWidget*      table_;
    bool               updating_ = false;
};
