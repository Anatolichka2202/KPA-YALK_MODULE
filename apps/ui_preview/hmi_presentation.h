#pragma once

// Authoring and rendering of HMI views. Entirely presentation-only:
// does not discover devices, subscribe to telemetry or execute commands.

#include <QByteArray>
#include <QFrame>
#include <QGraphicsView>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QComboBox;
class QGraphicsScene;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QSplitter;
class QVBoxLayout;
class QMouseEvent;

namespace miltech::ui {

struct HmiElement {
    QString id;
    QString type;      // label | numeric | status | bar | trend | command
    QString title;
    QString binding;   // unresolved key, NEVER an active hardware route
    QString unit;
    QRect geometry;    // fixed logical canvas coordinates; pixels at 100% zoom
    QString groupId;   // optional visual group; never an equipment capability
};

class HmiDocument final : public QObject {
    Q_OBJECT
public:
    explicit HmiDocument(QString deliveryId, QObject* parent = nullptr);

    QString deliveryId() const { return deliveryId_; }
    const QVector<HmiElement>& elements() const noexcept { return elements_; }
    const HmiElement* find(const QString& id) const;
    QByteArray toJson() const;
    bool loadFromJson(const QByteArray& json, QString* error = nullptr);
    QString add(const QString& type);
    bool updateElement(const HmiElement& updated);
    bool moveElement(const QString& id, int x, int y);
    bool remove(const QString& id);
    QStringList duplicateElements(const QStringList& ids);
    bool removeElements(const QStringList& ids);
    bool translateElements(const QStringList& ids, int dx, int dy);
    bool alignElements(const QStringList& ids, const QString& edge);
    bool groupElements(const QStringList& ids);
    bool ungroupElements(const QStringList& ids);
    QStringList expandGroups(const QStringList& ids) const;
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return historyIndex_ > 0; }
    bool canRedo() const noexcept { return historyIndex_ + 1 < history_.size(); }
    static QStringList allowedTypes();

signals:
    void changed();
    void historyChanged();

private:
    QString deliveryId_;
    QVector<HmiElement> elements_;
    QVector<QVector<HmiElement>> history_;
    int historyIndex_ = 0;
    void checkpoint();
};

class HmiCanvas final : public QGraphicsView {
    Q_OBJECT
public:
    explicit HmiCanvas(HmiDocument* document, QWidget* parent = nullptr);
    ~HmiCanvas() override;
    void setEditing(bool editing);
    bool editing() const noexcept { return editing_; }
    void setDemoValues(bool enabled);
    void setZoom(double factor); // 0 = fit all, otherwise fixed scale (e.g. 1.0)
    double zoomFactor() const noexcept { return zoomFactor_; }
    bool demoValues() const noexcept { return demoValues_; }
    void selectElement(const QString& id);
    void selectElements(const QStringList& ids);
    QStringList selectedElements() const { return selectedIds_; }
    QString selectedElement() const { return selectedId_; }
    int renderedElementCount() const;

signals:
    void selectedElementChanged(const QString& id);
    void selectionChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void redraw();
    void onSelected();
    void applyZoom();
    HmiDocument* document_ = nullptr;
    QGraphicsScene* scene_ = nullptr;
    bool editing_ = false;
    bool demoValues_ = false;
    double zoomFactor_ = 0.0;
    QString selectedId_;
    QStringList selectedIds_;
    QPoint dragStart_;
};

class HmiDesignerPage final : public QWidget {
    Q_OBJECT
public:
    explicit HmiDesignerPage(HmiDocument* document, QWidget* parent = nullptr,
                             bool compact = false);
    HmiCanvas* canvas() const noexcept { return canvas_; }
    HmiDocument* document() const noexcept { return document_; }
    QComboBox* previewMode() const noexcept { return mode_; }
    QComboBox* zoomChoice() const noexcept { return zoom_; }
    QComboBox* bindingChoice() const noexcept { return bindingChoice_; }
    QPushButton* addButton(const QString& type) const;
    QPushButton* exportButton() const noexcept { return export_; }
    QPushButton* importButton() const noexcept { return import_; }
    QPushButton* editCommand(const QString& id) const;

private:
    void showSelected(const QString& id);
    void applySelected();
    void updateMode(int index);
    void setNotice(const QString& notice);
    void refreshBindingHint();
    void updateEditActions();

    HmiDocument* document_ = nullptr;
    HmiCanvas* canvas_ = nullptr;
    QComboBox* mode_ = nullptr;
    QComboBox* zoom_ = nullptr;
    QLineEdit* id_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* binding_ = nullptr;
    QComboBox* bindingChoice_ = nullptr;
    QLabel* bindingHint_ = nullptr;
    QLineEdit* unit_ = nullptr;
    QSpinBox* x_ = nullptr;
    QSpinBox* y_ = nullptr;
    QSpinBox* width_ = nullptr;
    QSpinBox* height_ = nullptr;
    QPushButton* apply_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* export_ = nullptr;
    QPushButton* import_ = nullptr;
    QLabel* notice_ = nullptr;
    QVector<QPair<QString, QPushButton*>> palette_;
    QVector<QPair<QString, QPushButton*>> edits_;
    QLabel* selectionInfo_ = nullptr;
};

class HmiReadOnlyPanel final : public QFrame {
    Q_OBJECT
public:
    explicit HmiReadOnlyPanel(HmiDocument* document, QWidget* parent = nullptr);
    HmiCanvas* canvas() const noexcept { return canvas_; }
    HmiDocument* document() const noexcept { return document_; }

private:
    HmiDocument* document_ = nullptr;
    HmiCanvas* canvas_ = nullptr;
};

} // namespace miltech::ui
