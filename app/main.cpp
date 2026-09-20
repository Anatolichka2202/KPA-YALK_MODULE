#include "app/tu_controller.h"
#include "ui/test_page.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("УБСИ · проверка по ТУ"));
    QApplication::setOrganizationName(QStringLiteral("MilTechStation"));

    TestPage page;
    TuController controller(&page);

    page.setWindowTitle(QStringLiteral("КТМА · УБСИ · проверка по ТУ"));
    page.resize(1440, 900);
    page.show();

    return app.exec();
}
