#pragma once

#include <QWidget>

class HomePage final : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget* parent = nullptr);

signals:
    // Универсальный контур: свободная проверка не регистрируется в поставке.
    void genericCheckRequested();

    // Специализированная поставка КТМА.
    void productionRequested();
    void tuRequested();
    void administrationRequested();
};
