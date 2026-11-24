#include "brp_tu_window.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("БРП · проверка по ТУ"));
    QApplication::setOrganizationName(QStringLiteral("MilTechStation"));
    BrpTuWindow window(app.arguments().contains(QStringLiteral("--simulate")));
    window.resize(1440, 900);
    window.show();
    return app.exec();
}
