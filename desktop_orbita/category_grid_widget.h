#pragma once

#include <QWidget>
#include <QGridLayout>
#include <QScrollArea>
#include <vector>
#include <QMap>
#include "category_card_widget.h"
#include "metadata_service.h"
#include "orbita.h"

class ToleranceResolver;

class CategoryGridWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CategoryGridWidget(QWidget *parent = nullptr);

    void setMetadataService(MetadataService* db);
    void setToleranceResolver(ToleranceResolver* r);
    void setChannels(const std::vector<orbita::ChannelSpec>& specs);
    void updateValues(const QMap<QString, double>& values);
    void setSelectedChannel(int index);

signals:
    void channelSelected(int index);
    void channelDoubleClicked(int index);

private:
    void rebuildGrid();

    MetadataService* m_db = nullptr;
    ToleranceResolver* m_resolver = nullptr;
    std::vector<orbita::ChannelSpec> m_specs;
    QMap<QString, double> m_values;
    QMap<QString, CategoryCardWidget*> m_cards;
    QGridLayout* m_grid;
    QWidget* m_container;
    QScrollArea* m_scroll;
};
