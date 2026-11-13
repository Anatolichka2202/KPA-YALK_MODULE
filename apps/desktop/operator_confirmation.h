#pragma once

#include <QEventLoop>
#include <QMessageBox>
#include <QTimer>
#include <functional>

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
