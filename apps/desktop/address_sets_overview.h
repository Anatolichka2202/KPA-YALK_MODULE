#pragma once

/**
 * @file address_sets_overview.h
 * @brief Одновременное отображение live-данных независимых наборов адресов.
 */

#include "address_set_model.h"

#include <QWidget>

class BarChartWidget;
class QLabel;
class MetadataService;
class ToleranceResolver;
class QHBoxLayout;

class AddressSetsOverview final : public QWidget {
    Q_OBJECT
public:
    explicit AddressSetsOverview(QWidget* parent = nullptr);
    void setMetadataService(MetadataService* db);
    void setToleranceResolver(ToleranceResolver* resolver);
    void setAddressSets(const std::vector<AddressSetDefinition>& sets);
    void updateData(const orbita::Snapshot& snapshot);

private:
    struct Card {
        QLabel* status = nullptr;
        BarChartWidget* chart = nullptr;
        std::vector<orbita::ChannelSpec> specs;
    };
    void rebuild();

    MetadataService* db_ = nullptr;
    ToleranceResolver* resolver_ = nullptr;
    QHBoxLayout* cardsLayout_ = nullptr;
    std::vector<AddressSetDefinition> sets_;
    std::vector<Card> cards_;
};
