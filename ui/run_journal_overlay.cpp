#include "ui/run_journal_overlay.h"

#include <QDateTime>
#include <QEvent>
#include <QScrollBar>

#include <algorithm>
#include <chrono>

RunJournalOverlay::RunJournalOverlay(QWidget* host)
    : QPlainTextEdit(host), host_(host)
{
    setObjectName(QStringLiteral("runJournal"));
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setMaximumBlockCount(400);
    setFrameShape(QFrame::StyledPanel);
    setPlaceholderText(QStringLiteral("Журнал стенда"));
    setStyleSheet(QStringLiteral(
        "QPlainTextEdit#runJournal{"
        "background:rgba(12,18,25,235);color:#d7e1ea;"
        "border:1px solid #42505d;border-radius:5px;"
        "font-family:'Cascadia Mono','Consolas',monospace;font-size:11px;"
        "padding:5px;}"));
    if (host_) host_->installEventFilter(this);
    hide();
}

void RunJournalOverlay::beginRun()
{
    clear();
    appendPlainText(QStringLiteral("— Журнал стенда —"));
    reposition();
    show();
    raise();
}

void RunJournalOverlay::finishRun()
{
    if (!isVisible()) return;
    appendPlainText(QStringLiteral("— Проверка завершена —"));
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void RunJournalOverlay::appendRunEvent(const tu::RunEvent& event)
{
    const QString stage = QString::fromStdString(event.stage);
    const bool operational = stage == QStringLiteral("JOURNAL")
        || stage == QStringLiteral("START")
        || stage == QStringLiteral("FINISH")
        || stage == QStringLiteral("OVERLOAD")
        || stage == QStringLiteral("YALK_POINT")
        || stage == QStringLiteral("OPERATOR")
        || stage == QStringLiteral("SKIPPED")
        || stage == QStringLiteral("POWER")
        || stage == QStringLiteral("YVP_V7_POINT");
    if (!operational) return;

    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        event.timestamp.time_since_epoch()).count();
    const QString time = QDateTime::fromMSecsSinceEpoch(millis).toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString node = QString::fromStdString(event.nodeId);
    const QString message = QString::fromStdString(event.message);

    appendPlainText(QStringLiteral("%1  [%2]  %3")
        .arg(time, node.isEmpty() ? stage : node, message));
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    raise();
}

bool RunJournalOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == host_ && event->type() == QEvent::Resize) reposition();
    return QPlainTextEdit::eventFilter(watched, event);
}

void RunJournalOverlay::reposition()
{
    if (!host_) return;
    constexpr int margin = 16;
    constexpr int footerReserve = 76;
    const int width = std::clamp(host_->width() / 2, 420, 660);
    const int height = 132;
    setGeometry(std::max(margin, host_->width() - width - margin),
                std::max(margin, host_->height() - height - footerReserve),
                width, height);
}
