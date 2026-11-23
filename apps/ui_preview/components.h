#pragma once

// A collection of Qt Widgets presentation components for UI-only exploration.
// No classes in this file depend on station runtime, telemetry or drivers.

#include <QAbstractTableModel>
#include <QFrame>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSortFilterProxyModel;
class QTableView;

namespace miltech::ui {

struct ParameterFixture {
    QString code;
    QString title;
    QString unit;
    double syntheticValue = 0.0;
};

class ParameterModel final : public QAbstractTableModel {
public:
    explicit ParameterModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setFixture(bool reference);
    void setSyntheticVisible(bool visible);
    bool syntheticVisible() const noexcept { return syntheticVisible_; }

private:
    QVector<ParameterFixture> rows_;
    bool syntheticVisible_ = true;
};

class ParameterPanel final : public QWidget {
public:
    explicit ParameterPanel(bool reference, QWidget* parent = nullptr);
    void setFixture(bool reference);
    void setSyntheticVisible(bool visible);
    int filteredRowCount() const;
    QLineEdit* searchInput() const noexcept { return search_; }

private:
    ParameterModel* model_ = nullptr;
    QSortFilterProxyModel* filter_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* notice_ = nullptr;
};

class TrendCanvas final : public QWidget {
public:
    explicit TrendCanvas(QWidget* parent = nullptr);
    void setSyntheticVisible(bool visible);
    void setWaveform(int waveform);
    bool syntheticVisible() const noexcept { return syntheticVisible_; }
    int waveform() const noexcept { return waveform_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool syntheticVisible_ = true;
    int waveform_ = 0;
};

class TrendPanel final : public QFrame {
public:
    explicit TrendPanel(QWidget* parent = nullptr);
    QCheckBox* syntheticToggle() const noexcept { return syntheticToggle_; }
    QComboBox* waveformSelector() const noexcept { return waveformSelector_; }
    TrendCanvas* canvas() const noexcept { return canvas_; }

private:
    QCheckBox* syntheticToggle_ = nullptr;
    QComboBox* waveformSelector_ = nullptr;
    TrendCanvas* canvas_ = nullptr;
};

class SignalTopology final : public QFrame {
public:
    explicit SignalTopology(bool reference, QWidget* parent = nullptr);
};

class EquipmentOverview final : public QWidget {
public:
    explicit EquipmentOverview(const QStringList& components,
                               QWidget* parent = nullptr);
    int equipmentCount() const noexcept { return count_; }
    QListWidget* equipmentList() const noexcept { return list_; }

private:
    int count_ = 0;
    QListWidget* list_ = nullptr;
    QLabel* selectedName_ = nullptr;
    QLabel* selectedDescription_ = nullptr;
};

class EventLogPanel final : public QWidget {
public:
    explicit EventLogPanel(QWidget* parent = nullptr);
    void appendUiEvent(const QString& text);
    int entryCount() const;

private:
    QListWidget* events_ = nullptr;
};

} // namespace miltech::ui
