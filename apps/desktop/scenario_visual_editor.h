#pragma once

/**
 * @file scenario_visual_editor.h
 * @brief Визуальный редактор schema: 1 сценария без собственного формата.
 *
 * Редактор читает и записывает тот же YAML, который загружает ScenarioEngine.
 * Он не исполняет процедуры и не содержит их каталога: процедура остаётся
 * строковым идентификатором зарегистрированного backend-контракта.
 */

#include "orbita_stand/config.h"

#include <QDialog>

#include <vector>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class ScenarioVisualEditor final : public QDialog {
    Q_OBJECT

public:
    explicit ScenarioVisualEditor(QString path, QWidget* parent = nullptr);

private slots:
    void selectStep(QTreeWidgetItem* item);
    void applyStep();
    void addRootStep();
    void addChildStep();
    void removeStep();
    void save();
    void createDraft();

private:
    using StepPath = std::vector<int>;

    void load();
    void rebuildTree();
    void appendNode(QTreeWidgetItem* parent, const orbita::stand::ScenarioNode& node,
                    const StepPath& path);
    void setEditorEnabled(bool enabled);
    StepPath selectedPath() const;
    orbita::stand::ScenarioNode* findNode(const StepPath& path);
    std::vector<orbita::stand::ScenarioNode>* siblingsFor(const StepPath& path);
    void showNode(const orbita::stand::ScenarioNode& node);
    void clearNodeEditor();
    QString serialize() const;
    static QString quote(const std::string& value);
    static std::vector<std::string> lines(const QString& text);
    static std::map<std::string, std::string> arguments(const QString& text);
    static std::vector<orbita::stand::ResourceRequirement> resources(const QString& text);
    static void appendNodeYaml(QString& output, const orbita::stand::ScenarioNode& node,
                               int indent);
    bool write(const QString& path, const QString& content);

    QString path_;
    orbita::stand::ScenarioDefinition scenario_;
    bool published_ = false;

    QTreeWidget* tree_ = nullptr;
    QLineEdit* id_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* tu_ = nullptr;
    QLineEdit* procedure_ = nullptr;
    QPlainTextEdit* requires_ = nullptr;
    QPlainTextEdit* resources_ = nullptr;
    QPlainTextEdit* arguments_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* draftButton_ = nullptr;
};
