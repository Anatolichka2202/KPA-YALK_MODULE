#include "scenario_yaml_editor.h"

#include "orbita_stand/yaml_lite.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
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
        "находятся внутри плагинов. Опубликованная версия неизменяема — для правки создайте черновик.")
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
    draftButton_ = new QPushButton(QStringLiteral("Создать черновик"));
    draftButton_->setVisible(scenario_);
    saveButton_ = new QPushButton(QStringLiteral("Сохранить"));
    auto* closeButton = new QPushButton(QStringLiteral("Закрыть"));
    actions->addWidget(validateButton);
    actions->addWidget(draftButton_);
    actions->addStretch(1);
    actions->addWidget(saveButton_);
    actions->addWidget(closeButton);
    layout->addLayout(actions);
    connect(validateButton, &QPushButton::clicked, this, &ScenarioYamlEditor::validate);
    connect(draftButton_, &QPushButton::clicked, this, &ScenarioYamlEditor::createDraft);
    connect(saveButton_, &QPushButton::clicked, this, &ScenarioYamlEditor::save);
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
        published_ = false;
    }
    saveButton_->setEnabled(!published_);
    status_->setText(published_
        ? QStringLiteral("Опубликованная версия: сохранение поверх файла запрещено")
        : scenario_ ? QStringLiteral("Черновик можно редактировать и сохранять")
                    : QStringLiteral("Конфигурацию можно редактировать и сохранять"));
}

void ScenarioYamlEditor::validate()
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
    } catch (const std::exception& error) {
        status_->setText(QStringLiteral("Ошибка: %1").arg(QString::fromUtf8(error.what())));
        status_->setStyleSheet(QStringLiteral("color:#e1766d"));
    }
}

bool ScenarioYamlEditor::write(const QString& path, const QString& content)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(content.toUtf8());
    return file.commit();
}

void ScenarioYamlEditor::save()
{
    if (published_) return;
    validate();
    if (!write(path_, editor_->toPlainText())) {
        QMessageBox::critical(this, QStringLiteral("Сохранение"), QStringLiteral("Не удалось сохранить сценарий"));
        return;
    }
    status_->setText(QStringLiteral("Сохранено: %1").arg(path_));
}

void ScenarioYamlEditor::createDraft()
{
    QString content = editor_->toPlainText();
    content.replace(QStringLiteral("state: published"), QStringLiteral("state: draft"));
    const QFileInfo source(path_);
    const QString draftPath = source.dir().filePath(source.completeBaseName() + QStringLiteral("_draft.yaml"));
    if (!write(draftPath, content)) {
        QMessageBox::critical(this, QStringLiteral("Черновик"), QStringLiteral("Не удалось создать черновик"));
        return;
    }
    path_ = draftPath;
    published_ = false;
    saveButton_->setEnabled(true);
    editor_->setPlainText(content);
    status_->setText(QStringLiteral("Создан черновик: %1").arg(path_));
}
