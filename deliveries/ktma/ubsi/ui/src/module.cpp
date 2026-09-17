#include "ktma/ubsi/ui/module.h"

namespace ktma::ubsi::ui {

miltech::ui::ProductUiManifest Module::manifest() const
{
    return {
        QStringLiteral("ubsi"),
        QStringLiteral("УБСИ"),
    };
}

} // namespace ktma::ubsi::ui
