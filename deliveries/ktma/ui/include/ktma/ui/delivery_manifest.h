#pragma once

#include "miltech/ui/manifest.h"

namespace ktma::ui {

miltech::ui::DeliveryUiManifest makeDeliveryManifest(
    QVector<miltech::ui::ProductUiManifest> products);

} // namespace ktma::ui
