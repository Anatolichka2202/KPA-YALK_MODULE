#pragma once

#include "orbita_stand/parameter_repository.h"
#include <QWidget>
#include <vector>

class QTableWidget;
class QTimer;

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
    QTableWidget* table_ = nullptr;
    QTimer* timer_ = nullptr;
    orbita::stand::ParameterRepository* repository_ = nullptr;
    std::vector<MonitoredBinding> bindings_;
};
