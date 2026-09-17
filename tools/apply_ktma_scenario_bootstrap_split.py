from pathlib import Path

main_cpp = Path("apps/desktop/mainwindow.cpp")
ktma_cpp = Path("apps/desktop/ktma_mainwindow.cpp")
ktma_h = Path("apps/desktop/ktma_mainwindow.h")
plan = Path("plans/active/universal-miltechstation-backend.md")

main = main_cpp.read_text(encoding="utf-8")
main = main.replace('#include "orbita_stand/ubsi_procedures.h"\n', '', 1)
main = main.replace('        orbita::stand::registerUbsiProcedures(*scenarioEngine_);\n', '', 1)
start_marker = '        const QHash<QString, QString> equipmentForCapability = {\n'
end_marker = '        QDir().mkpath(root.filePath("runs"));\n'
if start_marker not in main:
    raise SystemExit("MainWindow KTMA scenario bootstrap start marker not found")
start = main.index(start_marker)
end = main.index(end_marker, start)
main = main[:start] + '''        // Product procedure registration and scenario discovery belong to the\n        // delivery/application composition. The reusable shell only owns the\n        // generic engine and persistent run store.\n        scenarios_.clear();\n        scenarioPaths_.clear();\n''' + main[end:]
main_cpp.write_text(main, encoding="utf-8")

header = ktma_h.read_text(encoding="utf-8")
needle = 'private:\n    void loadProductionScenarios();\n'
if needle not in header:
    raise SystemExit("KtmaMainWindow private method marker not found")
header = header.replace(needle, 'private:\n    void loadTuScenarios();\n    void loadProductionScenarios();\n', 1)
ktma_h.write_text(header, encoding="utf-8")

ktma = ktma_cpp.read_text(encoding="utf-8")
include_marker = '#include "ktma/ubsi/production_report.h"\n'
if include_marker not in ktma:
    raise SystemExit("KTMA include marker not found")
ktma = ktma.replace(include_marker, include_marker + '#include "ktma/ubsi/procedures.h"\n', 1)

ctor_marker = '    integrationEnsureStandRuntime();\n    loadProductionScenarios();\n'
if ctor_marker not in ktma:
    raise SystemExit("KTMA constructor bootstrap marker not found")
ktma = ktma.replace(ctor_marker, '''    integrationEnsureStandRuntime();\n    loadTuScenarios();\n    loadProductionScenarios();\n''', 1)

impl_marker = 'void KtmaMainWindow::loadProductionScenarios()\n'
if impl_marker not in ktma:
    raise SystemExit("loadProductionScenarios implementation marker not found")

tu_impl = r'''void KtmaMainWindow::loadTuScenarios()
{
    auto* page = integrationTestPage();
    auto* engine = integrationScenarioEngine();
    if (!page) return;

    const QList<QString> publishedCodes = {
        QStringLiteral("UBSI_NORMAL_5_6"),
        QStringLiteral("YALK_FULL_5_6"),
        QStringLiteral("YALK_CONTACT_THRESHOLDS"),
        QStringLiteral("YTP_FULL_5_6"),
        QStringLiteral("YTP_120_CHECK"),
        QStringLiteral("ULK_COMBINED_CHECK"),
        QStringLiteral("BSI_DIAGNOSTIC")};

    if (!engine || !integrationStandRuntimeReady()) {
        const QString detail = QStringLiteral(
            "Сценарии ТУ недоступны: общий runtime станции не инициализирован");
        for (const auto& code : publishedCodes)
            page->setScenarioInfo(code, false, code == QStringLiteral("YTP_120_CHECK")
                                      || code == QStringLiteral("BSI_DIAGNOSTIC"), {}, detail);
        return;
    }

    // Product-owned procedures are registered by the KTMA delivery, not by the
    // reusable Station/Orbita shell.
    orbita::stand::registerUbsiProcedures(*engine);

    const QHash<QString, QString> ids = {
        {QStringLiteral("ubsi.468157.002.tu5_6.normal"), QStringLiteral("UBSI_NORMAL_5_6")},
        {QStringLiteral("ubsi.468157.002.yalk.tu5_6"), QStringLiteral("YALK_FULL_5_6")},
        {QStringLiteral("ubsi.468157.002.yalk.contact_thresholds"), QStringLiteral("YALK_CONTACT_THRESHOLDS")},
        {QStringLiteral("ubsi.468157.002.ytp.tu5_6"), QStringLiteral("YTP_FULL_5_6")},
        {QStringLiteral("ubsi.468157.002.ytp.120ohm.check"), QStringLiteral("YTP_120_CHECK")},
        {QStringLiteral("ubsi.468157.002.ulk.combined.check"), QStringLiteral("ULK_COMBINED_CHECK")},
        {QStringLiteral("bsi.468157.003.telemetry.diagnostic"), QStringLiteral("BSI_DIAGNOSTIC")}};
    const QSet<QString> diagnosticCodes = {
        QStringLiteral("YTP_120_CHECK"), QStringLiteral("BSI_DIAGNOSTIC")};
    const QHash<QString, QString> equipmentForCapability = {
        {QStringLiteral("ulk.parameter_source"), QStringLiteral("RS485")},
        {QStringLiteral("stand.switch_matrix"), QStringLiteral("ISD")},
        {QStringLiteral("orbita.parameter_source"), QStringLiteral("E20")},
        {QStringLiteral("measure.reference_voltage"), QStringLiteral("V7")},
        {QStringLiteral("measure.dc_current"), QStringLiteral("V7")},
        {QStringLiteral("measure.reference_ac_voltage"), QStringLiteral("V7")},
        {QStringLiteral("measure.reference_frequency"), QStringLiteral("V7")},
        {QStringLiteral("power.dc_supply"), QStringLiteral("AKIP")},
        {QStringLiteral("signal.generator"), QStringLiteral("RIGOL")},
        {QStringLiteral("operator.manual_input"), QStringLiteral("R4831")},
        {QStringLiteral("measure.waveform"), QStringLiteral("SCOPE")}};

    const QDir root(QCoreApplication::applicationDirPath());
    const auto catalog = orbita::stand::importCatalogYaml(
        root.filePath(QStringLiteral("catalog/catalog.yaml")).toStdString(),
        root.filePath(QStringLiteral("parameters.db")).toStdString());
    const QDir scenarioDirectory(root.filePath(QStringLiteral("scenarios")));
    QSet<QString> loaded;

    for (const auto& file : scenarioDirectory.entryInfoList(
             {QStringLiteral("*.yaml")}, QDir::Files, QDir::Name)) {
        try {
            const auto scenario = orbita::stand::loadScenarioYaml(
                file.absoluteFilePath().toUtf8().toStdString());
            const QString code = ids.value(QString::fromStdString(scenario.id));
            if (code.isEmpty()) continue;

            QStringList errors;
            if (catalog.version != scenario.catalogVersion) {
                errors << QStringLiteral("Версия каталога %1 не совпадает со сценарием %2")
                    .arg(QString::fromStdString(catalog.version),
                         QString::fromStdString(scenario.catalogVersion));
            }
            for (const auto& error : engine->validate(scenario))
                errors << QString::fromStdString(error);

            QSet<QString> requiredRoles;
            if (!diagnosticCodes.contains(code)) requiredRoles.insert(QStringLiteral("SCHEME"));
            std::function<void(const orbita::stand::ScenarioNode&)> collectRequired;
            collectRequired = [&](const orbita::stand::ScenarioNode& node) {
                for (const auto& capability : node.requiredCapabilities) {
                    const QString role = equipmentForCapability.value(
                        QString::fromStdString(capability));
                    if (!role.isEmpty()) requiredRoles.insert(role);
                }
                for (const auto& requirement : node.requiredResources) {
                    const QString role = equipmentForCapability.value(
                        QString::fromStdString(requirement.capability));
                    if (!role.isEmpty()) requiredRoles.insert(role);
                }
                for (const auto& child : node.children) collectRequired(child);
            };
            for (const auto& step : scenario.steps) collectRequired(step);
            QStringList required = requiredRoles.values();
            required.sort();

            const bool available = errors.isEmpty();
            const QString detail = available
                ? QStringLiteral("Загружен сценарий «%1», версия %2; профиль %3")
                    .arg(QString::fromStdString(scenario.title),
                         QString::fromStdString(scenario.version),
                         QString::fromStdString(integrationStandProfile().version))
                : errors.join(QStringLiteral("; "));
            if (available) {
                integrationScenarios().insert(code, scenario);
                integrationScenarioPaths().insert(code, file.absoluteFilePath());
                loaded.insert(code);
            }
            page->setScenarioInfo(code, available, diagnosticCodes.contains(code), required, detail);
        } catch (const std::exception& error) {
            integrationLog(QStringLiteral("TU scenario %1: %2")
                .arg(file.fileName(), QString::fromUtf8(error.what())));
        }
    }

    for (const auto& code : publishedCodes) {
        if (!loaded.contains(code)) {
            page->setScenarioInfo(code, false, diagnosticCodes.contains(code), {},
                QStringLiteral("Сценарий %1 не загружен").arg(code));
        }
    }
}

'''
ktma = ktma.replace(impl_marker, tu_impl + impl_marker, 1)
ktma_cpp.write_text(ktma, encoding="utf-8")

text = plan.read_text(encoding="utf-8")
text = text.replace(
    '- [x] отдельный неиспользуемый Rigol readiness path удалён; Rigol проходит общий delivery readiness plan как обычный component.\n\nОстаётся:\n\n- [ ] вынести KTMA registrar/scenario/profile bootstrap из `MainWindow` в delivery/application composition;\n- [ ] убрать оставшиеся `DeviceProfile`-совместимые desktop reads в пользу `ComponentProfile`;\n',
    '- [x] отдельный неиспользуемый Rigol readiness path удалён; Rigol проходит общий delivery readiness plan как обычный component.\n- [x] desktop equipment metadata читается из `ComponentProfile`; `standProfile_.devices` / `bindCapabilities` больше не используются в desktop.\n- [x] регистрация KTMA/UBSI procedures и discovery TU-сценариев вынесены из reusable `MainWindow` в `KtmaMainWindow`.\n\nОстаётся:\n\n- [ ] вынести KTMA registrar/profile bootstrap из `MainWindow` в delivery/application composition;\n',
    1)
text = text.replace(
    '2. вынести KTMA registrar/scenario/profile bootstrap из reusable `MainWindow`;\n',
    '2. вынести оставшийся KTMA registrar/profile bootstrap из reusable `MainWindow`;\n',
    1)
plan.write_text(text, encoding="utf-8")

print("KTMA scenario/procedure bootstrap moved out of reusable MainWindow")
