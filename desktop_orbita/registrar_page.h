#pragma once

#include <QWidget>

#include <memory>
#include <optional>

namespace ktma::registrar {
class Registrar;
enum class Stage;
}

class QLineEdit;
class QLabel;
class QTableWidget;
class QComboBox;

class RegistrarPage final : public QWidget
{
    Q_OBJECT

public:
    struct ProductionSelection {
        QString productId;
        QString productSerial;
        QString componentId;
        ktma::registrar::Stage stage;
    };

    explicit RegistrarPage(QWidget* parent = nullptr);

    void setRegistrar(ktma::registrar::Registrar* registrar);
    std::optional<ProductionSelection> selectedProductionSelection() const;

signals:
    void homeRequested();

private slots:
    void refreshProducts();
    void createProduct();
    void refreshComposition();
    void addComponent();
    void replaceComponent();
    void showStageHistory();

private:
    QString selectedProductId() const;

    ktma::registrar::Registrar* registrar_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QLineEdit* serialEdit_ = nullptr;
    QTableWidget* productsTable_ = nullptr;
    QTableWidget* compositionTable_ = nullptr;
    QComboBox* stageCombo_ = nullptr;
    QComboBox* componentTypeCombo_ = nullptr;
    QLineEdit* componentSerialEdit_ = nullptr;
    QLineEdit* replacementSerialEdit_ = nullptr;
    QLineEdit* replacementReasonEdit_ = nullptr;
    QLabel* compositionLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};
