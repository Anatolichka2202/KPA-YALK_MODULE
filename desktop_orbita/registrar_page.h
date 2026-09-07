#pragma once

#include <QWidget>

#include <memory>

namespace ktma::registrar {
class Registrar;
}

class QLineEdit;
class QLabel;
class QTableWidget;
class QComboBox;

class RegistrarPage final : public QWidget
{
    Q_OBJECT

public:
    explicit RegistrarPage(QWidget* parent = nullptr);

    void setRegistrar(ktma::registrar::Registrar* registrar);

signals:
    void homeRequested();

private slots:
    void refreshProducts();
    void createProduct();
    void refreshComposition();
    void addComponent();

private:
    QString selectedProductId() const;

    ktma::registrar::Registrar* registrar_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QLineEdit* serialEdit_ = nullptr;
    QTableWidget* productsTable_ = nullptr;
    QTableWidget* compositionTable_ = nullptr;
    QComboBox* componentTypeCombo_ = nullptr;
    QLineEdit* componentSerialEdit_ = nullptr;
    QLabel* compositionLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};
