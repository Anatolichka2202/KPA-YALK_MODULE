#include "scenario_yaml_editor.h"
#include "scenario_visual_editor.h"

#include "orbita_stand/yaml_lite.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextEdit>
#include <QVBoxLayout>

#include <utility>

ScenarioYamlEditor::ScenarioYamlEditor(QString path, QWidget* parent)
    : QDialog(parent), path_(std::move(path))
{
    scenario_ = QFileInfo(path_).dir().dirName() == QStringLiteral("scenarios");
    setWindowTitle(scenario_ ? QStringLiteral("Редактор сценария YAML")
                             : QStringLiteral("Редактор конфигурации YAML"));
    resize(900, 700);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(scenario_ ? QStringLiteral(
        "Сценарий содержит типизированные процедуры. Низкоуровневые HTTP/SCPI/UDP-команды "
        "находятся внутри плагинов. Порядок и параметры этапов можно изменить "
        "и сохранить в том же YAML, который читает сценарный движок.")
        : QStringLiteral("Инженерная конфигурация каталога или профиля стенда. "
                         "Перед сохранением проверьте YAML; изменения применятся после повторной проверки оборудования."));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    editor_ = new QTextEdit;
    editor_->setFontFamily(QStringLiteral("Consolas"));
    editor_->setTabStopDistance(32);
    layout->addWidget(editor_, 1);
    status_ = new QLabel;
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    auto* actions = new QHBoxLayout;
    auto* validateButton = new QPushButton(QStringLiteral("Проверить YAML"));
    auto* visualButton = new QPushButton(QStringLiteral("Визуальный редактор"));
    visualButton->setObjectName(QStringLiteral("openScenarioVisualEditor"));
    visualButton->setVisible(scenario_);
    draftButton_ = new QPushButton(QStringLiteral("Создать черновик"));
    draftButton_->setObjectName(QStringLiteral("createScenarioDraft"));
    draftButton_->setVisible(false);
    saveButton_ = new QPushButton(QStringLiteral("Сохранить"));
    saveButton_->setObjectName(QStringLiteral("saveScenarioConfig"));
    auto* closeButton = new QPushButton(QStringLiteral("Закрыть"));
    actions->addWidget(validateButton);
    actions->addWidget(visualButton);
    actions->addWidget(draftButton_);
    actions->addStretch(1);
    actions->addWidget(saveButton_);
    actions->addWidget(closeButton);
    layout->addLayout(actions);
    connect(validateButton, &QPushButton::clicked, this, &ScenarioYamlEditor::validate);
    connect(visualButton, &QPushButton::clicked, this, [this]() {
        ScenarioVisualEditor visual(path_, this);
        visual.exec();
        load();
    });
    connect(saveButton_, &QPushButton::clicked, this, &ScenarioYamlEditor::save);
    connect(draftButton_, &QPushButton::clicked, this, &ScenarioYamlEditor::createDraft);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    load();
}
void ScenarioYamlEditor::load()
{
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        status_->setText(QStringLiteral("Не удалось открыть %1: %2").arg(path_, file.errorString()));
        saveButton_->setEnabled(false);
        return;
    }
    editor_->setPlainText(QString::fromUtf8(file.readAll()));
    try {
        const auto root = orbita::stand::yaml::parse(editor_->toPlainText().toStdString());
        published_ = scenario_ && root.value("state") == "published";
    } catch (...) {
        // Некорректный исходный сценарий нельзя безопасно считать черновиком.
        published_ = scenario_;
    }
    saveButton_->setEnabled(!published_);
    draftButton_->setVisible(published_);
    status_->setText(published_ ? QStringLiteral("Исходный сценарий защищён: создайте черновик")
                              : scenario_ ? QStringLiteral("Сценарий можно редактировать и сохранять")
                              : QStringLiteral("Конфигурацию можно редактировать и сохранять"));
}

bool ScenarioYamlEditor::validate()
{
    try {
        const auto root = orbita::stand::yaml::parse(editor_->toPlainText().toStdString());
        if (!root.isMap() || root.value("schema") != "1")
            throw std::runtime_error("нужны корневой объект и schema: 1");
        if (scenario_) {
            if (root.value("id").empty() || root.value("version").empty() || !root.find("steps"))
                throw std::runtime_error("для сценария нужны id, version и steps");
        } else if (QFileInfo(path_).dir().dirName() == QStringLiteral("profiles")) {
            if (root.value("id").empty() || root.value("version").empty()
                || !root.find("devices") || !root.find("routes"))
                throw std::runtime_error("для профиля нужны id, version, routes и devices");
        } else if (root.value("version").empty() || !root.find("bindings")
                   || !root.find("parameter_groups")) {
            throw std::runtime_error("для каталога нужны version, parameter_groups и bindings");
        }
        status_->setText(QStringLiteral("YAML корректен; обязательные разделы присутствуют"));
        status_->setStyleSheet(QStringLiteral("color:#70d79b"));
        return true;
    } catch (const std::exception& error) {
        status_->setText(QStringLiteral("Ошибка: %1").arg(QString::fromUtf8(error.what())));
        status_->setStyleSheet(QStringLiteral("color:#e1766d"));
        return false;
    }
}

bool ScenarioYamlEditor::write(const QString& path, const QString& content)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const auto bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size()) return false;
    return file.commit();
}

void ScenarioYamlEditor::save()
{
    if (published_) return;
    // Ошибка проверки оставляет исходный файл и введённый черновик без изменений.
    if (!validate()) return;
    if (!write(path_, editor_->toPlainText())) {
        QMessageBox::critical(this, QStringLiteral("Сохранение"), QStringLiteral("Не удалось сохранить сценарий"));
        return;
    }
    status_->setText(QStringLiteral("Сохранено: %1").arg(path_));
}

void ScenarioYamlEditor::createDraft()
{
    if (!published_ || !validate()) return;
    QString content = editor_->toPlainText();
    const QRegularExpression state(QStringLiteral("(?m)^state\\s*:\\s*['\"]?published['\"]?\\s*(?:#.*)?$"));
    if (!state.match(content).hasMatch()) {
        status_->setText(QStringLiteral("Не удалось определить состояние опубликованного сценария"));
        return;
    }
    content.replace(state, QStringLiteral("state: draft"));
    const QFileInfo source(path_);
    const QString draftPath = source.dir().filePath(source.completeBaseName() + QStringLiteral("_draft.yaml"));
    // NewOnly исключает перезапись ранее созданного оператором черновика.
    QFile draft(draftPath);
    if (!draft.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        status_->setText(QStringLiteral("Не удалось создать черновик: %1").arg(draft.errorString()));
        return;
    }
    const auto bytes = content.toUtf8();
    if (draft.write(bytes) != bytes.size() || !draft.flush()) {
        status_->setText(QStringLiteral("Не удалось полностью записать черновик: %1").arg(draft.errorString()));
        draft.close();
        QFile::remove(draftPath);
        return;
    }
    draft.close();
    path_ = draftPath;
    published_ = false;
    editor_->setPlainText(content);
    saveButton_->setEnabled(true);
    draftButton_->setVisible(false);
    status_->setText(QStringLiteral("Создан черновик: %1").arg(path_));
}
