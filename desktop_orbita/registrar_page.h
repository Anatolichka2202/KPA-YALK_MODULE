#pragma once

#include <QWidget>

#include <memory>
#include <optional>

namespace ktma::registrar {
class Registrar;
enum class Stage;
}
namespace ktma::ubsi { class ProductionLedger; }

class QLineEdit;
class QLabel;
class QTableWidget;
class QComboBox;

class RegistrarPage final : public QWidget
{
    Q_OBJECT

public:
    // Legacy component-level selection kept for history/compatibility. New
    // Production orchestration must use ProductProductionSelection below.
    struct ProductionSelection {
        QString productId;
        QString productSerial;
        QString componentId;
        QString componentType;
        QString componentSerial;
        ktma::registrar::Stage stage;
    };

    struct ProductProductionSelection {
        QString productId;
        QString productSerial;
        ktma::registrar::Stage stage;
    };

    explicit RegistrarPage(QWidget* parent = nullptr);

    void setRegistrar(ktma::registrar::Registrar* registrar);
    void setProductionLedger(ktma::ubsi::ProductionLedger* ledger) { productionLedger_ = ledger; }
    std::optional<ProductionSelection> selectedProductionSelection() const;
    std::optional<ProductProductionSelection> selectedProductionProduct() const;

signals:
    void homeRequested();
    void productionRequested();

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
    ktma::ubsi::ProductionLedger* productionLedger_ = nullptr;
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
