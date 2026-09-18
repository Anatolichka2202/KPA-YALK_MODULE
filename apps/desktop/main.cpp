#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStatusBar>
#include <QStyleFactory>

#include "ktma_mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#08131d"));
    palette.setColor(QPalette::WindowText, QColor("#eaf4fb"));
    palette.setColor(QPalette::Base, QColor("#0e1e2c"));
    palette.setColor(QPalette::AlternateBase, QColor("#102333"));
    palette.setColor(QPalette::ToolTipBase, QColor("#132a3d"));
    palette.setColor(QPalette::ToolTipText, QColor("#eaf4fb"));
    palette.setColor(QPalette::Text, QColor("#eaf4fb"));
    palette.setColor(QPalette::Button, QColor("#132a3d"));
    palette.setColor(QPalette::ButtonText, QColor("#eaf4fb"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Highlight, QColor("#2e7de9"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#61788a"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#61788a"));
    app.setPalette(palette);

    QFile styleFile(":/styles.qss");
    if (styleFile.open(QFile::ReadOnly))
        app.setStyleSheet(QLatin1String(styleFile.readAll()));

    KtmaMainWindow window;
    // Approved operator layout is designed at 1920x1080 and remains usable at
    // 1600x900. The legacy QMainWindow status strip is not part of that shell.
    window.setMinimumSize(1600, 900);
    window.resize(1920, 1080);
    if (window.statusBar()) window.statusBar()->hide();
    window.show();
    return app.exec();
}
