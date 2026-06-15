#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QPushButton>
#include "parameter_database.h"

class ParameterBrowser : public QWidget {
    Q_OBJECT
public:
    explicit ParameterBrowser(ParameterDatabase* db, QWidget *parent = nullptr);
    void rebuildTree();
signals:
    void parametersSelected(const QList<QString>& normalizedAddresses, const QList<QString>& names);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onAddSelected();

private:

    ParameterDatabase* db_;
    QTreeWidget* tree_;
    QPushButton* addButton_;
};
