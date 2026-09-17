#include "ktma/ubsi/ui/module.h"
#include "ktma/ui/delivery_manifest.h"

#include <cassert>

int main()
{
    ktma::ubsi::ui::Module ubsi;
    const auto product = ubsi.manifest();
    assert(product.id == QStringLiteral("ubsi"));
    assert(product.displayName == QStringLiteral("УБСИ"));

    const auto delivery = ktma::ui::makeDeliveryManifest({product});
    assert(delivery.id == QStringLiteral("ktma"));
    assert(delivery.displayName == QStringLiteral("КТМА"));
    assert(delivery.products.size() == 1);
    assert(delivery.products.front().id == QStringLiteral("ubsi"));
    return 0;
}
