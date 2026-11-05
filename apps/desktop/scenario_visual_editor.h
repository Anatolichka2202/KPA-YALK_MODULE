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
#include <QStringList>

#include <vector>
#include <functional>

class QLabel;
class QLineEdit;
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class ScenarioVisualEditor final : public QDialog {
    Q_OBJECT

public:
    explicit ScenarioVisualEditor(QString path, QWidget* parent = nullptr);
    void setAvailableProcedures(const std::vector<std::string>& procedures);
    QString currentPath() const { return path_; }
    void setScenarioStorage(QString writableDirectory, QStringList versionDirectories);
    void setScenarioValidator(std::function<std::vector<std::string>(
        const orbita::stand::ScenarioDefinition&)> validator);

public slots:
    void done(int result) override;

private slots:
    void selectStep(QTreeWidgetItem* item);
    void applyStep();
    void addRootStep();
    void addChildStep();
    void removeStep();
    void moveStepUp();
    void moveStepDown();
    void save();
    void createDraft();
    void createRunnableVersion();
    void configureTelemetryHealth();

private:
    using StepPath = std::vector<int>;

    void load();
    void setDirty(bool dirty);
    void rebuildTree();
    void appendNode(QTreeWidgetItem* parent, const orbita::stand::ScenarioNode& node,
                    const StepPath& path);
    void setEditorEnabled(bool enabled);
    StepPath selectedPath() const;
    void selectPath(const StepPath& path);
    orbita::stand::ScenarioNode* findNode(const StepPath& path);
    std::vector<orbita::stand::ScenarioNode>* siblingsFor(const StepPath& path);
    void showNode(const orbita::stand::ScenarioNode& node);
    void clearNodeEditor();
    bool commitEditorToSelectedNode();
    bool commitEditorToNode(const StepPath& path);
    bool configureTelemetryHealthForNode(const orbita::stand::ScenarioNode* node);
    QString serialize() const;
    static QString quote(const std::string& value);
    static std::vector<std::string> lines(const QString& text);
    static std::map<std::string, std::string> arguments(const QString& text);
    static std::vector<orbita::stand::ResourceRequirement> resources(const QString& text);
    static void appendNodeYaml(QString& output, const orbita::stand::ScenarioNode& node,
                               int indent);
    bool write(const QString& path, const QString& content);

    QString path_;
    QString writableDirectory_;
    bool storageConfigured_ = false;
    QStringList versionDirectories_;
    orbita::stand::ScenarioDefinition scenario_;
    bool published_ = false;
    bool dirty_ = false;
    bool loaded_ = false;
    bool updatingFields_ = false;
    QLabel* draftStatus_ = nullptr;
    bool healthConfigured_ = false;
    QString healthOriginalArguments_;
    QString healthOriginalRequires_;
    QString healthOriginalResources_;

    QTreeWidget* tree_ = nullptr;
    QLineEdit* id_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* tu_ = nullptr;
    QLineEdit* procedure_ = nullptr;
    QComboBox* procedureChoice_ = nullptr;
    QPlainTextEdit* requires_ = nullptr;
    QPlainTextEdit* resources_ = nullptr;
    QPlainTextEdit* arguments_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* versionButton_ = nullptr;
    std::function<std::vector<std::string>(const orbita::stand::ScenarioDefinition&)> validator_;
    QPushButton* draftButton_ = nullptr;
    QPushButton* telemetrySettings_ = nullptr;
};
