#pragma once

#include <QString>
#include <QVector>

namespace miltech::ui {

// Compile-time UI composition descriptor. It deliberately contains no QWidget,
// equipment, scenario-engine or product-domain types.
struct ProductUiManifest {
    QString id;
    QString displayName;
};

struct DeliveryUiManifest {
    QString id;
    QString displayName;
    QVector<ProductUiManifest> products;
};

} // namespace miltech::ui
