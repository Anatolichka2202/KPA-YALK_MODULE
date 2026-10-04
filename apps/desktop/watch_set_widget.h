#pragma once
/**
 * @file watch_set_widget.h
 * @brief Один редактируемый набор отслеживаемых каналов (WatchSet).
 *
 * И загрузка конфига, и добавление из библиотеки пишут сюда. Контейнер наборов
 * решает, как объединить изменения нескольких WatchSet перед передачей ядру.
 * Виджет поддерживает включение, удаление на лету и сохранение в .txt-конфиг.
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
    std::vector<orbita::ChannelSpec> enabledSpecs() const;

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
