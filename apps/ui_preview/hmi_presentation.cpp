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
#include <QHash>
#include <QFont>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShortcut>
#include <QKeySequence>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>
#include <functional>

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
    history_.append(elements_);
}

QStringList HmiDocument::allowedTypes()
{
    return {
        QStringLiteral("label"), QStringLiteral("numeric"),
        QStringLiteral("status"), QStringLiteral("bar"),
        QStringLiteral("trend"), QStringLiteral("command")
    };
}

const HmiElement* HmiDocument::find(const QString& id) const
{
    const auto it = std::find_if(elements_.cbegin(), elements_.cend(),
        [&id](const HmiElement& value) { return value.id == id; });
    return it == elements_.cend() ? nullptr : &*it;
}

void HmiDocument::checkpoint()
{
    // One snapshot per logical user action; undo never re-enters this function.
    history_.resize(historyIndex_ + 1);
    history_.append(elements_);
    if (history_.size() > 100) history_.removeFirst();
    historyIndex_ = history_.size() - 1;
    emit changed();
    emit historyChanged();
}

bool HmiDocument::undo()
{
    if (!canUndo()) return false;
    elements_ = history_.at(--historyIndex_);
    emit changed();
    emit historyChanged();
    return true;
}

bool HmiDocument::redo()
{
    if (!canRedo()) return false;
    elements_ = history_.at(++historyIndex_);
    emit changed();
    emit historyChanged();
    return true;
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
        if (!value.groupId.isEmpty())
            element.insert(QStringLiteral("groupId"), value.groupId);
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
                  e.value(QStringLiteral("height")).toInt()),
            e.value(QStringLiteral("groupId")).toString()
        };
        if (!idPattern.match(element.id).hasMatch() || ids.contains(element.id))
            return fail(QStringLiteral("Повторяющийся или неверный ID элемента"));
        if (!allowedType(element.type) || element.title.trimmed().isEmpty()
            || !geometryValid(element.geometry)
            || element.title.size() > 160 || element.binding.size() > 256
            || element.unit.size() > 32
            || (!element.groupId.isEmpty()
                && !idPattern.match(element.groupId).hasMatch()))
            return fail(QStringLiteral("Неверные свойства элемента: %1").arg(element.id));
        ids.insert(element.id);
        next.push_back(element);
    }
    elements_ = std::move(next);
    history_.clear();
    history_.append(elements_);
    historyIndex_ = 0;
    if (error) error->clear();
    emit changed();
    emit historyChanged();
    return true;
}

QString HmiDocument::add(const QString& type)
{
    if (!allowedType(type) || elements_.size() >= maxElements) return {};
    int serial = 1;
    QString id;
    do { id = QStringLiteral("element_%1").arg(serial++); }
    while (find(id) != nullptr);
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
        QRect(x, y, w, h), {}
    });
    checkpoint();
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
            if (item.type == updated.type && item.title == updated.title
                && item.binding == updated.binding && item.unit == updated.unit
                && item.geometry == updated.geometry) return true;
            const QString originalGroup = item.groupId;
            item = updated;
            item.groupId = originalGroup; // group membership is changed via group/ungroup only
            checkpoint();
            return true;
        }
    }
    return false;
}

QStringList HmiDocument::expandGroups(const QStringList& ids) const
{
    QSet<QString> groups;
    QSet<QString> direct;
    for (const QString& id : ids) {
        if (const auto* element = find(id)) {
            direct.insert(id);
            if (!element->groupId.isEmpty()) groups.insert(element->groupId);
        }
    }
    QStringList output;
    for (const auto& element : elements_) {
        if (direct.contains(element.id) || (!element.groupId.isEmpty()
                                             && groups.contains(element.groupId)))
            output.append(element.id);
    }
    return output;
}

QStringList HmiDocument::duplicateElements(const QStringList& ids)
{
    const QStringList expanded = expandGroups(ids);
    if (expanded.isEmpty() || elements_.size() + expanded.size() > maxElements) return {};
    const QSet<QString> include(expanded.begin(), expanded.end());
    QSet<QString> used;
    QSet<QString> usedGroups;
    for (const auto& e : elements_) {
        used.insert(e.id);
        if (!e.groupId.isEmpty()) usedGroups.insert(e.groupId);
    }
    QStringList produced;
    QVector<HmiElement> copies;
    QHash<QString, QString> groupMapping;
    int idSerial = 1, groupSerial = 1;
    for (const auto& e : elements_) {
        if (!include.contains(e.id)) continue;
        HmiElement copy = e;
        do { copy.id = QStringLiteral("element_%1").arg(idSerial++); }
        while (used.contains(copy.id));
        used.insert(copy.id);
        copy.geometry.moveTo(
            qMin(canvasWidth - copy.geometry.width(), copy.geometry.x() + gridStep),
            qMin(canvasHeight - copy.geometry.height(), copy.geometry.y() + gridStep));
        if (!copy.groupId.isEmpty()) {
            if (!groupMapping.contains(copy.groupId)) {
                QString groupId;
                do { groupId = QStringLiteral("group_%1").arg(groupSerial++); }
                while (usedGroups.contains(groupId));
                usedGroups.insert(groupId);
                groupMapping.insert(copy.groupId, groupId);
            }
            copy.groupId = groupMapping.value(copy.groupId);
        }
        produced.append(copy.id);
        copies.push_back(copy);
    }
    for (const auto& copy : copies) elements_.append(copy);
    checkpoint();
    return produced;
}

bool HmiDocument::removeElements(const QStringList& ids)
{
    const QStringList expanded = expandGroups(ids);
    const QSet<QString> include(expanded.begin(), expanded.end());
    if (include.isEmpty()) return false;
    const int before = elements_.size();
    elements_.erase(std::remove_if(elements_.begin(), elements_.end(),
        [&include](const HmiElement& e) { return include.contains(e.id); }),
        elements_.end());
    if (elements_.size() == before) return false;
    checkpoint();
    return true;
}

bool HmiDocument::remove(const QString& id)
{
    return removeElements(QStringList{id});
}

bool HmiDocument::translateElements(const QStringList& ids, int dx, int dy)
{
    const QStringList expanded = expandGroups(ids);
    if (expanded.isEmpty()) return false;
    const QSet<QString> include(expanded.begin(), expanded.end());
    int minX = canvasWidth, minY = canvasHeight, maxX = 0, maxY = 0;
    for (const auto& e : elements_) {
        if (!include.contains(e.id)) continue;
        minX = qMin(minX, e.geometry.left());
        minY = qMin(minY, e.geometry.top());
        maxX = qMax(maxX, e.geometry.right());
        maxY = qMax(maxY, e.geometry.bottom());
    }
    dx = std::clamp(dx, -minX, canvasWidth - 1 - maxX);
    dy = std::clamp(dy, -minY, canvasHeight - 1 - maxY);
    if (dx == 0 && dy == 0) return false;
    for (auto& e : elements_) {
        if (include.contains(e.id)) e.geometry.translate(dx, dy);
    }
    checkpoint();
    return true;
}

bool HmiDocument::moveElement(const QString& id, int x, int y)
{
    const auto* element = find(id);
    if (!element) return false;
    return translateElements(QStringList{id}, x - element->geometry.x(),
                            y - element->geometry.y());
}

bool HmiDocument::alignElements(const QStringList& ids, const QString& edge)
{
    const QStringList expanded = expandGroups(ids);
    if (expanded.size() < 2) return false;
    const QSet<QString> include(expanded.begin(), expanded.end());
    QRect bounds;
    bool first = true;
    for (const auto& e : elements_) {
        if (!include.contains(e.id)) continue;
        bounds = first ? e.geometry : bounds.united(e.geometry);
        first = false;
    }
    if (first) return false;
    bool modified = false;
    for (auto& e : elements_) {
        if (!include.contains(e.id)) continue;
        QRect next = e.geometry;
        if (edge == QStringLiteral("left")) next.moveLeft(bounds.left());
        else if (edge == QStringLiteral("right")) next.moveRight(bounds.right());
        else if (edge == QStringLiteral("top")) next.moveTop(bounds.top());
        else if (edge == QStringLiteral("bottom")) next.moveBottom(bounds.bottom());
        else if (edge == QStringLiteral("hcenter")) next.moveCenter(QPoint(bounds.center().x(), next.center().y()));
        else if (edge == QStringLiteral("vcenter")) next.moveCenter(QPoint(next.center().x(), bounds.center().y()));
        else return false;
        if (next != e.geometry && geometryValid(next)) {
            e.geometry = next;
            modified = true;
        }
    }
    if (modified) checkpoint();
    return modified;
}

bool HmiDocument::groupElements(const QStringList& ids)
{
    const QStringList expanded = expandGroups(ids);
    if (expanded.size() < 2) return false;
    const QSet<QString> include(expanded.begin(), expanded.end());
    int serial = 1;
    QString group;
    bool exists;
    do {
        group = QStringLiteral("group_%1").arg(serial++);
        exists = std::any_of(elements_.cbegin(), elements_.cend(),
            [&group](const HmiElement& e) { return e.groupId == group; });
    } while (exists);
    for (auto& e : elements_)
        if (include.contains(e.id)) e.groupId = group;
    checkpoint();
    return true;
}

bool HmiDocument::ungroupElements(const QStringList& ids)
{
    const QStringList expanded = expandGroups(ids);
    const QSet<QString> include(expanded.begin(), expanded.end());
    bool modified = false;
    for (auto& e : elements_) {
        if (include.contains(e.id) && !e.groupId.isEmpty()) {
            e.groupId.clear();
            modified = true;
        }
    }
    if (modified) checkpoint();
    return modified;
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
    selectElements(id.isEmpty() ? QStringList{} : QStringList{id});
}

void HmiCanvas::selectElements(const QStringList& ids)
{
    const QStringList next = editing_ ? document_->expandGroups(ids) : QStringList{};
    const bool different = next != selectedIds_;
    selectedIds_ = next;
    selectedId_ = selectedIds_.isEmpty() ? QString() : selectedIds_.constFirst();
    {
        QSignalBlocker blocker(scene_);
        for (auto* item : scene_->items()) {
            if (item->flags() & QGraphicsItem::ItemIsSelectable)
                item->setSelected(selectedIds_.contains(item->data(0).toString()));
        }
    }
    if (different) {
        emit selectedElementChanged(selectedId_);
        emit selectionChanged();
    }
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
    QSet<QString> raw;
    for (auto* item : scene_->selectedItems()) {
        const QString id = item->data(0).toString();
        if (!id.isEmpty()) raw.insert(id);
    }
    QStringList requested;
    for (const auto& element : document_->elements())
        if (raw.contains(element.id)) requested.append(element.id);
    selectElements(requested);
}

void HmiCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    QGraphicsView::mouseReleaseEvent(event);
    if (!editing_) return;
    const auto selected = scene_->selectedItems();
    if (selected.isEmpty()) return;
    QGraphicsItem* item = selected.constFirst();
    const QString id = item->data(0).toString();
    const HmiElement* reference = document_->find(id);
    if (!reference) return;
    const QPointF position = item->pos();
    const int nextX = qRound(position.x() / gridStep) * gridStep;
    const int nextY = qRound(position.y() / gridStep) * gridStep;
    // One history checkpoint per group drag, not one for each selected widget.
    document_->translateElements(selectedIds_, nextX - reference->geometry.x(),
                                 nextY - reference->geometry.y());
}

void HmiCanvas::redraw()
{
    QSignalBlocker blocker(scene_);
    const QStringList stillSelected = document_->expandGroups(selectedIds_);
    const bool selectionLost = stillSelected != selectedIds_;
    selectedIds_ = stillSelected;
    selectedId_ = selectedIds_.isEmpty() ? QString() : selectedIds_.constFirst();
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
            QPen(element.groupId.isEmpty()
                ? borderColor(element.type) : QColor(QStringLiteral("#67AFCB")), 1.3),
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
        if (editing_ && selectedIds_.contains(element.id)) root->setSelected(true);

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
    if (selectionLost) {
        emit selectedElementChanged(selectedId_);
        emit selectionChanged();
    }
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

    // Multi-object tools stay independent from the station's runtime capabilities.
    auto* editRow = new QHBoxLayout;
    editRow->setSpacing(6);
    auto* alignmentRow = new QHBoxLayout;
    alignmentRow->setSpacing(6);
    auto edit = [this](QHBoxLayout* layout, const QString& id,
                       const QString& caption, const QString& tip) {
        auto* button = new QPushButton(caption);
        button->setObjectName(QStringLiteral("ui.hmi.action.%1").arg(id));
        button->setToolTip(tip);
        layout->addWidget(button);
        edits_.append({id, button});
        return button;
    };
    auto* undoButton = edit(editRow, QStringLiteral("undo"), QStringLiteral("↶ Отменить"),
                            QStringLiteral("Ctrl+Z · отменить изменение HMI"));
    auto* redoButton = edit(editRow, QStringLiteral("redo"), QStringLiteral("↷ Повторить"),
                            QStringLiteral("Ctrl+Shift+Z · повторить изменение HMI"));
    auto* duplicateButton = edit(editRow, QStringLiteral("duplicate"), QStringLiteral("Копия"),
                                 QStringLiteral("Ctrl+D · дублировать выбранные элементы"));
    auto* groupButton = edit(editRow, QStringLiteral("group"), QStringLiteral("Группа"),
                             QStringLiteral("Ctrl+G · объединить выделенные элементы"));
    auto* ungroupButton = edit(editRow, QStringLiteral("ungroup"), QStringLiteral("Разгрупп."),
                               QStringLiteral("Ctrl+Shift+G · разделить группу"));
    editRow->addStretch(1);
    root->addLayout(editRow);

    auto* leftButton = edit(alignmentRow, QStringLiteral("align.left"),
                            QStringLiteral("Лево"), QStringLiteral("По левому краю"));
    auto* centerButton = edit(alignmentRow, QStringLiteral("align.hcenter"),
                              QStringLiteral("Центр X"), QStringLiteral("По горизонтальному центру"));
    auto* rightButton = edit(alignmentRow, QStringLiteral("align.right"),
                             QStringLiteral("Право"), QStringLiteral("По правому краю"));
    auto* topButton = edit(alignmentRow, QStringLiteral("align.top"),
                           QStringLiteral("Верх"), QStringLiteral("По верхнему краю"));
    auto* middleButton = edit(alignmentRow, QStringLiteral("align.vcenter"),
                              QStringLiteral("Центр Y"), QStringLiteral("По вертикальному центру"));
    auto* bottomButton = edit(alignmentRow, QStringLiteral("align.bottom"),
                              QStringLiteral("Низ"), QStringLiteral("По нижнему краю"));
    alignmentRow->addStretch(1);
    root->addLayout(alignmentRow);

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
    selectionInfo_ = label(QStringLiteral("Ctrl+Click · выбрать несколько элементов"), "muted");
    selectionInfo_->setObjectName(QStringLiteral("ui.hmi.selectionCount"));
    inspectorLayout->addWidget(selectionInfo_);

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
    connect(canvas_, &HmiCanvas::selectionChanged,
            this, &HmiDesignerPage::updateEditActions);
    connect(document_, &HmiDocument::historyChanged,
            this, &HmiDesignerPage::updateEditActions);

    connect(undoButton, &QPushButton::clicked, document_, &HmiDocument::undo);
    connect(redoButton, &QPushButton::clicked, document_, &HmiDocument::redo);
    connect(duplicateButton, &QPushButton::clicked, this, [this] {
        const QStringList created = document_->duplicateElements(canvas_->selectedElements());
        if (!created.isEmpty()) {
            canvas_->selectElements(created);
            setNotice(QStringLiteral("Создано копий: %1").arg(created.size()));
        }
    });
    connect(groupButton, &QPushButton::clicked, this, [this] {
        if (document_->groupElements(canvas_->selectedElements()))
            setNotice(QStringLiteral("Элементы объединены; перетаскивание группы — одним действием"));
    });
    connect(ungroupButton, &QPushButton::clicked, this, [this] {
        if (document_->ungroupElements(canvas_->selectedElements()))
            setNotice(QStringLiteral("Группа разделена"));
    });
    for (const auto& action : QVector<QPair<QPushButton*, QString>>{
        {leftButton, QStringLiteral("left")}, {centerButton, QStringLiteral("hcenter")},
        {rightButton, QStringLiteral("right")}, {topButton, QStringLiteral("top")},
        {middleButton, QStringLiteral("vcenter")}, {bottomButton, QStringLiteral("bottom")}}) {
        connect(action.first, &QPushButton::clicked, this, [this, edge = action.second] {
            if (document_->alignElements(canvas_->selectedElements(), edge))
                setNotice(QStringLiteral("Выравнивание применено к выбранным элементам"));
        });
    }
    auto shortcut = [this](const QString& sequence, const std::function<void()>& handler) {
        auto* key = new QShortcut(QKeySequence(sequence), canvas_);
        key->setContext(Qt::WidgetWithChildrenShortcut);
        connect(key, &QShortcut::activated, this, handler);
    };
    shortcut(QStringLiteral("Ctrl+Z"), [this] { if (canvas_->editing()) document_->undo(); });
    shortcut(QStringLiteral("Ctrl+Shift+Z"), [this] { if (canvas_->editing()) document_->redo(); });
    shortcut(QStringLiteral("Ctrl+D"), [duplicateButton] { if (duplicateButton->isEnabled()) duplicateButton->click(); });
    shortcut(QStringLiteral("Ctrl+G"), [groupButton] { if (groupButton->isEnabled()) groupButton->click(); });
    shortcut(QStringLiteral("Ctrl+Shift+G"), [ungroupButton] { if (ungroupButton->isEnabled()) ungroupButton->click(); });
    shortcut(QStringLiteral("Ctrl+A"), [this] {
        if (!canvas_->editing()) return;
        QStringList ids;
        for (const auto& element : document_->elements()) ids.append(element.id);
        canvas_->selectElements(ids);
    });
    shortcut(QStringLiteral("Delete"), [this] {
        if (canvas_->editing() && !canvas_->selectedElements().isEmpty()) {
            const QStringList ids = canvas_->selectedElements();
            canvas_->selectElements({});
            document_->removeElements(ids);
        }
    });
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
        const QStringList ids = canvas_->selectedElements();
        canvas_->selectElements({});
        if (document_->removeElements(ids))
            setNotice(QStringLiteral("Удалено элементов: %1").arg(ids.size()));
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
