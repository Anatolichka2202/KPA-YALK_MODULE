#pragma once

#include "orbita_stand/parameter_repository.h"
#include <QMap>
#include <QWidget>
#include <QFutureWatcher>
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
};

class MultiSourceMonitor final : public QWidget {
    Q_OBJECT
public:
    explicit MultiSourceMonitor(QWidget* parent = nullptr);
    ~MultiSourceMonitor() override;
    void setRepository(orbita::stand::ParameterRepository* repository);
    void setBindings(std::vector<MonitoredBinding> bindings);
    // Перед сменой профиля все опросы должны завершиться, иначе сессия
    // уничтожит источник, который ещё используется worker-потоком.
    void shutdown();
private:
    void poll();
    void display();
    QTableWidget* table_ = nullptr;
    QTimer* timer_ = nullptr;
    orbita::stand::ParameterRepository* repository_ = nullptr;
    std::vector<MonitoredBinding> bindings_;
    QMap<QString, QFutureWatcher<void>*> workers_;
};
