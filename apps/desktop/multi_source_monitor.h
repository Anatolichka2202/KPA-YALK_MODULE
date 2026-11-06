#pragma once

#include "orbita_stand/parameter_repository.h"
#include <QWidget>
#include "qcustomplot.h"
#include <optional>
#include <vector>

class QTableWidget;
class QTimer;
class QCheckBox;

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
    void setBindings(std::vector<MonitoredBinding> bindings);
    const std::vector<MonitoredBinding>& bindings() const { return bindings_; }
private:
    void display();
    void updateGraph(int row, const std::optional<orbita::stand::ParameterSample>& sample);
    void updateVisibility();
    QTableWidget* table_ = nullptr;
    QTimer* timer_ = nullptr;
    QCustomPlot* plot_ = nullptr;
    QCheckBox* followTime_ = nullptr;
    QCheckBox* automaticScale_ = nullptr;
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
};
