#pragma once

#include <QDialog>

class QLabel;
class QPushButton;
class QTextEdit;

class ScenarioYamlEditor final : public QDialog
{
    Q_OBJECT

public:
    explicit ScenarioYamlEditor(QString path, QWidget* parent = nullptr);

private:
    void load();
    void validate();
    void save();
    void createDraft();
    bool write(const QString& path, const QString& content);

    QString path_;
    QTextEdit* editor_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    bool published_ = false;
};
