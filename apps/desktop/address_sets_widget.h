#pragma once

/**
 * @file address_sets_widget.h
 * @brief Вкладочный редактор нескольких параллельных наборов адресов.
 */

#include "address_set_model.h"

#include <QWidget>

class MetadataService;
class QTabWidget;
class WatchSetWidget;

class AddressSetsWidget final : public QWidget {
    Q_OBJECT

public:
    explicit AddressSetsWidget(MetadataService* db, QWidget* parent = nullptr);

    void setFromSpecs(const std::vector<orbita::ChannelSpec>& specs);
    void addParams(const std::vector<orbita::ChannelSpec>& specs);
    int setCount() const noexcept;
    std::vector<orbita::ChannelSpec> combinedSpecs() const;
    std::vector<AddressSetDefinition> definitions() const;

signals:
    // Все наборы сведены в единый список без повторов адреса. Именно он
    // передаётся существующему Orbita, поэтому декодер и источник не дублируются.
    void combinedWatchSetChanged(const std::vector<orbita::ChannelSpec>& specs);
    void addressSetsChanged(const std::vector<AddressSetDefinition>& sets);
    void configSaved();

private slots:
    void addAddressSet();
    void removeAddressSet();
    void renameAddressSet();
    void setCurrentAddressSet(int index);
    void onCurrentSetChanged(const std::vector<orbita::ChannelSpec>& specs);

private:
    void createEditorForSet(int index);
    void refreshTabTitle(int index);
    void emitCombinedChanged();

    MetadataService* db_ = nullptr;
    AddressSetModel model_;
    QTabWidget* tabs_ = nullptr;
};
