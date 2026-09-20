#pragma once

#include "backend/scenario_engine.h"

#include <QPlainTextEdit>

class QEvent;

class RunJournalOverlay final : public QPlainTextEdit
{
public:
    explicit RunJournalOverlay(QWidget* host);

    void beginRun();
    void finishRun();
    void appendRunEvent(const tu::RunEvent& event);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reposition();

    QWidget* host_ = nullptr;
};
