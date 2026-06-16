#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QPushButton>
#include "metadata_service.h"

class ParameterBrowser : public QWidget {
    Q_OBJECT
public:
    explicit ParameterBrowser(MetadataService* db, QWidget *parent = nullptr);
    void rebuildTree();
signals:
    void parametersSelected(const QList<QString>& normalizedAddresses, const QList<QString>& names);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onAddSelected();

private:

    MetadataService* db_;
    QTreeWidget* tree_;
    QPushButton* addButton_;
};
