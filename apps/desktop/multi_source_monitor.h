#pragma once

#include "orbita_stand/parameter_repository.h"
#include <QWidget>
#include "qcustomplot.h"
#include <optional>
#include <vector>

class QTableWidget;
class QTimer;
class QCheckBox;
class QComboBox;
class QSplitter;

// Параметр задаётся парой идентификаторов: одинаковые адреса у разных
// источников не смешиваются и не требуют общего пространства имён приборов.
struct MonitoredBinding {
    QString sourceId;
    QString sourceTitle;
    QString parameterKey;
    QString parameterTitle;
    QString unavailableReason;
    QString unit;
};

class MultiSourceMonitor final : public QWidget {
    Q_OBJECT
public:
    explicit MultiSourceMonitor(QWidget* parent = nullptr);
    ~MultiSourceMonitor() override;
    void setRepository(orbita::stand::ParameterRepository* repository);
    void setPageScope(const QString& scope);
    void setBindings(std::vector<MonitoredBinding> bindings);
    const std::vector<MonitoredBinding>& bindings() const { return availableBindings_; }
private:
    void rebuildBindings(std::vector<MonitoredBinding> bindings);
    void refreshPages(const QString& selectedId = {});
    void applyPage(bool resetSelection);
    void editPage(bool create);
    void display();
    void updateGraph(int row, const std::optional<orbita::stand::ParameterSample>& sample);
    void updateVisibility();
    QTableWidget* table_ = nullptr;
    QTimer* timer_ = nullptr;
    QCustomPlot* plot_ = nullptr;
    QCheckBox* followTime_ = nullptr;
    QCheckBox* automaticScale_ = nullptr;
    QComboBox* pages_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QWidget* graphPanel_ = nullptr;
    struct Series {
        QCPGraph* graph = nullptr;
        QCPAxis* axis = nullptr;
        std::uint64_t lastSequence = 0;
        std::chrono::system_clock::time_point lastTimestamp{};
        double gapTime = 0.0;
        bool hasSample = false;
        bool gap = false;
    };
    std::vector<Series> series_;
    orbita::stand::ParameterRepository* repository_ = nullptr;
    std::vector<MonitoredBinding> bindings_;
    std::vector<MonitoredBinding> availableBindings_;
    QString pageScope_;
};
