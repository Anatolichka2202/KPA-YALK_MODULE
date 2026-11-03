#pragma once

#include <QDialog>
#include <vector>
#include <string>

class QLabel;
class QPushButton;
class QTextEdit;

class ScenarioYamlEditor final : public QDialog
{
    Q_OBJECT

public:
    explicit ScenarioYamlEditor(QString path, QWidget* parent = nullptr);
    void setAvailableProcedures(std::vector<std::string> procedures);

private:
    void load();
    bool validate();
    void save();
    void createDraft();
    bool write(const QString& path, const QString& content);

    QString path_;
    QTextEdit* editor_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    bool scenario_ = false;
    bool published_ = false;
    QPushButton* draftButton_ = nullptr;
    std::vector<std::string> availableProcedures_;
};
