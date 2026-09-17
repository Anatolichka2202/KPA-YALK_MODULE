from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, got {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# Reusable shell must not initialize delivery runtime from its base constructor.
replace_once(
    "apps/desktop/mainwindow.cpp",
    "\n    initializeStandRuntime();\n\n    // Физический источник телеметрии принадлежит ComponentRuntime",
    "\n    // Delivery/application composition initializes StationSession after the\n"
    "    // derived package is fully constructed. This keeps profile/procedure\n"
    "    // selection out of the reusable shell constructor.\n\n"
    "    // Физический источник телеметрии принадлежит ComponentRuntime",
)

# UBSI procedures are delivery-owned and must not be included by generic desktop.
replace_once(
    "apps/desktop/mainwindow.cpp",
    '#include "orbita_stand/ubsi_procedures.h"\n',
    "",
)

# Let the delivery select the fallback profile; env override remains generic.
replace_once(
    "apps/desktop/mainwindow.cpp",
    '        const QString profileName = qEnvironmentVariable(\n            "MILTECH_STAND_PROFILE", QStringLiteral("stand_ktma.yaml"));\n',
    '        const QString profileName = qEnvironmentVariable(\n            "MILTECH_STAND_PROFILE", integrationDefaultStandProfile());\n',
)

# Generic engine installs generic integrations, then asks delivery to add its procedures.
replace_once(
    "apps/desktop/mainwindow.cpp",
    '        scenarioEngine_ = std::make_unique<orbita::stand::ScenarioEngine>();\n        orbita::stand::registerUbsiProcedures(*scenarioEngine_);\n        orbita::stand::registerTelemetryProcedures(*scenarioEngine_);\n',
    '        scenarioEngine_ = std::make_unique<orbita::stand::ScenarioEngine>();\n        orbita::stand::registerTelemetryProcedures(*scenarioEngine_);\n        integrationRegisterDeliveryProcedures(*scenarioEngine_);\n',
)

# Add narrow composition hooks to the reusable shell.
replace_once(
    "apps/desktop/mainwindow.h",
    '    bool integrationResultSaved() const { return lastResultSaved_; }\n    virtual bool integrationUsesDedicatedProductionFinalizer() const\n',
    '    bool integrationResultSaved() const { return lastResultSaved_; }\n'
    '    virtual QString integrationDefaultStandProfile() const\n'
    '    {\n'
    '        return QStringLiteral("stand.yaml");\n'
    '    }\n'
    '    virtual void integrationRegisterDeliveryProcedures(\n'
    '        orbita::stand::ScenarioEngine&)\n'
    '    {\n'
    '    }\n'
    '    virtual bool integrationUsesDedicatedProductionFinalizer() const\n',
)

# KTMA owns its fallback profile and UBSI procedure registration.
replace_once(
    "apps/desktop/ktma_mainwindow.h",
    'protected:\n    bool integrationUsesDedicatedProductionFinalizer() const override\n',
    'protected:\n'
    '    QString integrationDefaultStandProfile() const override;\n'
    '    void integrationRegisterDeliveryProcedures(\n'
    '        orbita::stand::ScenarioEngine& engine) override;\n'
    '    bool integrationUsesDedicatedProductionFinalizer() const override\n',
)

replace_once(
    "apps/desktop/ktma_mainwindow.cpp",
    '#include "ktma/ubsi/equipment_readiness.h"\n',
    '#include "ktma/ubsi/equipment_readiness.h"\n#include "ktma/ubsi/procedures.h"\n',
)

marker = 'KtmaMainWindow::~KtmaMainWindow() = default;\n\n'
insert = '''KtmaMainWindow::~KtmaMainWindow() = default;\n\nQString KtmaMainWindow::integrationDefaultStandProfile() const\n{\n    return QStringLiteral("stand_ktma.yaml");\n}\n\nvoid KtmaMainWindow::integrationRegisterDeliveryProcedures(\n    orbita::stand::ScenarioEngine& engine)\n{\n    orbita::stand::registerUbsiProcedures(engine);\n}\n\n'''
replace_once("apps/desktop/ktma_mainwindow.cpp", marker, insert)

print("Delivery bootstrap split applied")
