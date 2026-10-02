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
    QString kind;
};

class HomePage final : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget* parent = nullptr);
    void setProjectWorkflows(const QVector<HomeWorkflowEntry>& workflows);

signals:
    // Универсальный контур не входит в маршруты КТМА и не запускает YAML-сценарии.
    void genericCheckRequested();
    void freeWorkspaceRequested();
    void workflowRequested(const QString& workflowId);
    void stationAdminRequested();

    // Специализированная поставка КТМА.
    void productionRequested();
    void tuRequested();
    void administrationRequested();

private:
    void showKtmaMenu(bool visible);
    QVBoxLayout* rootLayout_ = nullptr;
    QFrame* genericCard_ = nullptr;
    QFrame* ktmaCard_ = nullptr;
    QFrame* projectWorkflowCard_ = nullptr;
    QFrame* stationAdminCard_ = nullptr;
};
