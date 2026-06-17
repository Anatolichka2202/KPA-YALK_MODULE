#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLabel>
#include "metadata_service.h"

class ParameterBrowser : public QWidget {
    Q_OBJECT
public:
    explicit ParameterBrowser(MetadataService* db, QWidget *parent = nullptr);
    void rebuildTree();
signals:
    void parametersSelected(const QList<QString>& normalizedAddresses, const QList<QString>& names);
    void overrideTolerance(QString address, double lo, double nominal, double hi);
    void toleranceSavedToDb(QString address);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onAddSelected();
    void onItemSelectionChanged();
    void onSaveToSession();
    void onSaveToDb();

private:
    void updateEditorPanel();

    MetadataService* db_;
    QTreeWidget* tree_;
    QPushButton* addButton_;

    // Панель редактора допусков
    QLabel* addressLabel_;
    QDoubleSpinBox* loSpinBox_;
    QDoubleSpinBox* nominalSpinBox_;
    QDoubleSpinBox* hiSpinBox_;
    QPushButton* saveSessionBtn_;
    QPushButton* saveDbBtn_;
    QString currentAddress_;
};
