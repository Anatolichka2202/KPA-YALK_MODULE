#include "brp_tu_window.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    BrpTuWindow window(app.arguments().contains(QStringLiteral("--simulate")));
    window.resize(1000, 720);
    window.show();
    return app.exec();
}
