#include <QApplication>
#include "mainwindow.h"
#include <QFile>
#include <QPalette>
#include <QStyleFactory>
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Единая Fusion-палитра не даёт системной светлой теме Windows
    // просачиваться в диалоги и составные виджеты поверх тёмного QSS.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#14171c"));
    palette.setColor(QPalette::WindowText, QColor("#e6eaf0"));
    palette.setColor(QPalette::Base, QColor("#0e1115"));
    palette.setColor(QPalette::AlternateBase, QColor("#1c2128"));
    palette.setColor(QPalette::ToolTipBase, QColor("#1b2129"));
    palette.setColor(QPalette::ToolTipText, QColor("#e6eaf0"));
    palette.setColor(QPalette::Text, QColor("#e6eaf0"));
    palette.setColor(QPalette::Button, QColor("#1b2129"));
    palette.setColor(QPalette::ButtonText, QColor("#c2ccd8"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Highlight, QColor("#2f80ed"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#5b6573"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#5b6573"));
    app.setPalette(palette);

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
