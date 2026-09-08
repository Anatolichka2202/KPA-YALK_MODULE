#include "registrar_page.h"

#include <QComboBox>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "registrar.h"

std::optional<RegistrarPage::ProductProductionSelection>
RegistrarPage::selectedProductionProduct() const
{
    const QString productId = selectedProductId();
    const int productRow = productsTable_ ? productsTable_->currentRow() : -1;
    const auto* productItem = productRow < 0 || !productsTable_
        ? nullptr : productsTable_->item(productRow, 0);
    if (productId.isEmpty() || !productItem || !stageCombo_) return std::nullopt;

    try {
        return ProductProductionSelection{
            productId,
            productItem->text(),
            ktma::registrar::stageFromString(
                stageCombo_->currentData().toString().toStdString())};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
