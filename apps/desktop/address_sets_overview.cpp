#include "address_sets_overview.h"

#include "bar_chart_widget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

AddressSetsOverview::AddressSetsOverview(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("parallelAddressSetOverview"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    auto* caption = new QLabel(QStringLiteral("ПАРАЛЛЕЛЬНЫЕ НАБОРЫ · LIVE"), this);
    caption->setStyleSheet(QStringLiteral("color:#9ac7ff;font-weight:700;font-size:13px;"));
    layout->addWidget(caption);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* host = new QWidget(scroll);
    cardsLayout_ = new QHBoxLayout(host);
    cardsLayout_->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(host);
    layout->addWidget(scroll);
    setMinimumHeight(250);
    setVisible(false);
}

void AddressSetsOverview::setMetadataService(MetadataService* db)
{
    db_ = db;
    for (auto& card : cards_) card.chart->setMetadataService(db_);
}

void AddressSetsOverview::setToleranceResolver(ToleranceResolver* resolver)
{
    resolver_ = resolver;
    for (auto& card : cards_) card.chart->setToleranceResolver(resolver_);
}

void AddressSetsOverview::setAddressSets(const std::vector<AddressSetDefinition>& sets)
{
    sets_ = sets;
    rebuild();
}

void AddressSetsOverview::rebuild()
{
    while (cardsLayout_->count() > 0) {
        auto* item = cardsLayout_->takeAt(0);
        delete item->widget();
        delete item;
    }
    cards_.clear();
    for (const auto& set : sets_) {
        auto* frame = new QFrame(this);
        frame->setObjectName(QStringLiteral("addressSetLiveCard"));
        frame->setStyleSheet(QStringLiteral("#addressSetLiveCard{background:#111b25;border:1px solid #315b79;border-radius:5px;}"));
        frame->setMinimumWidth(430);
        auto* layout = new QVBoxLayout(frame);
        auto* title = new QLabel(set.title, frame);
        title->setStyleSheet(QStringLiteral("color:#e4f1ff;font-weight:700;font-size:16px;"));
        layout->addWidget(title);
        auto* status = new QLabel(frame);
        status->setStyleSheet(QStringLiteral("color:#b6c8d8;"));
        layout->addWidget(status);
        auto* chart = new BarChartWidget(frame);
        chart->setMinimumHeight(170);
        chart->setMetadataService(db_);
        chart->setToleranceResolver(resolver_);
        chart->setChannels(set.specs);
        layout->addWidget(chart, 1);
        cardsLayout_->addWidget(frame);
        cards_.push_back({status, chart, set.specs});
    }
    cardsLayout_->addStretch(1);
}

void AddressSetsOverview::updateData(const orbita::Snapshot& snapshot)
{
    QMap<QString, double> values;
    for (const auto& value : snapshot.values) values[QString::fromStdString(value.address)] = value.value;
    for (auto& card : cards_) {
        int received = 0;
        for (const auto& spec : card.specs)
            if (values.contains(QString::fromStdString(spec.address))) ++received;
        card.status->setText(received == 0
            ? QStringLiteral("НЕТ ДАННЫХ · адресов: %1").arg(card.specs.size())
            : QStringLiteral("ДАННЫЕ: %1 из %2 адресов").arg(received).arg(card.specs.size()));
        card.chart->updateValues(values);
    }
}
