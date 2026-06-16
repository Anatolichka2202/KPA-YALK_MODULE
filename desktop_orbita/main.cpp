#include <QApplication>
#include "mainwindow.h"
#include <QFile>
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Тему применяем ТОЛЬКО после создания QApplication (иначе qApp == nullptr → падение)
    QFile styleFile(":/styles.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        QString style = QLatin1String(styleFile.readAll());
        app.setStyleSheet(style);
    }

    MainWindow w;
    w.show();
    return app.exec();
}
