from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "apps/desktop/mainwindow.cpp",
    "\n    initializeStandRuntime();\n\n    // Физический источник телеметрии принадлежит ComponentRuntime",
    "\n    // Station runtime is initialized by the concrete application/delivery after\n"
    "    // it provides its profile. Calling a delivery hook from this base\n"
    "    // constructor would dispatch to MainWindow, not the derived package.\n\n"
    "    // Физический источник телеметрии принадлежит ComponentRuntime",
)

replace_once(
    "apps/desktop/mainwindow.cpp",
    '''void MainWindow::onOpenStandProfile()\n{\n    const QString profileName = qEnvironmentVariable(\n        "MILTECH_STAND_PROFILE", QStringLiteral("stand_ktma.yaml"));\n    const QString path = QDir(QCoreApplication::applicationDirPath())\n        .filePath(QStringLiteral("profiles/") + profileName);\n    ScenarioYamlEditor editor(path, this);\n    editor.exec();\n}\n''',
    '''void MainWindow::onOpenStandProfile()\n{\n    if (standProfilePath_.isEmpty() || !QFileInfo::exists(standProfilePath_)) {\n        QMessageBox::warning(this, QStringLiteral("Профиль стенда"),\n            QStringLiteral("Application composition не настроила файл профиля стенда."));\n        return;\n    }\n    ScenarioYamlEditor editor(standProfilePath_, this);\n    editor.exec();\n}\n''',
)

replace_once(
    "apps/desktop/mainwindow.cpp",
    '''        const QString profileName = qEnvironmentVariable(\n            "MILTECH_STAND_PROFILE", QStringLiteral("stand_ktma.yaml"));\n        standProfile_ = orbita::stand::loadStandProfile(\n            root.filePath("profiles/" + profileName).toStdString());\n\n''',
    '''        if (standProfile_.id.empty())\n            throw std::runtime_error(\n                "Профиль станции не настроен application composition");\n\n''',
)

replace_once(
    "apps/desktop/mainwindow.h",
    '    bool integrationStandRuntimeReady() const { return standRuntimeReady_; }\n    void integrationEnsureStandRuntime()\n',
    '    bool integrationStandRuntimeReady() const { return standRuntimeReady_; }\n'
    '    void integrationConfigureStandProfile(\n'
    '        const orbita::stand::StandProfile& profile, const QString& sourcePath)\n'
    '    {\n'
    '        standProfile_ = profile;\n'
    '        standProfilePath_ = sourcePath;\n'
    '        standRuntimeReady_ = false;\n'
    '    }\n'
    '    void integrationEnsureStandRuntime()\n',
)

replace_once(
    "apps/desktop/mainwindow.h",
    '    orbita::stand::StandProfile& standProfile_ = stationSession_.profile();\n\n    // Источник принадлежит StationSession;',
    '    orbita::stand::StandProfile& standProfile_ = stationSession_.profile();\n'
    '    QString standProfilePath_;\n\n'
    '    // Источник принадлежит StationSession;',
)

replace_once(
    "apps/desktop/ktma_mainwindow.cpp",
    '''    page->registerEquipmentRow(QStringLiteral("RIGOL"),\n        QStringLiteral("Rigol DG-1022Z / ДГ10.2"), QStringLiteral("USB / VISA"),\n        QStringLiteral("Требуется только сценариям, где есть signal.generator"));\n\n    integrationEnsureStandRuntime();\n''',
    '''    page->registerEquipmentRow(QStringLiteral("RIGOL"),\n        QStringLiteral("Rigol DG-1022Z / ДГ10.2"), QStringLiteral("USB / VISA"),\n        QStringLiteral("Требуется только сценариям, где есть signal.generator"));\n\n    try {\n        const QDir root(QCoreApplication::applicationDirPath());\n        const QString profileName = qEnvironmentVariable(\n            "MILTECH_STAND_PROFILE", QStringLiteral("stand_ktma.yaml"));\n        const QString profilePath = root.filePath(\n            QStringLiteral("profiles/") + profileName);\n        integrationConfigureStandProfile(\n            orbita::stand::loadStandProfile(profilePath.toUtf8().toStdString()),\n            profilePath);\n    } catch (const std::exception& error) {\n        integrationLog(QStringLiteral("Профиль КТМА не загружен: %1")\n            .arg(QString::fromUtf8(error.what())));\n    }\n\n    integrationEnsureStandRuntime();\n''',
)

print("Profile bootstrap moved to KTMA application composition")
