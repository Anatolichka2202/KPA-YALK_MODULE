#include "ui/test_page.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    TestPage page;
    page.setProductionMode(false);
    return 0;
}
