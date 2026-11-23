#pragma once

// UI-only collection of HMI screens for one delivery.
// Does not read station configuration or provide device commands.

#include "hmi_presentation.h"
#include "ui_composition.h"

#include <QByteArray>
#include <QFrame>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class QComboBox;
class QDialog;
class QLineEdit;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace miltech::ui {

struct HmiPageRecord {
    QString id;
    QString title;
    HmiDocument* document = nullptr;
};

class HmiDeliveryCollection final : public QObject {
    Q_OBJECT
public:
    explicit HmiDeliveryCollection(QString deliveryId, QObject* parent = nullptr);
    ~HmiDeliveryCollection() override;

    int pageCount() const noexcept { return static_cast<int>(pages_.size()); }
    int currentIndex() const noexcept { return selectedIndex_; }
    HmiPageRecord pageAt(int index) const;
    QString deliveryId() const { return deliveryId_; }

    bool initialize(const QVector<HmiPageSpec>& templates, QString* error = nullptr);
    QString addPage(QString title);
    QString duplicatePage(int sourceIndex);
    bool renamePage(int index, const QString& title);
    bool deletePage(int index);
    bool setCurrentIndex(int index);

    QByteArray toJson() const;
    bool loadFromJson(const QByteArray& json, QString* error = nullptr);

signals:
    void pagesChanged();
    void currentPageChanged(int index);

private:
    QString uniqueId() const;
    void replacePages(QVector<HmiPageRecord> replacement);

    QString deliveryId_;
    QVector<HmiPageRecord> pages_;
    int selectedIndex_ = 0;
};

class HmiDeliveryEditor final : public QWidget {
    Q_OBJECT
public:
    explicit HmiDeliveryEditor(HmiDeliveryCollection* pages, QWidget* parent = nullptr);
    QComboBox* pageSelector() const noexcept { return pageSelector_; }
    QPushButton* addPageButton() const noexcept { return add_; }
    QPushButton* duplicatePageButton() const noexcept { return duplicate_; }
    QPushButton* deletePageButton() const noexcept { return remove_; }
    QPushButton* renamePageButton() const noexcept { return rename_; }
    QPushButton* fullscreenButton() const noexcept { return fullscreenButton_; }
    QDialog* fullscreenDialog() const noexcept { return fullscreen_.data(); }
    QLineEdit* nameInput() const noexcept { return name_; }
    HmiDesignerPage* currentDesigner() const;

private:
    void rebuild();
    void updateSelection();
    void notification(const QString& message);

    HmiDeliveryCollection* pages_ = nullptr;
    QComboBox* pageSelector_ = nullptr;
    QLineEdit* name_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* duplicate_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* rename_ = nullptr;
    QPushButton* fullscreenButton_ = nullptr;
    QPointer<QDialog> fullscreen_;
    QPushButton* save_ = nullptr;
    QPushButton* load_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* note_ = nullptr;
};

class HmiDeliveryViewer final : public QFrame {
    Q_OBJECT
public:
    explicit HmiDeliveryViewer(HmiDeliveryCollection* pages, QWidget* parent = nullptr);
    QComboBox* pageSelector() const noexcept { return selector_; }
    int visiblePageIndex() const;
    HmiReadOnlyPanel* currentPanel() const;

private:
    void rebuild();
    void updateSelection();

    HmiDeliveryCollection* pages_ = nullptr;
    QComboBox* selector_ = nullptr;
    QStackedWidget* views_ = nullptr;
};

} // namespace miltech::ui
