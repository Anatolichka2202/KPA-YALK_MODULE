#include "ktma/ui/delivery_manifest.h"

#include <utility>

namespace ktma::ui {

miltech::ui::DeliveryUiManifest makeDeliveryManifest(
    QVector<miltech::ui::ProductUiManifest> products)
{
    miltech::ui::DeliveryUiManifest result;
    result.id = QStringLiteral("ktma");
    result.displayName = QStringLiteral("КТМА");
    result.products = std::move(products);
    return result;
}

} // namespace ktma::ui
