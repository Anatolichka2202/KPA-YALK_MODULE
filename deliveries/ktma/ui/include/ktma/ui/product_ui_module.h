#pragma once

#include "miltech/ui/manifest.h"

namespace ktma::ui {

// Product UI belongs to a KTMA delivery and therefore depends on the KTMA UI
// contract, while the station UI contract remains unaware of KTMA products.
class ProductUiModule
{
public:
    virtual ~ProductUiModule() = default;
    virtual miltech::ui::ProductUiManifest manifest() const = 0;
};

} // namespace ktma::ui
