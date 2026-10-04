#include "free_workspace_widget.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <stdexcept>

FreeWorkspaceWidget::FreeWorkspaceWidget(
    std::vector<FreeWorkspaceCapability> capabilities,
    Invoke invoke,
    Confirm confirm,
    QWidget* parent)
    : QWidget(parent)
    , capabilities_(std::move(capabilities))
    , invoke_(std::move(invoke))
    , confirm_(std::move(confirm))
{
    setObjectName(QStringLiteral("freeWorkspaceConsole"));
    setWindowTitle(QStringLiteral("Свободный контур · команды оборудования"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(760, 560);

    auto* root = new QVBoxLayout(this);
    auto* description = new QLabel(QStringLiteral(
        "Команда передаётся только через capability зарегистрированного плагина. "
        "Перед каждым вызовом инженер подтверждает воздействие."), this);
    description->setWordWrap(true);
    root->addWidget(description);

    auto* form = new QFormLayout;
    capability_ = new QComboBox(this);
    capability_->setObjectName(QStringLiteral("freeCapability"));
    for (const auto& item : capabilities_) {
        capability_->addItem(item.title.isEmpty() ? item.id : item.title, item.id);
    }
    form->addRow(QStringLiteral("Capability:"), capability_);

    operation_ = new QLineEdit(this);
    operation_->setObjectName(QStringLiteral("freeOperation"));
    operation_->setPlaceholderText(QStringLiteral("Например: read_frame"));
    form->addRow(QStringLiteral("Операция:"), operation_);

    arguments_ = new QPlainTextEdit(this);
    arguments_->setObjectName(QStringLiteral("freeOperationArguments"));
    arguments_->setPlaceholderText(QStringLiteral("Один аргумент на строку: имя=значение"));
    arguments_->setFixedHeight(100);
    form->addRow(QStringLiteral("Аргументы:"), arguments_);
    root->addLayout(form);

    auto* controls = new QHBoxLayout;
    auto* invokeButton = new QPushButton(QStringLiteral("Выполнить команду"), this);
    invokeButton->setObjectName(QStringLiteral("invokeFreeOperation"));
    controls->addWidget(invokeButton);
    monitorAdapter_ = new QPushButton(QStringLiteral("Открыть поток адаптера"), this);
    monitorAdapter_->setObjectName(QStringLiteral("monitorFreeAdapter"));
    controls->addWidget(monitorAdapter_);
    controls->addStretch(1);
    root->addLayout(controls);

    output_ = new QPlainTextEdit(this);
    output_->setObjectName(QStringLiteral("freeOperationOutput"));
    output_->setReadOnly(true);
    output_->setPlaceholderText(QStringLiteral("Ответы плагинов будут показаны здесь"));
    root->addWidget(output_, 1);

    connect(invokeButton, &QPushButton::clicked,
            this, &FreeWorkspaceWidget::invokeSelectedOperation);
    connect(monitorAdapter_, &QPushButton::clicked, this, [this] {
        emit adapterMonitoringRequested(capability_->currentData().toString());
    });
    connect(capability_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FreeWorkspaceWidget::updateCapabilityState);
    updateCapabilityState();
}

std::map<std::string, std::string> FreeWorkspaceWidget::parseArguments(const QString& text)
{
    std::map<std::string, std::string> result;
    for (const auto& line : text.split('\n')) {
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        const int separator = trimmed.indexOf('=');
        if (separator <= 0) {
            throw std::runtime_error(QStringLiteral(
                "Аргумент должен иметь форму имя=значение: %1").arg(trimmed)
                .toUtf8().toStdString());
        }
        const auto name = trimmed.left(separator).trimmed();
        const auto value = trimmed.mid(separator + 1).trimmed();
        if (name.isEmpty()) throw std::runtime_error("Имя аргумента не может быть пустым");
        result.emplace(name.toUtf8().toStdString(), value.toUtf8().toStdString());
    }
    return result;
}

void FreeWorkspaceWidget::invokeSelectedOperation()
{
    const QString capability = capability_->currentData().toString();
    const QString operation = operation_->text().trimmed();
    if (capability.isEmpty() || operation.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Свободный контур"),
            QStringLiteral("Выберите capability и укажите операцию плагина."));
        return;
    }
    std::map<std::string, std::string> parsedArguments;
    try {
        // Сначала проверяем форму аргументов. Оператор не должен подтверждать
        // команду, которую приложение заведомо не сможет передать плагину.
        parsedArguments = parseArguments(arguments_->toPlainText());
    } catch (const std::exception& error) {
        output_->appendPlainText(QStringLiteral("ОШИБКА АРГУМЕНТОВ: %1")
            .arg(QString::fromUtf8(error.what())));
        return;
    }
    const QString confirmation = QStringLiteral(
        "Выполнить %1.%2? Команда может воздействовать на оборудование.")
        .arg(capability, operation);
    const bool accepted = confirm_
        ? confirm_(confirmation)
        : QMessageBox::question(this, QStringLiteral("Подтверждение команды"), confirmation,
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes;
    if (!accepted) return;

    try {
        const auto response = invoke_(capability.toUtf8().toStdString(),
                                      operation.toUtf8().toStdString(),
                                      parsedArguments);
        output_->appendPlainText(QStringLiteral("> %1.%2\n%3")
            .arg(capability, operation, QString::fromStdString(response).trimmed()));
    } catch (const std::exception& error) {
        output_->appendPlainText(QStringLiteral("> %1.%2\nОШИБКА: %3")
            .arg(capability, operation, QString::fromUtf8(error.what())));
    }
}

void FreeWorkspaceWidget::updateCapabilityState()
{
    const bool adapter = capability_->currentData().toString()
        == QStringLiteral("ulk.parameter_source");
    monitorAdapter_->setVisible(adapter);
}
