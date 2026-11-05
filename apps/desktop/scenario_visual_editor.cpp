#include "scenario_visual_editor.h"

#include <QDialogButtonBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <set>
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
        "сохраняются в том же формате, который читает сценарный движок. Порядок этапов "
        "можно менять кнопками вверх/вниз. При сохранении визуальный редактор "
        "нормализует разметку YAML; комментарии исходного файла не сохраняются."), this);
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
    procedureChoice_ = new QComboBox(formHost);
    procedureChoice_->setObjectName(QStringLiteral("availableScenarioProcedures"));
    procedureChoice_->addItem(QStringLiteral("Выберите процедуру…"), QString());
    procedureChoice_->setToolTip(QStringLiteral("Регистрация процедуры не означает готовность оборудования"));
    form->addRow(QStringLiteral("Процедуры станции:"), procedureChoice_);
    connect(procedureChoice_, &QComboBox::activated, this, [this](int index) {
        const auto id = procedureChoice_->itemData(index).toString();
        if (!id.isEmpty()) procedure_->setText(id);
    });
    telemetrySettings_ = new QPushButton(QStringLiteral("Настроить проверку потока…"), formHost);
    telemetrySettings_->setObjectName(QStringLiteral("configureTelemetryHealth"));
    form->addRow(QString(), telemetrySettings_);
    connect(telemetrySettings_, &QPushButton::clicked, this, &ScenarioVisualEditor::configureTelemetryHealth);
    connect(procedure_, &QLineEdit::textChanged, this, [this](const QString& procedure) {
        healthConfigured_ = false;
        telemetrySettings_->setEnabled(!published_ && procedure == QStringLiteral("telemetry.health"));
    });
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
    connect(arguments_, &QPlainTextEdit::textChanged, this, [this] {
        healthConfigured_ = false;
    });
    const auto onRequirementsChanged = [this] {
        healthConfigured_ = false;
    };
    connect(requires_, &QPlainTextEdit::textChanged, this, onRequirementsChanged);
    connect(resources_, &QPlainTextEdit::textChanged, this, onRequirementsChanged);
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
    auto* moveUp = new QPushButton(QStringLiteral("Вверх"), this);
    auto* moveDown = new QPushButton(QStringLiteral("Вниз"), this);
    draftButton_ = new QPushButton(QStringLiteral("Создать черновик"), this);
    draftButton_->setObjectName(QStringLiteral("createScenarioVisualDraft"));
    saveButton_ = new QPushButton(QStringLiteral("Сохранить YAML"), this);
    auto* close = new QPushButton(QStringLiteral("Закрыть"), this);
    addRoot->setObjectName(QStringLiteral("addScenarioRootStep"));
    addChild->setObjectName(QStringLiteral("addScenarioChildStep"));
    remove->setObjectName(QStringLiteral("removeScenarioStep"));
    moveUp->setObjectName(QStringLiteral("moveScenarioStepUp"));
    moveDown->setObjectName(QStringLiteral("moveScenarioStepDown"));
    saveButton_->setObjectName(QStringLiteral("saveScenarioVisual"));
    actions->addWidget(addRoot);
    actions->addWidget(addChild);
    actions->addWidget(remove);
    actions->addWidget(moveUp);
    actions->addWidget(moveDown);
    actions->addWidget(draftButton_);
    actions->addStretch(1);
    actions->addWidget(saveButton_);
    actions->addWidget(close);
    layout->addLayout(actions);

    connect(tree_, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem* current, QTreeWidgetItem* previous) {
            // Сначала переносим поля предыдущего шага в черновик, иначе
            // показ нового шага уничтожит ещё не применённый ввод оператора.
            if (previous && !published_
                && !commitEditorToNode(textToPath(previous->data(0, Qt::UserRole).toString()))) {
                const QSignalBlocker block(tree_);
                tree_->setCurrentItem(previous);
                return;
            }
            if (previous && !published_) {
                if (const auto* node = findNode(textToPath(previous->data(0, Qt::UserRole).toString()))) {
                    previous->setText(0, QString::fromStdString(node->title.empty() ? node->id : node->title));
                    previous->setText(1, QString::fromStdString(node->tuRequirement));
                    previous->setToolTip(0, QStringLiteral("%1\n%2").arg(
                        QString::fromStdString(node->id), QString::fromStdString(node->procedure)));
                }
            }
            selectStep(current);
        });
    connect(applyButton_, &QPushButton::clicked, this, &ScenarioVisualEditor::applyStep);
    connect(addRoot, &QPushButton::clicked, this, &ScenarioVisualEditor::addRootStep);
    connect(addChild, &QPushButton::clicked, this, &ScenarioVisualEditor::addChildStep);
    connect(remove, &QPushButton::clicked, this, &ScenarioVisualEditor::removeStep);
    connect(moveUp, &QPushButton::clicked, this, &ScenarioVisualEditor::moveStepUp);
    connect(moveDown, &QPushButton::clicked, this, &ScenarioVisualEditor::moveStepDown);
    connect(saveButton_, &QPushButton::clicked, this, &ScenarioVisualEditor::save);
    connect(draftButton_, &QPushButton::clicked, this, &ScenarioVisualEditor::createDraft);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    load();
}

void ScenarioVisualEditor::setAvailableProcedures(const std::vector<std::string>& procedures)
{
    const QSignalBlocker block(procedureChoice_);
    procedureChoice_->clear();
    procedureChoice_->addItem(QStringLiteral("Выберите процедуру…"), QString());
    for (const auto& procedure : procedures) {
        const auto id = QString::fromStdString(procedure);
        const auto title = id == QStringLiteral("telemetry.health")
            ? QStringLiteral("Проверить состояние потока «Орбита»") : id;
        procedureChoice_->addItem(title, id);
    }
    procedureChoice_->setCurrentIndex(0);
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
    // При перестроении элементы уничтожаются; сигнал выбора в этот момент
    // не должен сохранять поля в узел с уже изменившимся индексом.
    const QSignalBlocker block(tree_);
    tree_->clear();
    for (int index = 0; index < static_cast<int>(scenario_.steps.size()); ++index)
        appendNode(nullptr, scenario_.steps[static_cast<std::size_t>(index)], {index});
    tree_->expandAll();
    if (tree_->topLevelItemCount() > 0) {
        tree_->setCurrentItem(tree_->topLevelItem(0));
        selectStep(tree_->currentItem());
    } else
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
    procedureChoice_->setEnabled(enabled);
    telemetrySettings_->setEnabled(enabled && procedure_->text() == QStringLiteral("telemetry.health"));
    requires_->setEnabled(enabled);
    resources_->setEnabled(enabled);
    arguments_->setEnabled(enabled);
    applyButton_->setEnabled(enabled);
    saveButton_->setEnabled(enabled);
    for (const auto* name : {"addScenarioRootStep", "addScenarioChildStep", "removeScenarioStep",
                             "moveScenarioStepUp", "moveScenarioStepDown"}) {
        if (auto* button = findChild<QPushButton*>(QString::fromLatin1(name)))
            button->setEnabled(enabled);
    }
    draftButton_->setVisible(published_);
}

ScenarioVisualEditor::StepPath ScenarioVisualEditor::selectedPath() const
{
    const auto* item = tree_->currentItem();
    return item ? textToPath(item->data(0, Qt::UserRole).toString()) : StepPath{};
}

void ScenarioVisualEditor::selectPath(const StepPath& path)
{
    if (path.empty()) return;
    auto* item = tree_->topLevelItem(path.front());
    for (std::size_t level = 1; item && level < path.size(); ++level)
        item = item->child(path[level]);
    if (item) tree_->setCurrentItem(item);
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
    healthConfigured_ = false;
    id_->setText(QString::fromStdString(node.id));
    title_->setText(QString::fromStdString(node.title));
    tu_->setText(QString::fromStdString(node.tuRequirement));
    procedure_->setText(QString::fromStdString(node.procedure));
    // Список — команда выбора, а не отображение текущего поля: при переходе
    // на другой шаг он не должен показывать ранее выбранную процедуру.
    procedureChoice_->setCurrentIndex(0);
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
    healthOriginalArguments_ = arguments_->toPlainText();
    healthOriginalRequires_ = requires_->toPlainText();
    healthOriginalResources_ = resources_->toPlainText();
}

void ScenarioVisualEditor::clearNodeEditor()
{
    for (auto* edit : {id_, title_, tu_, procedure_}) edit->clear();
    for (auto* edit : {requires_, resources_, arguments_}) edit->clear();
}

void ScenarioVisualEditor::applyStep()
{
    const auto path = selectedPath();
    if (!commitEditorToSelectedNode()) return;
    rebuildTree();
    selectPath(path);
}

bool ScenarioVisualEditor::commitEditorToSelectedNode()
{
    return commitEditorToNode(selectedPath());
}

bool ScenarioVisualEditor::commitEditorToNode(const StepPath& path)
{
    auto* node = findNode(path);
    // Пустое дерево и отсутствие выбранного элемента допустимы для кнопок
    // добавления: в этом случае нечего синхронизировать с моделью.
    if (!node) return true;
    const auto id = trimmedUtf8(id_->text());
    if (id.empty()) {
        QMessageBox::warning(this, QStringLiteral("Шаг сценария"), QStringLiteral("Нужен идентификатор шага."));
        return false;
    }
    const bool healthArgumentsChanged = arguments_->toPlainText() != healthOriginalArguments_;
    const bool healthRequirementsChanged = requires_->toPlainText() != healthOriginalRequires_
        || resources_->toPlainText() != healthOriginalResources_;
    if ((node->procedure != "telemetry.health" || healthArgumentsChanged)
        && procedure_->text() == QStringLiteral("telemetry.health")
        && !healthConfigured_ && !configureTelemetryHealthForNode(node)) return false;
    // Незаконченные строки нельзя молча пропускать при разборе формы:
    // оператор должен иметь возможность вернуться к ним и завершить ввод.
    std::set<std::string> argumentNames;
    for (const auto& line : arguments_->toPlainText().split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty() && (line.indexOf(QLatin1Char('=')) < 1
                                         || line.section(QLatin1Char('='), 0, 0).trimmed().isEmpty())) {
            QMessageBox::warning(this, QStringLiteral("Шаг сценария"),
                QStringLiteral("Каждый аргумент задаётся в виде «имя = значение»."));
            return false;
        }
        if (!line.trimmed().isEmpty()
            && !argumentNames.insert(trimmedUtf8(line.section(QLatin1Char('='), 0, 0))).second) {
            QMessageBox::warning(this, QStringLiteral("Шаг сценария"),
                QStringLiteral("Имена аргументов в шаге не должны повторяться."));
            return false;
        }
    }
    for (const auto& line : resources_->toPlainText().split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) continue;
        const int separator = line.indexOf(QLatin1Char('|'));
        if (separator < 1 || line.left(separator).trimmed().isEmpty()
            || line.mid(separator + 1).trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Шаг сценария"),
                QStringLiteral("Каждый ресурс задаётся в виде «ресурс | возможность»."));
            return false;
        }
    }
    if (procedure_->text() == QStringLiteral("telemetry.health")
        && (node->procedure != "telemetry.health" || healthArgumentsChanged
            || healthRequirementsChanged || healthConfigured_)) {
        const auto required = lines(requires_->toPlainText());
        const auto bound = resources(resources_->toPlainText());
        const bool hasCapability = std::find(required.begin(), required.end(), "orbita.parameter_source") != required.end()
            || std::any_of(bound.begin(), bound.end(), [](const auto& item) {
                return item.capability == "orbita.parameter_source";
            });
        if (!hasCapability) {
            QMessageBox::warning(this, QStringLiteral("Проверка потока"),
                QStringLiteral("Для проверки необходим источник параметров «Орбита»."));
            return false;
        }
    }
    // Поля формы являются временным состоянием Qt-редактора. Перед любой
    // перестановкой или изменением дерева сначала переносим их в модель
    // сценария, иначе rebuildTree() заменит форму данными до редактирования.
    node->id = id;
    node->title = trimmedUtf8(title_->text());
    node->tuRequirement = trimmedUtf8(tu_->text());
    node->procedure = trimmedUtf8(procedure_->text());
    node->requiredCapabilities.clear();
    for (const auto& capability : lines(requires_->toPlainText()))
        node->requiredCapabilities.insert(capability);
    node->requiredResources = resources(resources_->toPlainText());
    node->arguments = arguments(arguments_->toPlainText());
    // После применения новый состав полей становится базой для следующего
    // изменения, в том числе когда сохранение не перестраивает дерево.
    healthOriginalArguments_ = arguments_->toPlainText();
    healthOriginalRequires_ = requires_->toPlainText();
    healthOriginalResources_ = resources_->toPlainText();
    healthConfigured_ = false;
    return true;
}

void ScenarioVisualEditor::configureTelemetryHealth()
{
    const auto* node = findNode(selectedPath());
    configureTelemetryHealthForNode(node);
}

bool ScenarioVisualEditor::configureTelemetryHealthForNode(const orbita::stand::ScenarioNode* node)
{
    if (published_ || procedure_->text() != QStringLiteral("telemetry.health") || !node) return false;
    const bool newHealthAction = node->procedure != "telemetry.health";
    // Сначала проверяем исходный текст: разбор arguments() не должен молча
    // отбросить незаконченные или повторные строки при переходе к форме.
    std::set<std::string> names;
    for (const auto& line : arguments_->toPlainText().split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) continue;
        const int separator = line.indexOf(QLatin1Char('='));
        const auto key = trimmedUtf8(line.left(separator));
        if (separator < 1 || key.empty() || !names.insert(key).second) {
            QMessageBox::warning(this, QStringLiteral("Проверка потока"),
                QStringLiteral("Исправьте незаконченные или повторяющиеся аргументы шага."));
            return false;
        }
    }
    auto args = arguments(arguments_->toPlainText());
    const auto readPercent = [&args](const char* key, double& value) {
        const auto it = args.find(key);
        if (it == args.end()) { value = 0.0; return true; }
        bool ok = false;
        value = QLocale::c().toDouble(QString::fromStdString(it->second), &ok);
        return ok && std::isfinite(value) && value >= 0.0 && value <= 100.0;
    };
    double phrase = 0.0, group = 0.0;
    const auto flag = args.find("diagnostic_only");
    const auto flagText = flag == args.end() ? std::string{} : flag->second;
    if (!readPercent("max_phrase_error_percent", phrase) || !readPercent("max_group_error_percent", group)
        || (!flagText.empty() && flagText != "true" && flagText != "false" && flagText != "1"
            && flagText != "0" && flagText != "yes" && flagText != "no")) {
        QMessageBox::warning(this, QStringLiteral("Проверка потока"),
            QStringLiteral("Существующие настройки нельзя представить в форме. Проверьте аргументы вручную."));
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Настройка проверки потока"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(QStringLiteral(
        "Проверка оценивает приём и декодирование потока, а не соответствие изделия ТУ. "
        "Пределы ошибок относятся к потоку. Если отключить режим диагностики, "
        "успешный результат шага будет означать только исправность потока."), &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    QStringList unsupportedArguments;
    for (const auto& [key, value] : args) {
        (void)value;
        if (key != "max_phrase_error_percent" && key != "max_group_error_percent"
            && key != "diagnostic_only" && key != "technical_retries")
            unsupportedArguments << QString::fromStdString(key);
    }
    if (!unsupportedArguments.isEmpty()) {
        auto* unsupported = new QLabel(QStringLiteral(
            "Эти параметры не используются проверкой потока и будут удалены при подтверждении: %1")
            .arg(unsupportedArguments.join(QStringLiteral(", "))), &dialog);
        unsupported->setObjectName(QStringLiteral("telemetryUnsupportedArguments"));
        unsupported->setWordWrap(true);
        layout->addWidget(unsupported);
    }
    auto* form = new QFormLayout;
    auto* phraseLimit = new QDoubleSpinBox(&dialog);
    auto* groupLimit = new QDoubleSpinBox(&dialog);
    phraseLimit->setObjectName(QStringLiteral("telemetryPhraseLimit"));
    groupLimit->setObjectName(QStringLiteral("telemetryGroupLimit"));
    for (auto* spin : {phraseLimit, groupLimit}) {
        spin->setRange(0.0, 100.0);
        spin->setDecimals(12);
        spin->setSuffix(QStringLiteral(" %"));
    }
    phraseLimit->setValue(phrase);
    groupLimit->setValue(group);
    if (phraseLimit->value() != phrase || groupLimit->value() != group) {
        QMessageBox::warning(this, QStringLiteral("Проверка потока"),
            QStringLiteral("Точность сохранённого предела не поддерживается формой. Аргументы не изменены."));
        return false;
    }
    form->addRow(QStringLiteral("Максимум ошибок фраз:"), phraseLimit);
    form->addRow(QStringLiteral("Максимум ошибок групп:"), groupLimit);
    layout->addLayout(form);
    auto* diagnostic = new QComboBox(&dialog);
    diagnostic->setObjectName(QStringLiteral("telemetryDiagnosticMode"));
    QString prompt = QStringLiteral("Выберите режим проверки…");
    if (!newHealthAction) {
        const bool previous = flagText == "true" || flagText == "1" || flagText == "yes";
        prompt = previous ? QStringLiteral("Выберите режим (сейчас: диагностика)")
                          : QStringLiteral("Выберите режим (сейчас: оценка потока)");
    }
    diagnostic->addItem(prompt);
    diagnostic->addItem(QStringLiteral("Диагностика — результат НЕПОЛНО"), true);
    diagnostic->addItem(QStringLiteral("Оценка потока — возможен результат НОРМА"), false);
    layout->addWidget(diagnostic);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(diagnostic, &QComboBox::currentIndexChanged, &dialog, [buttons](int index) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(index > 0);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted || diagnostic->currentIndex() == 0) return false;

    // Изменения применяются только после подтверждения формы; отмена оставляет
    // параметры прежнего действия нетронутыми.
    for (const auto& key : unsupportedArguments)
        args.erase(key.toStdString());
    args["max_phrase_error_percent"] = QString::number(phraseLimit->value(), 'g', 15).toStdString();
    args["max_group_error_percent"] = QString::number(groupLimit->value(), 'g', 15).toStdString();
    args["diagnostic_only"] = diagnostic->currentData().toBool() ? "true" : "false";
    QStringList argumentLines;
    for (const auto& [key, value] : args)
        argumentLines << QStringLiteral("%1 = %2").arg(QString::fromStdString(key), QString::fromStdString(value));
    arguments_->setPlainText(argumentLines.join(QLatin1Char('\n')));
    auto required = lines(requires_->toPlainText());
    if (std::find(required.begin(), required.end(), "orbita.parameter_source") == required.end()) {
        QString text = requires_->toPlainText();
        if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) text += QLatin1Char('\n');
        requires_->setPlainText(text + QStringLiteral("orbita.parameter_source"));
    }
    healthConfigured_ = true;
    return true;
}

void ScenarioVisualEditor::addRootStep()
{
    if (!commitEditorToSelectedNode()) return;
    const int next = static_cast<int>(scenario_.steps.size()) + 1;
    scenario_.steps.push_back({"step_" + std::to_string(next), "Новый этап", "", "", {}, {}, {}});
    rebuildTree();
    tree_->setCurrentItem(tree_->topLevelItem(tree_->topLevelItemCount() - 1));
}

void ScenarioVisualEditor::addChildStep()
{
    if (!commitEditorToSelectedNode()) return;
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
    if (!commitEditorToSelectedNode()) return;
    const StepPath path = selectedPath();
    auto* siblings = siblingsFor(path);
    if (!siblings) return;
    const int index = path.back();
    if (index < 0 || index >= static_cast<int>(siblings->size())) return;
    siblings->erase(siblings->begin() + index);
    rebuildTree();
}

void ScenarioVisualEditor::moveStepUp()
{
    if (!commitEditorToSelectedNode()) return;
    const StepPath path = selectedPath();
    auto* siblings = siblingsFor(path);
    if (!siblings || path.empty()) return;
    const int index = path.back();
    if (index <= 0 || index >= static_cast<int>(siblings->size())) return;
    std::swap((*siblings)[static_cast<std::size_t>(index)],
              (*siblings)[static_cast<std::size_t>(index - 1)]);
    rebuildTree();
    auto selected = path;
    --selected.back();
    selectPath(selected);
}

void ScenarioVisualEditor::moveStepDown()
{
    if (!commitEditorToSelectedNode()) return;
    const StepPath path = selectedPath();
    auto* siblings = siblingsFor(path);
    if (!siblings || path.empty()) return;
    const int index = path.back();
    if (index < 0 || index + 1 >= static_cast<int>(siblings->size())) return;
    std::swap((*siblings)[static_cast<std::size_t>(index)],
              (*siblings)[static_cast<std::size_t>(index + 1)]);
    rebuildTree();
    auto selected = path;
    ++selected.back();
    selectPath(selected);
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
    output += QStringLiteral("state: %1\n").arg(
        scenario_.publicationState == orbita::stand::PublicationState::Published ? QStringLiteral("published") : QStringLiteral("draft"));
    output += scenario_.steps.empty() ? QStringLiteral("steps: []\n") : QStringLiteral("steps:\n");
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
    if (published_ || !commitEditorToSelectedNode()) return;
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
    if (!published_) return;
    const QFileInfo source(path_);
    const QString draftPath = source.dir().filePath(source.completeBaseName() + QStringLiteral("_draft.yaml"));
    QFile draft(draftPath);
    // Существующий черновик принадлежит пользователю и не перезаписывается.
    if (!draft.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        QMessageBox::warning(this, QStringLiteral("Черновик"),
            QStringLiteral("Не удалось создать черновик: %1").arg(draft.errorString()));
        return;
    }
    scenario_.publicationState = orbita::stand::PublicationState::Draft;
    const auto bytes = serialize().toUtf8();
    if (draft.write(bytes) != bytes.size() || !draft.flush()) {
        scenario_.publicationState = orbita::stand::PublicationState::Published;
        const auto error = draft.errorString();
        draft.close();
        QFile::remove(draftPath);
        QMessageBox::critical(this, QStringLiteral("Черновик"),
            QStringLiteral("Не удалось записать черновик: %1").arg(error));
        return;
    }
    draft.close();
    path_ = draftPath;
    published_ = false;
    setEditorEnabled(true);
}
