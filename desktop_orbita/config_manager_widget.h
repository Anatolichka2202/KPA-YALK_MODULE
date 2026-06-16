#pragma once

#include <QWidget>
#include <QString>

class QListWidget;
class QTableWidget;
class MetadataService;

class ConfigManagerWidget : public QWidget {
    Q_OBJECT
public:
    explicit ConfigManagerWidget(MetadataService* db, QWidget* parent = nullptr);

    void refreshFileList();
    void updateMetadata();  // перечитать данные из БД и обновить preview

signals:
    void applyConfigRequested(const QString& fileName);
    void refreshMetadataRequested();

private slots:
    void onApplyClicked();

private:
    void loadFilePreview(const QString& fileName);

    QListWidget*     fileList_;
    QTableWidget*    previewTable_;
    MetadataService* db_;
};
