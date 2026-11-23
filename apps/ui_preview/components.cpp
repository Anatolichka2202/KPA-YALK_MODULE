#include "components.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>

#include <cmath>

namespace miltech::ui {
namespace {

QLabel* label(const QString& value, const char* role = "body")
{
    auto* widget = new QLabel(value);
    widget->setWordWrap(true);
    widget->setProperty("role", role);
    return widget;
}

QVector<ParameterFixture> fixtureRows(bool reference)
{
    // Values below are exclusively synthetic UI fixtures. They do not
    // encode tolerances, physical wiring, device outputs or actual telemetry.
    if (reference) {
        return {
            {QStringLiteral("SIM.01"), QStringLiteral("Напряжение · эталонный пример"),
             QStringLiteral("В"), 12.0},
            {QStringLiteral("SIM.02"), QStringLiteral("Счётчик · пример"),
             QStringLiteral("код"), 512.0},
            {QStringLiteral("SIM.03"), QStringLiteral("Нормированный сигнал · пример"),
             QStringLiteral("отн."), 0.63},
        };
    }
    return {
        {QStringLiteral("DEMO.01"), QStringLiteral("Параметр 01 · пример"),
         QStringLiteral("В"), 12.34},
        {QStringLiteral("DEMO.02"), QStringLiteral("Параметр 02 · пример"),
         QStringLiteral("В"), 5.08},
        {QStringLiteral("DEMO.03"), QStringLiteral("Параметр 03 · пример"),
         QStringLiteral("мА"), 0.72},
        {QStringLiteral("DEMO.04"), QStringLiteral("Счётчик · пример"),
         QStringLiteral("код"), 512.0},
    };
}

QFrame* flowNode(const QString& title, const QString& caption)
{
    auto* node = new QFrame;
    node->setObjectName(QStringLiteral("item"));
    node->setMinimumWidth(110);
    auto* layout = new QVBoxLayout(node);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(5);
    layout->addWidget(label(title, "section"));
    layout->addWidget(label(caption, "muted"));
    return node;
}

} // namespace

ParameterModel::ParameterModel(QObject* parent)
    : QAbstractTableModel(parent), rows_(fixtureRows(false))
{
}

int ParameterModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int ParameterModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 5;
}

QVariant ParameterModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const QStringList titles = {
        QStringLiteral("ПАРАМЕТР"),
        QStringLiteral("ЗНАЧЕНИЕ"),
        QStringLiteral("ЕД."),
        QStringLiteral("ИСТОЧНИК"),
        QStringLiteral("КАЧЕСТВО"),
    };
    return titles.value(section);
}

QVariant ParameterModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()
        || index.column() < 0 || index.column() >= 5) {
        return {};
    }

    const auto& sample = rows_.at(index.row());
    const bool demo = syntheticVisible_;

    if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole) {
        switch (index.column()) {
        case 0:
            return QStringLiteral("%1  ·  %2").arg(sample.title, sample.code);
        case 1:
            return demo ? QLocale(QLocale::Russian).toString(sample.syntheticValue, 'f', 2)
                        : QStringLiteral("—");
        case 2:
            return sample.unit;
        case 3:
            return demo ? QStringLiteral("UI fixture") : QStringLiteral("—");
        case 4:
            return demo ? QStringLiteral("СИНТЕТИКА") : QStringLiteral("Нет данных");
        default:
            return {};
        }
    }
    if (role == Qt::ToolTipRole) {
        return demo
            ? QStringLiteral("Только демонстрационное значение. Не является телеметрией.")
            : QStringLiteral("Источники телеметрии не подключены.");
    }
    if (role == Qt::ForegroundRole) {
        if (index.column() == 4)
            return QColor(demo ? QStringLiteral("#88CDEB") : QStringLiteral("#A8BAC8"));
        if (index.column() == 1)
            return QColor(demo ? QStringLiteral("#EAF5FE") : QStringLiteral("#90A4B5"));
    }
    if (role == Qt::TextAlignmentRole && index.column() == 1)
        return static_cast<int>(Qt::AlignVCenter | Qt::AlignRight);

    return {};
}

void ParameterModel::setFixture(bool reference)
{
    beginResetModel();
    rows_ = fixtureRows(reference);
    endResetModel();
}

void ParameterModel::setSyntheticVisible(bool visible)
{
    if (syntheticVisible_ == visible) return;
    syntheticVisible_ = visible;
    if (!rows_.isEmpty())
        emit dataChanged(index(0, 1), index(rowCount() - 1, 4));
}

ParameterPanel::ParameterPanel(bool reference, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ui.parameters"));
    setMinimumWidth(260);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->addWidget(label(QStringLiteral("ИЗМЕРИТЕЛЬНЫЕ КАНАЛЫ / ПРИМЕР"), "eyebrow"));

    search_ = new QLineEdit;
    search_->setObjectName(QStringLiteral("ui.parameterSearch"));
    search_->setPlaceholderText(QStringLiteral("Найти параметр или код…"));
    search_->setAccessibleName(QStringLiteral("Поиск по демонстрационным параметрам"));
    layout->addWidget(search_);

    model_ = new ParameterModel(this);
    model_->setFixture(reference);
    filter_ = new QSortFilterProxyModel(this);
    filter_->setSourceModel(model_);
    filter_->setFilterKeyColumn(0);
    filter_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    connect(search_, &QLineEdit::textChanged, filter_,
            &QSortFilterProxyModel::setFilterFixedString);

    auto* table = new QTableView;
    table->setObjectName(QStringLiteral("ui.parameterTable"));
    table->setAccessibleName(QStringLiteral("Таблица синтетических и отсутствующих параметров"));
    table->setModel(filter_);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(44);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < model_->columnCount(); ++column)
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setMinimumHeight(180);
    layout->addWidget(table, 1);

    notice_ = label(QStringLiteral("СИНТЕТИКА · значения не получены со стенда"), "warning");
    layout->addWidget(notice_);
}

void ParameterPanel::setFixture(bool reference)
{
    search_->clear();
    model_->setFixture(reference);
}

void ParameterPanel::setSyntheticVisible(bool visible)
{
    model_->setSyntheticVisible(visible);
    notice_->setText(visible
        ? QStringLiteral("СИНТЕТИКА · значения не получены со стенда")
        : QStringLiteral("Нет данных · подключения к стенду нет"));
}

int ParameterPanel::filteredRowCount() const
{
    return filter_->rowCount();
}

TrendCanvas::TrendCanvas(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ui.trendCanvas"));
    setMinimumHeight(235);
    setAccessibleName(QStringLiteral("График демонстрационных значений"));
    setAccessibleDescription(QStringLiteral(
        "Тренд генерируется исключительно для проверки отрисовки интерфейса, не поступает с оборудования"));
}

void TrendCanvas::setSyntheticVisible(bool visible)
{
    if (syntheticVisible_ == visible) return;
    syntheticVisible_ = visible;
    update();
}

void TrendCanvas::setWaveform(int waveform)
{
    if (waveform_ == waveform) return;
    waveform_ = waveform;
    update();
}

void TrendCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF outer = rect().adjusted(1, 1, -2, -2);
    painter.setPen(QPen(QColor(QStringLiteral("#304B60")), 1));
    painter.setBrush(QColor(QStringLiteral("#101F2E")));
    painter.drawRoundedRect(outer, 10, 10);

    if (width() < 180 || height() < 140) return;

    QRectF plot = outer.adjusted(53, 40, -20, -43);
    painter.setPen(QColor(QStringLiteral("#9EB4C5")));
    painter.drawText(QRectF(18, 8, width() - 36, 23),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     syntheticVisible_
                         ? QStringLiteral("СИНТЕТИЧЕСКИЙ СИГНАЛ · UI DEMO")
                         : QStringLiteral("ТЕЛЕМЕТРИЯ НЕ ПОДКЛЮЧЕНА"));
    painter.setPen(QPen(QColor(QStringLiteral("#2D4559")), 1));
    for (int i = 0; i <= 4; ++i) {
        const qreal x = plot.left() + plot.width() * static_cast<qreal>(i) / 4.;
        const qreal y = plot.top() + plot.height() * static_cast<qreal>(i) / 4.;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    painter.setPen(QColor(QStringLiteral("#829EB2")));
    const QString top = waveform_ == 1 ? QStringLiteral("1023") : QStringLiteral("1,0");
    painter.drawText(QRectF(5, plot.top() - 7, 42, 18), Qt::AlignRight, top);
    painter.drawText(QRectF(5, plot.bottom() - 11, 42, 18),
                     Qt::AlignRight, QStringLiteral("0"));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 8, plot.width(), 23),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("ОТСЧЁТЫ · ДЕМОНСТРАЦИЯ, НЕ ВРЕМЕННАЯ ШКАЛА"));

    if (!syntheticVisible_) {
        painter.setPen(QColor(QStringLiteral("#B4C7D6")));
        painter.drawText(plot, Qt::AlignCenter,
                         QStringLiteral("Нет измерений\nИсточник данных отсутствует"));
        return;
    }

    QPainterPath path;
    const int samples = waveform_ == 1 ? 192 : 144;
    for (int i = 0; i < samples; ++i) {
        double value = 0.;
        if (waveform_ == 1) {
            value = static_cast<double>(i % 64) / 63.;
        } else {
            const double x = static_cast<double>(i);
            value = 0.5 + 0.25 * std::sin(x * 0.11) + 0.045 * std::sin(x * 0.49);
        }
        const qreal x = plot.left() + plot.width() * i / (samples - 1);
        const qreal y = plot.bottom() - plot.height() * value;
        if (i == 0) path.moveTo(x, y);
        else path.lineTo(x, y);
    }
    painter.setClipRect(plot.adjusted(-1, -1, 1, 1));
    painter.setPen(QPen(QColor(QStringLiteral("#7ECBEF")), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.setClipping(false);
}

TrendPanel::TrendPanel(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    setMinimumHeight(340);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto* header = new QHBoxLayout;
    auto* title = label(QStringLiteral("График сигнала"), "section");
    header->addWidget(title);
    header->addStretch();

    waveformSelector_ = new QComboBox;
    waveformSelector_->setObjectName(QStringLiteral("ui.waveformSelector"));
    waveformSelector_->setAccessibleName(QStringLiteral("Демонстрационная форма сигнала"));
    waveformSelector_->addItem(QStringLiteral("Непрерывный тренд"));
    waveformSelector_->addItem(QStringLiteral("Счётчик 0–1023 · пила"));
    header->addWidget(waveformSelector_);
    layout->addLayout(header);

    canvas_ = new TrendCanvas;
    layout->addWidget(canvas_, 1);
    connect(waveformSelector_, qOverload<int>(&QComboBox::currentIndexChanged),
            canvas_, &TrendCanvas::setWaveform);

    syntheticToggle_ = new QCheckBox(QStringLiteral("Показывать синтетические данные"));
    syntheticToggle_->setObjectName(QStringLiteral("ui.syntheticToggle"));
    syntheticToggle_->setChecked(true);
    connect(syntheticToggle_, &QCheckBox::toggled, canvas_,
            &TrendCanvas::setSyntheticVisible);
    layout->addWidget(syntheticToggle_);
    layout->addWidget(label(QStringLiteral(
        "Графики демонстрируют возможности UI. Точка времени, качество и допуски не выводятся без реального источника."),
        "muted"));
}

SignalTopology::SignalTopology(bool reference, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    layout->addWidget(label(QStringLiteral("Логическая схема представления"), "section"));
    layout->addWidget(label(QStringLiteral(
        "МАКЕТ · НЕ ФИЗИЧЕСКАЯ СХЕМА СОЕДИНЕНИЙ, НЕ ИНДИКАЦИЯ КОММУТАЦИИ"),
        "warning"));
    auto* nodes = new QHBoxLayout;
    nodes->setSpacing(9);
    if (reference) {
        nodes->addWidget(flowNode(QStringLiteral("Имитатор"),
                                  QStringLiteral("UI источник")));
        nodes->addWidget(label(QStringLiteral("→"), "section"));
        nodes->addWidget(flowNode(QStringLiteral("Параметры"),
                                  QStringLiteral("Представление")));
        nodes->addWidget(label(QStringLiteral("→"), "section"));
        nodes->addWidget(flowNode(QStringLiteral("Анализ"),
                                  QStringLiteral("Отображение")));
    } else {
        nodes->addWidget(flowNode(QStringLiteral("Источник"),
                                  QStringLiteral("Роль из профиля")));
        nodes->addWidget(label(QStringLiteral("→"), "section"));
        nodes->addWidget(flowNode(QStringLiteral("Коммутация"),
                                  QStringLiteral("Логическая роль")));
        nodes->addWidget(label(QStringLiteral("→"), "section"));
        nodes->addWidget(flowNode(QStringLiteral("Изделие"),
                                  QStringLiteral("УБСИ · тип")));
    }
    layout->addLayout(nodes);
}

EquipmentOverview::EquipmentOverview(const QStringList& components, QWidget* parent)
    : QWidget(parent), count_(static_cast<int>(components.size()))
{
    setObjectName(QStringLiteral("ui.equipmentOverview"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    list_ = new QListWidget;
    list_->setObjectName(QStringLiteral("ui.equipmentList"));
    list_->setAccessibleName(QStringLiteral("Список компонентов выбранной поставки"));
    list_->setMinimumWidth(280);
    for (const auto& component : components) {
        list_->addItem(component);
    }
    layout->addWidget(list_, 2);

    auto* detail = new QFrame;
    detail->setObjectName(QStringLiteral("card"));
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(20, 20, 20, 20);
    detailLayout->setSpacing(15);
    detailLayout->addWidget(label(QStringLiteral("КАРТОЧКА ОБОРУДОВАНИЯ"), "eyebrow"));
    selectedName_ = label(QStringLiteral("Не выбрано"), "section");
    detailLayout->addWidget(selectedName_);
    selectedDescription_ = label(QStringLiteral(
        "Выберите компонент для просмотра его проектного представления."), "muted");
    detailLayout->addWidget(selectedDescription_);
    detailLayout->addWidget(label(QStringLiteral("СТАТУС · НЕ ОПРОШЕН"), "warning"));
    detailLayout->addWidget(label(QStringLiteral(
        "Здесь позднее будут только подтверждённые ресурс, capability, статус и диагностические сообщения."),
        "muted"));
    detailLayout->addStretch();
    layout->addWidget(detail, 3);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        auto* selected = list_->item(row);
        if (!selected) {
            selectedName_->setText(QStringLiteral("Не выбрано"));
            selectedDescription_->setText(QStringLiteral("Выберите компонент."));
            return;
        }
        selectedName_->setText(selected->text());
        selectedDescription_->setText(QStringLiteral(
            "Отображаемый компонент присутствует в UI-фикстуре поставки. "
            "Фактическая доступность и состояние пока неизвестны."));
    });
    if (!components.isEmpty()) list_->setCurrentRow(0);
}

EventLogPanel::EventLogPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ui.eventLog"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(9);
    layout->addWidget(label(QStringLiteral("ТОЛЬКО UI-СОБЫТИЯ / НЕ ЖУРНАЛ СТЕНДА"), "eyebrow"));
    events_ = new QListWidget;
    events_->setObjectName(QStringLiteral("ui.eventList"));
    events_->setAccessibleName(QStringLiteral("Журнал действий с прототипом"));
    events_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(events_, 1);
    appendUiEvent(QStringLiteral("Интерфейсный прототип создан. Физический стенд не подключён."));
}

void EventLogPanel::appendUiEvent(const QString& text)
{
    if (events_->count() >= 100)
        delete events_->takeItem(0);
    events_->addItem(QStringLiteral("UI · %1").arg(text));
    events_->scrollToBottom();
}

int EventLogPanel::entryCount() const
{
    return events_->count();
}

} // namespace miltech::ui
