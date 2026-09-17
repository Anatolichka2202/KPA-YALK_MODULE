#pragma once

#include "ktma/ui/product_ui_module.h"

namespace ktma::ubsi::ui {

class Module final : public ktma::ui::ProductUiModule
{
public:
    miltech::ui::ProductUiManifest manifest() const override;
};

} // namespace ktma::ubsi::ui
