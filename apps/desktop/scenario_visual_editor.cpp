#include "scenario_visual_editor.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <sstream>

namespace {
QString pathToText(const std::vector<int>& path)
{
    QStringList parts;
    for (const int part : path)
        parts << QString::number(part);
    return parts.join(QLatin1Char('/'));
}

std::vector<int> textToPath(const QString& text)
{
    std::vector<int> path;
    for (const auto& part : text.split(QLatin1Char('/'), Qt::SkipEmptyParts))
        path.push_back(part.toInt());
    return path;
}

std::string trimmedUtf8(const QString& value)
{
    return value.trimmed().toUtf8().toStdString();
}
}

ScenarioVisualEditor::ScenarioVisualEditor(QString path, QWidget* parent)
    : QDialog(parent)
    , path_(std::move(path))
{
    setWindowTitle(QStringLiteral("Визуальный редактор сценария"));
    resize(1260, 780);

    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(QStringLiteral(
        "Форма изменяет schema: 1 YAML текущего сценария. Процедура, ресурсы и аргументы "
        "сохраняются в том же формате, который читает сценарный движок. При сохранении "
        "визуальный редактор нормализует разметку YAML; комментарии исходного файла не сохраняются."), this);
    explanation->setWordWrap(true);
    explanation->setObjectName(QStringLiteral("scenarioVisualEditorExplanation"));
    layout->addWidget(explanation);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    tree_ = new QTreeWidget(splitter);
    tree_->setObjectName(QStringLiteral("scenarioVisualTree"));
    tree_->setHeaderLabels({QStringLiteral("Шаг / процедура"), QStringLiteral("ТУ")});
    tree_->setMinimumWidth(400);

    auto* formHost = new QWidget(splitter);
    auto* formLayout = new QVBoxLayout(formHost);
    auto* form = new QFormLayout;
    id_ = new QLineEdit(formHost);
    title_ = new QLineEdit(formHost);
    tu_ = new QLineEdit(formHost);
    procedure_ = new QLineEdit(formHost);
    id_->setObjectName(QStringLiteral("scenarioStepId"));
    title_->setObjectName(QStringLiteral("scenarioStepTitle"));
    tu_->setObjectName(QStringLiteral("scenarioStepTu"));
    procedure_->setObjectName(QStringLiteral("scenarioStepProcedure"));
    form->addRow(QStringLiteral("Идентификатор:"), id_);
    form->addRow(QStringLiteral("Название:"), title_);
    form->addRow(QStringLiteral("Требование ТУ:"), tu_);
    form->addRow(QStringLiteral("Процедура:"), procedure_);
    formLayout->addLayout(form);

    requires_ = new QPlainTextEdit(formHost);
    requires_->setObjectName(QStringLiteral("scenarioStepRequires"));
    requires_->setPlaceholderText(QStringLiteral("Одна capability в строке"));
    resources_ = new QPlainTextEdit(formHost);
    resources_->setObjectName(QStringLiteral("scenarioStepResources"));
    resources_->setPlaceholderText(QStringLiteral("resource | capability — одна строка на ресурс"));
    arguments_ = new QPlainTextEdit(formHost);
    arguments_->setObjectName(QStringLiteral("scenarioStepArguments"));
    arguments_->setPlaceholderText(QStringLiteral("имя = значение — одна строка на аргумент"));
    formLayout->addWidget(new QLabel(QStringLiteral("Требуемые capabilities:"), formHost));
    formLayout->addWidget(requires_, 1);
    formLayout->addWidget(new QLabel(QStringLiteral("Ресурсы поставки:"), formHost));
    formLayout->addWidget(resources_, 1);
    formLayout->addWidget(new QLabel(QStringLiteral("Аргументы процедуры:"), formHost));
    formLayout->addWidget(arguments_, 2);
    applyButton_ = new QPushButton(QStringLiteral("Применить к шагу"), formHost);
    applyButton_->setObjectName(QStringLiteral("applyScenarioVisualStep"));
    formLayout->addWidget(applyButton_);

    splitter->addWidget(tree_);
    splitter->addWidget(formHost);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    auto* actions = new QHBoxLayout;
    auto* addRoot = new QPushButton(QStringLiteral("Добавить этап"), this);
    auto* addChild = new QPushButton(QStringLiteral("Добавить подэтап"), this);
    auto* remove = new QPushButton(QStringLiteral("Удалить этап"), this);
    draftButton_ = new QPushButton(QStringLiteral("Создать черновик"), this);
    saveButton_ = new QPushButton(QStringLiteral("Сохранить YAML"), this);
    auto* close = new QPushButton(QStringLiteral("Закрыть"), this);
    addRoot->setObjectName(QStringLiteral("addScenarioRootStep"));
    addChild->setObjectName(QStringLiteral("addScenarioChildStep"));
    remove->setObjectName(QStringLiteral("removeScenarioStep"));
    draftButton_->setObjectName(QStringLiteral("createScenarioVisualDraft"));
    saveButton_->setObjectName(QStringLiteral("saveScenarioVisual"));
    actions->addWidget(addRoot);
    actions->addWidget(addChild);
    actions->addWidget(remove);
    actions->addStretch(1);
    actions->addWidget(draftButton_);
    actions->addWidget(saveButton_);
    actions->addWidget(close);
    layout->addLayout(actions);

    connect(tree_, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem* current, QTreeWidgetItem*) { selectStep(current); });
    connect(applyButton_, &QPushButton::clicked, this, &ScenarioVisualEditor::applyStep);
    connect(addRoot, &QPushButton::clicked, this, &ScenarioVisualEditor::addRootStep);
    connect(addChild, &QPushButton::clicked, this, &ScenarioVisualEditor::addChildStep);
    connect(remove, &QPushButton::clicked, this, &ScenarioVisualEditor::removeStep);
    connect(saveButton_, &QPushButton::clicked, this, &ScenarioVisualEditor::save);
    connect(draftButton_, &QPushButton::clicked, this, &ScenarioVisualEditor::createDraft);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    load();
}

void ScenarioVisualEditor::load()
{
    try {
        scenario_ = orbita::stand::loadScenarioYaml(path_.toStdString());
        published_ = scenario_.publicationState == orbita::stand::PublicationState::Published;
        setEditorEnabled(!published_);
        rebuildTree();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("Сценарий"),
            QStringLiteral("Не удалось загрузить YAML: %1").arg(QString::fromUtf8(error.what())));
        setEditorEnabled(false);
    }
}

void ScenarioVisualEditor::rebuildTree()
{
    tree_->clear();
    for (int index = 0; index < static_cast<int>(scenario_.steps.size()); ++index)
        appendNode(nullptr, scenario_.steps[static_cast<std::size_t>(index)], {index});
    tree_->expandAll();
    if (tree_->topLevelItemCount() > 0)
        tree_->setCurrentItem(tree_->topLevelItem(0));
    else
        clearNodeEditor();
}

void ScenarioVisualEditor::appendNode(QTreeWidgetItem* parent,
    const orbita::stand::ScenarioNode& node, const StepPath& path)
{
    auto* item = new QTreeWidgetItem({QString::fromStdString(node.title.empty() ? node.id : node.title),
                                      QString::fromStdString(node.tuRequirement)});
    item->setData(0, Qt::UserRole, pathToText(path));
    item->setToolTip(0, QStringLiteral("%1\n%2").arg(QString::fromStdString(node.id),
        QString::fromStdString(node.procedure)));
    if (parent)
        parent->addChild(item);
    else
        tree_->addTopLevelItem(item);
    for (int index = 0; index < static_cast<int>(node.children.size()); ++index) {
        StepPath childPath = path;
        childPath.push_back(index);
        appendNode(item, node.children[static_cast<std::size_t>(index)], childPath);
    }
}

void ScenarioVisualEditor::setEditorEnabled(bool enabled)
{
    id_->setEnabled(enabled);
    title_->setEnabled(enabled);
    tu_->setEnabled(enabled);
    procedure_->setEnabled(enabled);
    requires_->setEnabled(enabled);
    resources_->setEnabled(enabled);
    arguments_->setEnabled(enabled);
    applyButton_->setEnabled(enabled);
    saveButton_->setEnabled(enabled);
    draftButton_->setVisible(published_);
}

ScenarioVisualEditor::StepPath ScenarioVisualEditor::selectedPath() const
{
    const auto* item = tree_->currentItem();
    return item ? textToPath(item->data(0, Qt::UserRole).toString()) : StepPath{};
}

orbita::stand::ScenarioNode* ScenarioVisualEditor::findNode(const StepPath& path)
{
    if (path.empty()) return nullptr;
    std::vector<orbita::stand::ScenarioNode>* nodes = &scenario_.steps;
    orbita::stand::ScenarioNode* node = nullptr;
    for (const int index : path) {
        if (index < 0 || index >= static_cast<int>(nodes->size())) return nullptr;
        node = &(*nodes)[static_cast<std::size_t>(index)];
        nodes = &node->children;
    }
    return node;
}

std::vector<orbita::stand::ScenarioNode>* ScenarioVisualEditor::siblingsFor(const StepPath& path)
{
    if (path.empty()) return nullptr;
    if (path.size() == 1) return &scenario_.steps;
    StepPath parentPath = path;
    parentPath.pop_back();
    auto* parent = findNode(parentPath);
    return parent ? &parent->children : nullptr;
}

void ScenarioVisualEditor::selectStep(QTreeWidgetItem* item)
{
    if (!item) {
        clearNodeEditor();
        return;
    }
    if (auto* node = findNode(textToPath(item->data(0, Qt::UserRole).toString())))
        showNode(*node);
}

void ScenarioVisualEditor::showNode(const orbita::stand::ScenarioNode& node)
{
    id_->setText(QString::fromStdString(node.id));
    title_->setText(QString::fromStdString(node.title));
    tu_->setText(QString::fromStdString(node.tuRequirement));
    procedure_->setText(QString::fromStdString(node.procedure));
    QStringList required;
    for (const auto& capability : node.requiredCapabilities)
        required << QString::fromStdString(capability);
    requires_->setPlainText(required.join(QLatin1Char('\n')));
    QStringList resourceLines;
    for (const auto& resource : node.requiredResources)
        resourceLines << QStringLiteral("%1 | %2").arg(QString::fromStdString(resource.resource),
            QString::fromStdString(resource.capability));
    resources_->setPlainText(resourceLines.join(QLatin1Char('\n')));
    QStringList argumentLines;
    for (const auto& [key, value] : node.arguments)
        argumentLines << QStringLiteral("%1 = %2").arg(QString::fromStdString(key),
            QString::fromStdString(value));
    arguments_->setPlainText(argumentLines.join(QLatin1Char('\n')));
}

void ScenarioVisualEditor::clearNodeEditor()
{
    for (auto* edit : {id_, title_, tu_, procedure_}) edit->clear();
    for (auto* edit : {requires_, resources_, arguments_}) edit->clear();
}

void ScenarioVisualEditor::applyStep()
{
    auto* node = findNode(selectedPath());
    if (!node) return;
    const auto id = trimmedUtf8(id_->text());
    if (id.empty()) {
        QMessageBox::warning(this, QStringLiteral("Шаг сценария"), QStringLiteral("Нужен идентификатор шага."));
        return;
    }
    node->id = id;
    node->title = trimmedUtf8(title_->text());
    node->tuRequirement = trimmedUtf8(tu_->text());
    node->procedure = trimmedUtf8(procedure_->text());
    node->requiredCapabilities.clear();
    for (const auto& capability : lines(requires_->toPlainText()))
        node->requiredCapabilities.insert(capability);
    node->requiredResources = resources(resources_->toPlainText());
    node->arguments = arguments(arguments_->toPlainText());
    rebuildTree();
}

void ScenarioVisualEditor::addRootStep()
{
    const int next = static_cast<int>(scenario_.steps.size()) + 1;
    scenario_.steps.push_back({"step_" + std::to_string(next), "Новый этап", "", "", {}, {}, {}});
    rebuildTree();
    tree_->setCurrentItem(tree_->topLevelItem(tree_->topLevelItemCount() - 1));
}

void ScenarioVisualEditor::addChildStep()
{
    auto* node = findNode(selectedPath());
    if (!node) {
        addRootStep();
        return;
    }
    const int next = static_cast<int>(node->children.size()) + 1;
    node->children.push_back({"child_" + std::to_string(next), "Новый подэтап", "", "", {}, {}, {}});
    rebuildTree();
}

void ScenarioVisualEditor::removeStep()
{
    const StepPath path = selectedPath();
    auto* siblings = siblingsFor(path);
    if (!siblings) return;
    const int index = path.back();
    if (index < 0 || index >= static_cast<int>(siblings->size())) return;
    siblings->erase(siblings->begin() + index);
    rebuildTree();
}

QString ScenarioVisualEditor::quote(const std::string& value)
{
    QString result = QString::fromUtf8(value.c_str());
    result.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    result.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"") + result + QStringLiteral("\"");
}

QString ScenarioVisualEditor::serialize() const
{
    QString output;
    output += QStringLiteral("schema: 1\n");
    output += QStringLiteral("id: %1\n").arg(quote(scenario_.id));
    output += QStringLiteral("title: %1\n").arg(quote(scenario_.title));
    output += QStringLiteral("version: %1\n").arg(quote(scenario_.version));
    if (!scenario_.catalogVersion.empty())
        output += QStringLiteral("catalog_version: %1\n").arg(quote(scenario_.catalogVersion));
    if (!scenario_.objectType.empty())
        output += QStringLiteral("object_type: %1\n").arg(quote(scenario_.objectType));
    output += QStringLiteral("state: %1\nsteps:\n").arg(
        scenario_.publicationState == orbita::stand::PublicationState::Published ? QStringLiteral("published") : QStringLiteral("draft"));
    for (const auto& step : scenario_.steps)
        appendNodeYaml(output, step, 2);
    return output;
}

void ScenarioVisualEditor::appendNodeYaml(QString& output, const orbita::stand::ScenarioNode& node, int indent)
{
    const QString prefix(indent, QLatin1Char(' '));
    output += prefix + QStringLiteral("- id: %1\n").arg(quote(node.id));
    output += prefix + QStringLiteral("  title: %1\n").arg(quote(node.title));
    if (!node.tuRequirement.empty()) output += prefix + QStringLiteral("  tu: %1\n").arg(quote(node.tuRequirement));
    if (!node.procedure.empty()) output += prefix + QStringLiteral("  procedure: %1\n").arg(quote(node.procedure));
    if (!node.requiredCapabilities.empty()) {
        output += prefix + QStringLiteral("  requires:\n");
        for (const auto& capability : node.requiredCapabilities)
            output += prefix + QStringLiteral("    - %1\n").arg(quote(capability));
    }
    if (!node.requiredResources.empty()) {
        output += prefix + QStringLiteral("  resources:\n");
        for (const auto& resource : node.requiredResources) {
            output += prefix + QStringLiteral("    - resource: %1\n").arg(quote(resource.resource));
            output += prefix + QStringLiteral("      capability: %1\n").arg(quote(resource.capability));
        }
    }
    if (!node.arguments.empty()) {
        output += prefix + QStringLiteral("  args:\n");
        for (const auto& [key, value] : node.arguments)
            output += prefix + QStringLiteral("    %1: %2\n").arg(quote(key), quote(value));
        if (node.policy.technicalRetries > 0) {
            output += prefix + QStringLiteral("    technical_retries: %1\n")
                .arg(node.policy.technicalRetries);
        }
    } else if (node.policy.technicalRetries > 0) {
        output += prefix + QStringLiteral("  args:\n    technical_retries: %1\n")
            .arg(node.policy.technicalRetries);
    }
    if (!node.children.empty()) {
        output += prefix + QStringLiteral("  steps:\n");
        for (const auto& child : node.children)
            appendNodeYaml(output, child, indent + 4);
    }
}

std::vector<std::string> ScenarioVisualEditor::lines(const QString& text)
{
    std::vector<std::string> result;
    for (const auto& line : text.split(QLatin1Char('\n'))) {
        const auto value = trimmedUtf8(line);
        if (!value.empty()) result.push_back(value);
    }
    return result;
}

std::map<std::string, std::string> ScenarioVisualEditor::arguments(const QString& text)
{
    std::map<std::string, std::string> result;
    for (const auto& line : text.split(QLatin1Char('\n'))) {
        const int separator = line.indexOf(QLatin1Char('='));
        if (separator < 1) continue;
        const auto key = trimmedUtf8(line.left(separator));
        if (!key.empty()) result[key] = trimmedUtf8(line.mid(separator + 1));
    }
    return result;
}

std::vector<orbita::stand::ResourceRequirement> ScenarioVisualEditor::resources(const QString& text)
{
    std::vector<orbita::stand::ResourceRequirement> result;
    for (const auto& line : text.split(QLatin1Char('\n'))) {
        const int separator = line.indexOf(QLatin1Char('|'));
        if (separator < 1) continue;
        const auto resource = trimmedUtf8(line.left(separator));
        const auto capability = trimmedUtf8(line.mid(separator + 1));
        if (!resource.empty() && !capability.empty()) result.push_back({resource, capability});
    }
    return result;
}

bool ScenarioVisualEditor::write(const QString& path, const QString& content)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(content.toUtf8());
    return file.commit();
}

void ScenarioVisualEditor::save()
{
    if (published_) return;
    applyStep();
    if (!write(path_, serialize())) {
        QMessageBox::critical(this, QStringLiteral("Сохранение"), QStringLiteral("Не удалось сохранить YAML."));
        return;
    }
    try {
        (void)orbita::stand::loadScenarioYaml(path_.toStdString());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("YAML"),
            QStringLiteral("Сохранённый YAML не прошёл загрузку: %1").arg(QString::fromUtf8(error.what())));
        return;
    }
}

void ScenarioVisualEditor::createDraft()
{
    const QFileInfo source(path_);
    const QString draftPath = source.dir().filePath(source.completeBaseName() + QStringLiteral("_draft.yaml"));
    scenario_.publicationState = orbita::stand::PublicationState::Draft;
    if (!write(draftPath, serialize())) {
        QMessageBox::critical(this, QStringLiteral("Черновик"), QStringLiteral("Не удалось создать черновик YAML."));
        return;
    }
    path_ = draftPath;
    published_ = false;
    setEditorEnabled(true);
}
