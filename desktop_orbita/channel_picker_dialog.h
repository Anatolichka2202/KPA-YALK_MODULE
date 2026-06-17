#pragma once

#include <QDialog>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
#include <vector>

#include "orbita.h"
#include "metadata_service.h"

class ChannelPickerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChannelPickerDialog(const std::vector<orbita::ChannelSpec>& specs,
                                  MetadataService* db,
                                  QWidget* parent = nullptr);

    QString selectedAddress() const;
    QString selectedName() const;

private slots:
    void onSearchChanged(const QString& text);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemClicked(QTreeWidgetItem* item, int column);

private:
    void buildTree();
    void updateDetail(QTreeWidgetItem* item);

    const std::vector<orbita::ChannelSpec>& specs_;
    MetadataService* db_;

    QLineEdit*   search_;
    QTreeWidget* tree_;
    QLabel*      detailName_;
    QLabel*      detailAddr_;
    QLabel*      detailCat_;

    QString selectedAddress_;
    QString selectedName_;

    // UserRole data stored on leaf items
    static constexpr int RoleAddress = Qt::UserRole;
    static constexpr int RoleName    = Qt::UserRole + 1;
    static constexpr int RoleLeaf    = Qt::UserRole + 2;
};
