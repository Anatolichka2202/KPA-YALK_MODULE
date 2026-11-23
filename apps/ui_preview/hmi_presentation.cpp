#include "hmi_presentation.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSet>
#include <QFont>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace miltech::ui {
namespace {

constexpr int canvasWidth = 960;
constexpr int canvasHeight = 540;
constexpr int gridStep = 20;
constexpr int maxElements = 80;

bool allowedType(const QString& type)
{
    return HmiDocument::allowedTypes().contains(type);
}

bool geometryValid(const QRect& area)
{
    return area.x() >= 0 && area.y() >= 0 && area.width() >= 100
        && area.height() >= 66 && area.right() < canvasWidth
        && area.bottom() < canvasHeight;
}

QString typeTitle(const QString& type)
{
    if (type == QStringLiteral("label")) return QStringLiteral("Текст / обозначение");
    if (type == QStringLiteral("numeric")) return QStringLiteral("Числовое поле");
    if (type == QStringLiteral("status")) return QStringLiteral("Состояние");
    if (type == QStringLiteral("bar")) return QStringLiteral("Уровень");
    if (type == QStringLiteral("trend")) return QStringLiteral("Мини-график");
    if (type == QStringLiteral("command")) return QStringLiteral("Команда (заблокирована)");
    return QStringLiteral("Неизвестный элемент");
}

bool demoBindingCompatible(const QString& binding, const QString& type)
{
    if (type == QStringLiteral("label")) return binding.isEmpty();
    if (type == QStringLiteral("command")) return false;
    if (type == QStringLiteral("status")) return binding == QStringLiteral("ui.demo.flag");
    if (type == QStringLiteral("bar")) return binding == QStringLiteral("ui.demo.load");
    if (type == QStringLiteral("trend"))
        return binding == QStringLiteral("ui.demo.voltage")
            || binding == QStringLiteral("ui.demo.current");
    if (type == QStringLiteral("numeric"))
        return binding == QStringLiteral("ui.demo.voltage")
            || binding == QStringLiteral("ui.demo.current")
            || binding == QStringLiteral("ui.demo.load");
    return false;
}

QString sampleValue(const QString& binding, const QString& type)
{
    // UI fixture values only; typed compatibility is checked before display.
    if (!demoBindingCompatible(binding, type)) return {};
    if (binding == QStringLiteral("ui.demo.voltage")) return QStringLiteral("12,40");
    if (binding == QStringLiteral("ui.demo.current")) return QStringLiteral("0,76");
    if (binding == QStringLiteral("ui.demo.load")) return QStringLiteral("63,0");
    if (binding == QStringLiteral("ui.demo.flag")) return QStringLiteral("ВКЛ · ДЕМО");
    return {};
}

QLabel* label(const QString& value, const char* role = "body")
{
    auto* out = new QLabel(value);
    out->setProperty("role", role);
    out->setWordWrap(true);
    return out;
}

void drawText(QGraphicsItem* parent, const QString& value, const QPointF& position,
              int pointSize, const QColor& color, bool bold = false, int maxWidth = 1000)
{
    QFont font(QStringLiteral("Segoe UI"), pointSize);
    font.setBold(bold);
    QFontMetrics metrics(font);
    auto* item = new QGraphicsSimpleTextItem(metrics.elidedText(value, Qt::ElideRight, maxWidth), parent);
    item->setFont(font);
    item->setBrush(color);
    item->setPos(position);
    item->setAcceptedMouseButtons(Qt::NoButton);
}

QColor borderColor(const QString& type)
{
    if (type == QStringLiteral("command")) return QColor(QStringLiteral("#6C6370"));
    if (type == QStringLiteral("status")) return QColor(QStringLiteral("#4B738E"));
    return QColor(QStringLiteral("#37566F"));
}

} // namespace

HmiDocument::HmiDocument(QString deliveryId, QObject* parent)
    : QObject(parent), deliveryId_(std::move(deliveryId))
{
}

QStringList HmiDocument::allowedTypes()
{
    return {
        QStringLiteral("label"),
        QStringLiteral("numeric"),
        QStringLiteral("status"),
        QStringLiteral("bar"),
        QStringLiteral("trend"),
        QStringLiteral("command")
    };
}

const HmiElement* HmiDocument::find(const QString& id) const
{
    const auto it = std::find_if(elements_.cbegin(), elements_.cend(),
        [&id](const HmiElement& value) { return value.id == id; });
    return it == elements_.cend() ? nullptr : &*it;
}

QByteArray HmiDocument::toJson() const
{
    QJsonArray elements;
    for (const auto& value : elements_) {
        QJsonObject element;
        element.insert(QStringLiteral("id"), value.id);
        element.insert(QStringLiteral("type"), value.type);
        element.insert(QStringLiteral("title"), value.title);
        element.insert(QStringLiteral("binding"), value.binding);
        element.insert(QStringLiteral("unit"), value.unit);
        element.insert(QStringLiteral("x"), value.geometry.x());
        element.insert(QStringLiteral("y"), value.geometry.y());
        element.insert(QStringLiteral("width"), value.geometry.width());
        element.insert(QStringLiteral("height"), value.geometry.height());
        elements.append(element);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("format"), QStringLiteral("miltech.ui.hmi.preview"));
    root.insert(QStringLiteral("delivery_id"), deliveryId_);
    root.insert(QStringLiteral("canvas_width"), canvasWidth);
    root.insert(QStringLiteral("canvas_height"), canvasHeight);
    root.insert(QStringLiteral("elements"), elements);
    root.insert(QStringLiteral("runtime_binding_resolved"), false);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool HmiDocument::loadFromJson(const QByteArray& json, QString* error)
{
    auto fail = [error](const QString& reason) {
        if (error) *error = reason;
        return false;
    };

    QJsonParseError parseError{};
    const auto parsed = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject())
        return fail(QStringLiteral("Невозможно прочитать JSON HMI"));
    const auto object = parsed.object();
    if (object.value(QStringLiteral("schema")).toInt() != 1
        || object.value(QStringLiteral("format")).toString()
             != QStringLiteral("miltech.ui.hmi.preview"))
        return fail(QStringLiteral("Неизвестная версия или формат HMI"));
    if (object.value(QStringLiteral("delivery_id")).toString() != deliveryId_)
        return fail(QStringLiteral("HMI предназначено для другой поставки"));
    if (object.value(QStringLiteral("canvas_width")).toInt() != canvasWidth
        || object.value(QStringLiteral("canvas_height")).toInt() != canvasHeight)
        return fail(QStringLiteral("Неподдерживаемые размеры HMI-полотна"));
    if (!object.value(QStringLiteral("elements")).isArray())
        return fail(QStringLiteral("Отсутствует массив элементов HMI"));

    const auto array = object.value(QStringLiteral("elements")).toArray();
    if (array.size() > maxElements)
        return fail(QStringLiteral("Слишком много элементов HMI"));
    QVector<HmiElement> next;
    QSet<QString> ids;
    static const QRegularExpression idPattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_-]{0,63}$"));
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            return fail(QStringLiteral("Неверный элемент HMI"));
        const auto e = value.toObject();
        if (!e.value(QStringLiteral("x")).isDouble()
            || !e.value(QStringLiteral("y")).isDouble()
            || !e.value(QStringLiteral("width")).isDouble()
            || !e.value(QStringLiteral("height")).isDouble())
            return fail(QStringLiteral("Неверная геометрия элемента"));
        const HmiElement element{
            e.value(QStringLiteral("id")).toString(),
            e.value(QStringLiteral("type")).toString(),
            e.value(QStringLiteral("title")).toString(),
            e.value(QStringLiteral("binding")).toString(),
            e.value(QStringLiteral("unit")).toString(),
            QRect(e.value(QStringLiteral("x")).toInt(),
                  e.value(QStringLiteral("y")).toInt(),
                  e.value(QStringLiteral("width")).toInt(),
                  e.value(QStringLiteral("height")).toInt())
        };
        if (!idPattern.match(element.id).hasMatch() || ids.contains(element.id))
            return fail(QStringLiteral("Повторяющийся или неверный ID элемента"));
        if (!allowedType(element.type) || element.title.trimmed().isEmpty()
            || !geometryValid(element.geometry)
            || element.title.size() > 160 || element.binding.size() > 256
            || element.unit.size() > 32)
            return fail(QStringLiteral("Неверные свойства элемента: %1").arg(element.id));
        ids.insert(element.id);
        next.push_back(element);
    }
    elements_ = std::move(next);
    if (error) error->clear();
    emit changed();
    return true;
}

QString HmiDocument::add(const QString& type)
{
    if (!allowedType(type) || elements_.size() >= maxElements) return {};
    int serial = 1;
    QString id;
    do {
        id = QStringLiteral("element_%1").arg(serial++);
    } while (find(id) != nullptr);
    const int index = static_cast<int>(elements_.size());
    const int w = type == QStringLiteral("trend") ? 245 : 195;
    const int h = type == QStringLiteral("trend") ? 145 : 105;
    const int x = 24 + (index % 4) * 225;
    const int y = 24 + ((index / 4) % 3) * 160;
    elements_.push_back(HmiElement{
        id, type, typeTitle(type),
        type == QStringLiteral("label") || type == QStringLiteral("command")
            ? QString() : type == QStringLiteral("status")
                ? QStringLiteral("ui.demo.flag") : type == QStringLiteral("bar")
                    ? QStringLiteral("ui.demo.load") : QStringLiteral("ui.demo.voltage"),
        type == QStringLiteral("numeric") ? QStringLiteral("В") : QString(),
        QRect(x, y, w, h)
    });
    emit changed();
    return id;
}

bool HmiDocument::updateElement(const HmiElement& updated)
{
    if (!allowedType(updated.type) || updated.title.trimmed().isEmpty()
        || !geometryValid(updated.geometry) || updated.title.size() > 160
        || updated.binding.size() > 256 || updated.unit.size() > 32)
        return false;
    for (auto& item : elements_) {
        if (item.id == updated.id) {
            item = updated;
            emit changed();
            return true;
        }
    }
    return false;
}

bool HmiDocument::moveElement(const QString& id, int x, int y)
{
    for (auto& item : elements_) {
        if (item.id == id) {
            item.geometry.moveTo(
                std::clamp(x, 0, canvasWidth - item.geometry.width()),
                std::clamp(y, 0, canvasHeight - item.geometry.height()));
            emit changed();
            return true;
        }
    }
    return false;
}

bool HmiDocument::remove(const QString& id)
{
    for (int i = 0; i < elements_.size(); ++i) {
        if (elements_.at(i).id == id) {
            elements_.removeAt(i);
            emit changed();
            return true;
        }
    }
    return false;
}

HmiCanvas::HmiCanvas(HmiDocument* document, QWidget* parent)
    : QGraphicsView(parent), document_(document),
      scene_(new QGraphicsScene(this))
{
    setObjectName(QStringLiteral("ui.hmi.canvas"));
    setAccessibleName(QStringLiteral("Полотно HMI-представления"));
    setAccessibleDescription(QStringLiteral(
        "Изменяемое отображение элементов HMI, не подключённое к физическим измерениям"));
    setScene(scene_);
    scene_->setSceneRect(0, 0, canvasWidth, canvasHeight);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setBackgroundBrush(QColor(QStringLiteral("#0B1927")));
    setMinimumHeight(260);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    connect(scene_, &QGraphicsScene::selectionChanged, this, &HmiCanvas::onSelected);
    connect(document_, &HmiDocument::changed, this, &HmiCanvas::redraw);
    redraw();
    applyZoom();
}

void HmiCanvas::setZoom(double factor)
{
    zoomFactor_ = factor;
    const auto policy = factor == 0.0 ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded;
    setHorizontalScrollBarPolicy(policy);
    setVerticalScrollBarPolicy(policy);
    applyZoom();
}

void HmiCanvas::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (zoomFactor_ == 0.0) applyZoom();
}

void HmiCanvas::applyZoom()
{
    if (viewport()->width() < 24 || viewport()->height() < 24) return;
    if (zoomFactor_ == 0.0) {
        fitInView(scene_->sceneRect().adjusted(-5, -5, 5, 5), Qt::KeepAspectRatio);
    } else {
        resetTransform();
        scale(zoomFactor_, zoomFactor_);
    }
}

void HmiCanvas::setEditing(bool editing)
{
    if (editing_ == editing) return;
    editing_ = editing;
    redraw();
}

void HmiCanvas::setDemoValues(bool enabled)
{
    if (demoValues_ == enabled) return;
    demoValues_ = enabled;
    redraw();
}

void HmiCanvas::selectElement(const QString& id)
{
    selectedId_ = id;
    {
        QSignalBlocker blocker(scene_);
        for (auto* item : scene_->items()) {
            if (item->data(0).toString() == id && item->flags() & QGraphicsItem::ItemIsSelectable)
                item->setSelected(true);
            else if (item->flags() & QGraphicsItem::ItemIsSelectable)
                item->setSelected(false);
        }
    }
    emit selectedElementChanged(selectedId_);
}

int HmiCanvas::renderedElementCount() const
{
    int count = 0;
    for (auto* item : scene_->items())
        if (!item->data(0).toString().isEmpty()) ++count;
    return count;
}

void HmiCanvas::onSelected()
{
    const auto selected = scene_->selectedItems();
    const QString next = selected.isEmpty() ? QString() : selected.constFirst()->data(0).toString();
    if (selectedId_ == next) return;
    selectedId_ = next;
    emit selectedElementChanged(selectedId_);
}

void HmiCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    QGraphicsView::mouseReleaseEvent(event);
    if (!editing_) return;
    const auto selected = scene_->selectedItems();
    if (selected.isEmpty()) return;
    QGraphicsItem* selectedItem = selected.constFirst();
    const QString id = selectedItem->data(0).toString();
    const HmiElement* element = document_->find(id);
    if (id.isEmpty() || !element) return;
    const QPointF position = selectedItem->pos();
    const int x = qRound(position.x() / gridStep) * gridStep;
    const int y = qRound(position.y() / gridStep) * gridStep;
    document_->moveElement(id, x, y);
}

void HmiCanvas::redraw()
{
    QSignalBlocker blocker(scene_);
    scene_->clear();
    for (int x = 0; x <= canvasWidth; x += gridStep) {
        const bool major = x % 100 == 0;
        scene_->addLine(x, 0, x, canvasHeight,
                        QPen(QColor(major ? QStringLiteral("#263D51")
                                          : QStringLiteral("#172C3D")), 1));
    }
    for (int y = 0; y <= canvasHeight; y += gridStep) {
        const bool major = y % 100 == 0;
        scene_->addLine(0, y, canvasWidth, y,
                        QPen(QColor(major ? QStringLiteral("#263D51")
                                          : QStringLiteral("#172C3D")), 1));
    }

    for (const auto& element : document_->elements()) {
        const int w = element.geometry.width();
        const int h = element.geometry.height();
        auto* root = scene_->addRect(QRectF(0, 0, w, h),
            QPen(borderColor(element.type), 1.3),
            QBrush(QColor(QStringLiteral("#162C3E"))));
        root->setData(0, element.id);
        root->setPos(element.geometry.topLeft());
        root->setToolTip(QStringLiteral("%1\nID: %2\nИсточник: %3")
                             .arg(element.title, element.id,
                                  element.binding.isEmpty()
                                      ? QStringLiteral("нет") : element.binding));
        if (editing_) {
            root->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable
                           | QGraphicsItem::ItemSendsGeometryChanges);
            root->setCursor(Qt::SizeAllCursor);
        } else {
            root->setAcceptedMouseButtons(Qt::NoButton);
        }
        if (editing_ && element.id == selectedId_) root->setSelected(true);

        const int availableWidth = w - 24;
        drawText(root, element.title, QPointF(12, 8), 10,
                 QColor(QStringLiteral("#D9E8F4")), true, availableWidth);
        if (element.type == QStringLiteral("label")) {
            drawText(root, QStringLiteral("ПОЯСНЕНИЕ / СТАТИЧНЫЙ ТЕКСТ"),
                     QPointF(12, qMin(h - 27, 43)), 8,
                     QColor(QStringLiteral("#8EAFC6")), false, availableWidth);
            continue;
        }

        if (element.type == QStringLiteral("command")) {
            drawText(root, QStringLiteral("ЗАПРЕЩЕНО · НЕТ COMMAND API"),
                     QPointF(12, qMin(h - 29, 45)), 9,
                     QColor(QStringLiteral("#E6BF94")), true, availableWidth);
            continue;
        }

        const QString raw = demoValues_ ? sampleValue(element.binding, element.type) : QString();
        const bool available = !raw.isEmpty();
        const QColor foreground = available ? QColor(QStringLiteral("#86D2F3"))
                                            : QColor(QStringLiteral("#9BB2C3"));
        if (element.type == QStringLiteral("trend")) {
            const QRectF plot(13, 39, w - 26, qMax(20, h - 68));
            auto* area = new QGraphicsRectItem(plot, root);
            area->setPen(QPen(QColor(QStringLiteral("#39576C"))));
            area->setBrush(QBrush(QColor(QStringLiteral("#0B1927"))));
            area->setAcceptedMouseButtons(Qt::NoButton);
            if (available) {
                QPainterPath path;
                for (int point = 0; point < 64; ++point) {
                    const qreal x = plot.left() + (plot.width() * point / 63.);
                    const qreal value = 0.53 + 0.25 * std::sin(point * .21);
                    const qreal y = plot.bottom() - value * plot.height();
                    if (!point) path.moveTo(x, y);
                    else path.lineTo(x, y);
                }
                auto* curve = new QGraphicsPathItem(path, root);
                curve->setPen(QPen(QColor(QStringLiteral("#83CFEE")), 2));
                curve->setAcceptedMouseButtons(Qt::NoButton);
            } else {
                drawText(root, QStringLiteral("НЕТ ДАННЫХ"), QPointF(21, 56),
                         10, foreground, true, availableWidth - 10);
            }
            drawText(root, available ? QStringLiteral("UI DEMO / НЕТ ШКАЛЫ ВРЕМЕНИ")
                                     : QStringLiteral("ТЕЛЕМЕТРИЯ НЕ ПОДКЛЮЧЕНА"),
                     QPointF(13, h - 24), 8, foreground, false, availableWidth);
            continue;
        }
        if (element.type == QStringLiteral("bar")) {
            auto* rail = new QGraphicsRectItem(QRectF(12, qMin(h - 35, 49), w - 24, 15), root);
            rail->setPen(Qt::NoPen);
            rail->setBrush(QBrush(QColor(QStringLiteral("#30455B"))));
            rail->setAcceptedMouseButtons(Qt::NoButton);
            if (available) {
                auto* fill = new QGraphicsRectItem(
                    QRectF(12, qMin(h - 35, 49), (w - 24) * .63, 15), root);
                fill->setPen(Qt::NoPen);
                fill->setBrush(QBrush(QColor(QStringLiteral("#6BBEDC"))));
                fill->setAcceptedMouseButtons(Qt::NoButton);
            }
            drawText(root, available ? QStringLiteral("63% · UI DEMO")
                                     : QStringLiteral("НЕТ ДАННЫХ"),
                     QPointF(12, h - 24), 8, foreground, true, availableWidth);
            continue;
        }
        if (element.type == QStringLiteral("status")) {
            drawText(root, available ? QStringLiteral("ВКЛ · UI DEMO")
                                     : QStringLiteral("НЕИЗВЕСТНО"),
                     QPointF(12, qMin(h - 31, 45)), 12, foreground, true, availableWidth);
        } else {
            drawText(root, available
                ? QStringLiteral("%1 %2").arg(raw, element.unit)
                : QStringLiteral("—  %1").arg(element.unit),
                QPointF(12, qMin(h - 37, 36)), 16, foreground, true, availableWidth);
        }
        drawText(root, available ? QStringLiteral("СИНТЕТИКА · НЕ ИЗМЕРЕНИЕ")
                                 : QStringLiteral("НЕТ ПОДКЛЮЧЕНИЯ"),
                 QPointF(12, h - 22), 8,
                 QColor(QStringLiteral("#9EB5C6")), false, availableWidth);
    }
    applyZoom();
    viewport()->update();
}

HmiDesignerPage::HmiDesignerPage(HmiDocument* document, QWidget* parent, bool compact)
    : QWidget(parent), document_(document)
{
    setObjectName(QStringLiteral("ui.hmi.designer"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(compact ? 6 : 18, compact ? 5 : 14,
                             compact ? 6 : 18, compact ? 5 : 14);
    root->setSpacing(compact ? 6 : 10);
    if (!compact) {
        root->addWidget(label(QStringLiteral("HMI / ПРОЕКТИРОВАНИЕ"), "eyebrow"));
        root->addWidget(label(QStringLiteral("HMI-представления"), "pageTitle"));
        root->addWidget(label(QStringLiteral(
            "Конструктор приборных экранов: элементы, положение, свойства и ключи данных. "
            "Вся логика пока только визуальная."), "muted"));
    }

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(label(QStringLiteral("Режим"), "muted"));
    mode_ = new QComboBox;
    mode_->setObjectName(QStringLiteral("ui.hmi.mode"));
    mode_->addItem(QStringLiteral("Редактор · без данных"));
    mode_->addItem(QStringLiteral("Предпросмотр · нет данных"));
    mode_->addItem(QStringLiteral("Предпросмотр · синтетика"));
    toolbar->addWidget(mode_);
    toolbar->addWidget(label(QStringLiteral("Масштаб"), "muted"));
    zoom_ = new QComboBox;
    zoom_->setObjectName(QStringLiteral("ui.hmi.zoom"));
    zoom_->setAccessibleName(QStringLiteral("Масштаб полотна HMI"));
    zoom_->addItem(QStringLiteral("Вписать"), 0.0);
    zoom_->addItem(QStringLiteral("100%"), 1.0);
    zoom_->addItem(QStringLiteral("75%"), 0.75);
    zoom_->addItem(QStringLiteral("50%"), 0.5);
    toolbar->addWidget(zoom_);
    toolbar->addStretch();

    import_ = new QPushButton(QStringLiteral("Открыть JSON"), this);
    import_->setObjectName(QStringLiteral("ui.hmi.import"));
    export_ = new QPushButton(QStringLiteral("Сохранить JSON"), this);
    export_->setObjectName(QStringLiteral("ui.hmi.export"));
    if (!compact) {
        toolbar->addWidget(import_);
        toolbar->addWidget(export_);
    } else {
        import_->hide();
        export_->hide();
    }
    root->addLayout(toolbar);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setObjectName(QStringLiteral("ui.hmi.splitter"));

    auto* paletteScroll = new QScrollArea;
    paletteScroll->setWidgetResizable(true);
    paletteScroll->setMinimumWidth(155);
    auto* paletteWidget = new QFrame;
    paletteWidget->setObjectName(QStringLiteral("card"));
    auto* paletteLayout = new QVBoxLayout(paletteWidget);
    paletteLayout->setContentsMargins(12, 12, 12, 12);
    paletteLayout->setSpacing(8);
    paletteLayout->addWidget(label(QStringLiteral("БИБЛИОТЕКА ЭЛЕМЕНТОВ"), "eyebrow"));
    for (const QString& type : HmiDocument::allowedTypes()) {
        auto* button = new QPushButton(typeTitle(type));
        button->setObjectName(QStringLiteral("ui.hmi.add.%1").arg(type));
        button->setAccessibleName(QStringLiteral("Добавить HMI-элемент: %1").arg(typeTitle(type)));
        button->setToolTip(QStringLiteral("Добавить элемент в полотно HMI"));
        paletteLayout->addWidget(button);
        palette_.push_back({type, button});
        connect(button, &QPushButton::clicked, this, [this, type] {
            const QString id = document_->add(type);
            if (!id.isEmpty()) {
                canvas_->selectElement(id);
                setNotice(QStringLiteral("Добавлен элемент %1. Перетащите его на полотне.").arg(id));
            } else {
                setNotice(QStringLiteral("Не удалось добавить элемент"));
            }
        });
    }
    paletteLayout->addStretch();
    paletteScroll->setWidget(paletteWidget);
    splitter->addWidget(paletteScroll);

    canvas_ = new HmiCanvas(document_);
    canvas_->setObjectName(QStringLiteral("ui.hmi.editorCanvas"));
    canvas_->setEditing(true);
    canvas_->setZoom(0.0);
    canvas_->setMinimumWidth(300);
    splitter->addWidget(canvas_);

    auto* inspectorScroll = new QScrollArea;
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setMinimumWidth(225);
    auto* inspector = new QFrame;
    inspector->setObjectName(QStringLiteral("card"));
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(13, 13, 13, 13);
    inspectorLayout->setSpacing(9);
    inspectorLayout->addWidget(label(QStringLiteral("СВОЙСТВА ЭЛЕМЕНТА"), "eyebrow"));

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setVerticalSpacing(5);
    id_ = new QLineEdit;
    id_->setObjectName(QStringLiteral("ui.hmi.id"));
    id_->setReadOnly(true);
    title_ = new QLineEdit;
    title_->setObjectName(QStringLiteral("ui.hmi.title"));
    binding_ = new QLineEdit;
    binding_->setObjectName(QStringLiteral("ui.hmi.binding"));
    binding_->setPlaceholderText(QStringLiteral("Не выбрано"));
    bindingChoice_ = new QComboBox;
    bindingChoice_->setObjectName(QStringLiteral("ui.hmi.bindingChoice"));
    bindingChoice_->setAccessibleName(QStringLiteral("Каталог демонстрационных источников для выбранного HMI-элемента"));
    bindingChoice_->addItem(QStringLiteral("— Без источника —"), QString());
    bindingChoice_->addItem(QStringLiteral("Напряжение · UI DEMO"), QStringLiteral("ui.demo.voltage"));
    bindingChoice_->addItem(QStringLiteral("Ток · UI DEMO"), QStringLiteral("ui.demo.current"));
    bindingChoice_->addItem(QStringLiteral("Уровень · UI DEMO"), QStringLiteral("ui.demo.load"));
    bindingChoice_->addItem(QStringLiteral("Дискретный статус · UI DEMO"), QStringLiteral("ui.demo.flag"));
    bindingHint_ = label(QStringLiteral("Выберите элемент"), "muted");
    bindingHint_->setObjectName(QStringLiteral("ui.hmi.bindingHint"));
    unit_ = new QLineEdit;
    unit_->setObjectName(QStringLiteral("ui.hmi.unit"));
    x_ = new QSpinBox;
    x_->setObjectName(QStringLiteral("ui.hmi.x"));
    y_ = new QSpinBox;
    y_->setObjectName(QStringLiteral("ui.hmi.y"));
    width_ = new QSpinBox;
    width_->setObjectName(QStringLiteral("ui.hmi.width"));
    height_ = new QSpinBox;
    height_->setObjectName(QStringLiteral("ui.hmi.height"));
    x_->setRange(0, canvasWidth);
    y_->setRange(0, canvasHeight);
    width_->setRange(100, canvasWidth);
    height_->setRange(66, canvasHeight);
    form->addRow(QStringLiteral("ID"), id_);
    form->addRow(QStringLiteral("Подпись"), title_);
    form->addRow(QStringLiteral("Источник"), bindingChoice_);
    form->addRow(QStringLiteral("Ключ данных"), binding_);
    form->addRow(QStringLiteral("Качество"), bindingHint_);
    form->addRow(QStringLiteral("Единица"), unit_);
    form->addRow(QStringLiteral("X"), x_);
    form->addRow(QStringLiteral("Y"), y_);
    form->addRow(QStringLiteral("Ширина"), width_);
    form->addRow(QStringLiteral("Высота"), height_);
    inspectorLayout->addLayout(form);
    inspectorLayout->addWidget(label(QStringLiteral(
        "Демо-ключи: ui.demo.voltage, ui.demo.current, ui.demo.load, ui.demo.flag. "
        "Другие ключи останутся без данных до будущего API."), "muted"));
    apply_ = new QPushButton(QStringLiteral("Применить свойства"));
    apply_->setObjectName(QStringLiteral("ui.hmi.apply"));
    inspectorLayout->addWidget(apply_);
    remove_ = new QPushButton(QStringLiteral("Удалить элемент"));
    remove_->setObjectName(QStringLiteral("ui.hmi.remove"));
    inspectorLayout->addWidget(remove_);
    inspectorLayout->addStretch();
    inspectorScroll->setWidget(inspector);
    splitter->addWidget(inspectorScroll);
    splitter->setSizes({185, 650, 275});

    root->addWidget(splitter, 1);
    notice_ = label(QStringLiteral("UI-only: источники и команды не подключены"), "warning");
    notice_->setObjectName(QStringLiteral("ui.hmi.notice"));
    root->addWidget(notice_);

    connect(canvas_, &HmiCanvas::selectedElementChanged,
            this, &HmiDesignerPage::showSelected);
    connect(document_, &HmiDocument::changed, this, [this] {
        showSelected(canvas_->selectedElement());
    });
    connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &HmiDesignerPage::updateMode);
    connect(zoom_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) { canvas_->setZoom(zoom_->itemData(index).toDouble()); });
    connect(bindingChoice_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
        if (index >= 0) binding_->setText(bindingChoice_->itemData(index).toString());
    });
    connect(binding_, &QLineEdit::textChanged, this, [this] {
        QSignalBlocker blocker(bindingChoice_);
        const int index = bindingChoice_->findData(binding_->text());
        bindingChoice_->setCurrentIndex(index >= 0 ? index : 0);
        refreshBindingHint();
    });
    connect(apply_, &QPushButton::clicked, this, &HmiDesignerPage::applySelected);
    connect(remove_, &QPushButton::clicked, this, [this] {
        const QString id = id_->text();
        canvas_->selectElement({});
        if (document_->remove(id))
            setNotice(QStringLiteral("Элемент удалён"));
    });
    connect(export_, &QPushButton::clicked, this, [this] {
        const QString filename = QFileDialog::getSaveFileName(
            this, QStringLiteral("Сохранить макет HMI"),
            QStringLiteral("%1_hmi.json").arg(document_->deliveryId()),
            QStringLiteral("HMI JSON (*.json)"));
        if (filename.isEmpty()) return;
        QSaveFile output(filename);
        if (!output.open(QIODevice::WriteOnly)
            || output.write(document_->toJson()) < 0 || !output.commit()) {
            setNotice(QStringLiteral("Не удалось сохранить HMI JSON"));
            return;
        }
        setNotice(QStringLiteral("HMI-черновик сохранён: %1").arg(filename));
    });
    connect(import_, &QPushButton::clicked, this, [this] {
        const QString filename = QFileDialog::getOpenFileName(
            this, QStringLiteral("Открыть макет HMI"), {},
            QStringLiteral("HMI JSON (*.json)"));
        if (filename.isEmpty()) return;
        QFile input(filename);
        if (!input.open(QIODevice::ReadOnly) || input.size() > 512 * 1024) {
            setNotice(QStringLiteral("Невозможно открыть файл или он слишком большой"));
            return;
        }
        QString error;
        if (!document_->loadFromJson(input.readAll(), &error)) {
            setNotice(QStringLiteral("Ошибка импорта: %1").arg(error));
            return;
        }
        canvas_->selectElement({});
        setNotice(QStringLiteral("Загружен HMI-черновик: %1").arg(filename));
    });
    showSelected({});
}

QPushButton* HmiDesignerPage::addButton(const QString& type) const
{
    for (const auto& item : palette_) {
        if (item.first == type) return item.second;
    }
    return nullptr;
}

void HmiDesignerPage::setNotice(const QString& notice)
{
    notice_->setText(notice);
}

void HmiDesignerPage::refreshBindingHint()
{
    const auto* selected = document_->find(id_->text());
    if (!selected) {
        bindingHint_->setText(QStringLiteral("Элемент не выбран"));
        return;
    }
    const QString binding = binding_->text();
    if (selected->type == QStringLiteral("label")) {
        bindingHint_->setText(QStringLiteral("Статический элемент"));
    } else if (selected->type == QStringLiteral("command")) {
        bindingHint_->setText(QStringLiteral("Команды заблокированы · нет API"));
    } else if (binding.isEmpty()) {
        bindingHint_->setText(QStringLiteral("Источник не выбран · нет данных"));
    } else if (demoBindingCompatible(binding, selected->type)) {
        bindingHint_->setText(QStringLiteral("Совместим · синтетический источник"));
    } else if (binding.startsWith(QStringLiteral("ui.demo."))) {
        bindingHint_->setText(QStringLiteral("Тип источника несовместим с виджетом"));
    } else {
        bindingHint_->setText(QStringLiteral("Источник не разрешён · нет данных"));
    }
}

void HmiDesignerPage::showSelected(const QString& id)
{
    const HmiElement* item = document_->find(id);
    const bool exists = item != nullptr;
    for (QWidget* field : QVector<QWidget*>{title_, binding_, bindingChoice_, unit_,
                                            x_, y_, width_, height_, apply_, remove_})
        field->setEnabled(exists && mode_->currentIndex() == 0);
    if (!exists) {
        id_->clear();
        title_->clear();
        binding_->clear();
        unit_->clear();
        x_->setValue(0);
        y_->setValue(0);
        width_->setValue(100);
        height_->setValue(66);
        refreshBindingHint();
        return;
    }
    id_->setText(item->id);
    title_->setText(item->title);
    binding_->setText(item->binding);
    unit_->setText(item->unit);
    x_->setValue(item->geometry.x());
    y_->setValue(item->geometry.y());
    width_->setValue(item->geometry.width());
    height_->setValue(item->geometry.height());
    refreshBindingHint();
}

void HmiDesignerPage::applySelected()
{
    const HmiElement* item = document_->find(id_->text());
    if (!item) return;
    HmiElement edited = *item;
    edited.title = title_->text();
    edited.binding = binding_->text();
    edited.unit = unit_->text();
    edited.geometry = QRect(x_->value(), y_->value(),
                            width_->value(), height_->value());
    if (!document_->updateElement(edited)) {
        setNotice(QStringLiteral("Ошибка свойств: проверьте поля и границы полотна 960×540"));
        return;
    }
    setNotice(QStringLiteral("Свойства элемента %1 обновлены").arg(edited.id));
}

void HmiDesignerPage::updateMode(int index)
{
    canvas_->setEditing(index == 0);
    canvas_->setDemoValues(index == 2);
    for (const auto& element : palette_) element.second->setEnabled(index == 0);
    showSelected(canvas_->selectedElement());
    if (index == 2)
        setNotice(QStringLiteral("СИНТЕТИКА · значения только из UI-фикстур, НЕ измерения"));
    else if (index == 1)
        setNotice(QStringLiteral("Предпросмотр без данных: реальный источник не подключён"));
    else
        setNotice(QStringLiteral("Редактор: перетащите элементы, затем настройте ключи справа"));
}

HmiReadOnlyPanel::HmiReadOnlyPanel(HmiDocument* document, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(9);
    layout->addWidget(label(QStringLiteral("HMI-представление"), "section"));
    layout->addWidget(label(QStringLiteral(
        "СБОРНОЙ ЭКРАН · КОНФИГУРАЦИЯ ИЗ РЕДАКТОРА · РЕАЛЬНЫЕ ИСТОЧНИКИ НЕ ПОДКЛЮЧЕНЫ"),
        "warning"));
    canvas_ = new HmiCanvas(document);
    canvas_->setObjectName(QStringLiteral("ui.hmi.viewerCanvas"));
    canvas_->setEditing(false);
    canvas_->setDemoValues(false);
    canvas_->setZoom(0.0);
    canvas_->setMinimumHeight(290);
    layout->addWidget(canvas_);
}

} // namespace miltech::ui
