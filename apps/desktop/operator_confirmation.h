#pragma once

#include <QEventLoop>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <optional>
#include <QMessageBox>
#include <QTimer>
#include <functional>
#include <cmath>

// Ожидание подтверждения не блокирует кнопку остановки в окне проверки.
inline bool requestOperatorConfirmation(QWidget* parent, const QString& title, const QString& prompt,
                                        const std::function<bool()>& stopRequested)
{
    QMessageBox question(QMessageBox::Question, title, prompt,
        QMessageBox::Yes | QMessageBox::Cancel, parent);
    question.setObjectName(QStringLiteral("operatorConfirmationDialog"));
    question.setDefaultButton(QMessageBox::Cancel);
    question.setWindowModality(Qt::NonModal);
    QEventLoop waiting;
    QTimer stopPoll;
    stopPoll.setInterval(50);
    QObject::connect(&stopPoll, &QTimer::timeout, &question, [&] {
        if (stopRequested()) question.reject();
    });
    QObject::connect(&question, &QMessageBox::finished, &waiting, &QEventLoop::quit);
    question.show();
    stopPoll.start();
    waiting.exec();
    return question.result() == QMessageBox::Yes;
}

// Запятая и точка означают десятичный разделитель, группировка разрядов
// не поддерживается: неоднозначный ввод не должен менять измеренное значение.
inline std::optional<double> parseOperatorNumber(QString text)
{
    text = text.trimmed();
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const auto value = text.toDouble(&ok);
    if (!ok || !std::isfinite(value)) return std::nullopt;
    return value;
}

// Значение вводится явно: заранее заполненного числа нет.
inline std::optional<double> requestOperatorMeasurement(QWidget* parent, const QString& title,
    const QString& prompt, const QString& unit, const std::function<bool()>& stopRequested)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("operatorMeasurementDialog"));
    dialog.setWindowTitle(title);
    dialog.setWindowModality(Qt::NonModal);
    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(prompt + QStringLiteral(" (%1)").arg(unit), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto* input = new QLineEdit(&dialog);
    input->setObjectName(QStringLiteral("operatorMeasurementValue"));
    input->setPlaceholderText(QStringLiteral("Введите число, например 12,5"));
    layout->addWidget(input);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    QObject::connect(input, &QLineEdit::textChanged, &dialog, [=] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(parseOperatorNumber(input->text()).has_value());
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    QEventLoop waiting;
    QTimer stopPoll;
    stopPoll.setInterval(50);
    QObject::connect(&stopPoll, &QTimer::timeout, &dialog, [&] { if (stopRequested()) dialog.reject(); });
    QObject::connect(&dialog, &QDialog::finished, &waiting, &QEventLoop::quit);
    dialog.show();
    stopPoll.start();
    waiting.exec();
    if (dialog.result() != QDialog::Accepted) return std::nullopt;
    return parseOperatorNumber(input->text());
}
