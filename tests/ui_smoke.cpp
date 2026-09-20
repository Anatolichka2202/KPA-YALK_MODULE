#include "ui/run_journal_overlay.h"
#include "ui/test_page.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    TestPage page;
    page.setProductionMode(false);

    RunJournalOverlay journal(&page);
    journal.beginRun();
    journal.finishRun();
    return 0;
}
