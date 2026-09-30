#pragma once

#include <QWidget>
#include <QVector>

class QVBoxLayout;
class QFrame;

struct HomeWorkflowEntry
{
    QString id;
    QString title;
    bool available = true;
    QString unavailableReason;
};

class HomePage final : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget* parent = nullptr);
    void setProjectWorkflows(const QVector<HomeWorkflowEntry>& workflows);

signals:
    // Универсальный контур: свободная проверка не регистрируется в поставке.
    void genericCheckRequested();
    void workflowRequested(const QString& workflowId);
    void stationAdminRequested();

    // Специализированная поставка КТМА.
    void productionRequested();
    void tuRequested();
    void administrationRequested();

private:
    QVBoxLayout* rootLayout_ = nullptr;
    QFrame* genericCard_ = nullptr;
    QFrame* ktmaCard_ = nullptr;
    QFrame* projectWorkflowCard_ = nullptr;
};
