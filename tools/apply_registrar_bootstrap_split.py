from pathlib import Path


def read(path):
    return Path(path).read_text(encoding="utf-8")


def write(path, text):
    Path(path).write_text(text, encoding="utf-8")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}: {old[:100]!r}")
    return text.replace(old, new, 1)


# --- reusable MainWindow header: no KTMA registrar ownership -----------------
path = "apps/desktop/mainwindow.h"
text = read(path)
text = replace_once(text, '#include "registrar.h"\n', '', "mainwindow.h registrar include")
text = replace_once(
    text,
    '    RegistrarPage* integrationRegistrarPage() const { return registrarPage_; }\n'
    '    ktma::registrar::Registrar* integrationRegistrar() const { return registrar_.get(); }\n',
    '    RegistrarPage* integrationRegistrarPage() const { return registrarPage_; }\n'
    '    QMenu* integrationToolsMenu() const { return toolsMenu_; }\n',
    "mainwindow.h integration registrar",
)
text = replace_once(
    text,
    '    std::unique_ptr<orbita::stand::RunStore> runStore_;\n'
    '    std::unique_ptr<ktma::registrar::Registrar> registrar_;\n',
    '    std::unique_ptr<orbita::stand::RunStore> runStore_;\n',
    "mainwindow.h registrar member",
)
text = replace_once(
    text,
    '    Workflow activeWorkflow_ = Workflow::None;\n'
    '    std::string pendingProductionStageAttemptId_;\n'
    '    orbita::stand::ProductionReportMetadata pendingProductionReportMetadata_;\n',
    '    Workflow activeWorkflow_ = Workflow::None;\n',
    "mainwindow.h pending production",
)
write(path, text)

# --- reusable MainWindow implementation --------------------------------------
path = "apps/desktop/mainwindow.cpp"
text = read(path)

# Remove KTMA registrar engineer dialog from the reusable toolbar.
start = text.index('    auto* registrarAction = toolsMenu_->addAction(\n')
end = text.index('    scenarioRunConnection_ = connect(', start)
text = text[:start] + text[end:]

# Runtime no longer creates registrar.db.
old = '''        // Реестр жизненного цикла намеренно отдельный от parameters.db и\n        // runs.db: UI создаёт его при первом запуске, а подробности прогона\n        // остаются в RunStore.\n        registrar_ = std::make_unique<ktma::registrar::Registrar>(\n            root.filePath(QStringLiteral("registrar.db")).toStdString());\n        registrarPage_->setRegistrar(registrar_.get());\n'''
text = replace_once(text, old, '', "mainwindow.cpp registrar init")

# Generic report writer does not receive KTMA production metadata.
text = replace_once(
    text,
    '                    const auto paths = orbita::stand::writeHtmlCsvReport(\n'
    '                        result, reportDir.toStdString(), pendingProductionReportMetadata_);\n',
    '                    const auto paths = orbita::stand::writeHtmlCsvReport(\n'
    '                        result, reportDir.toStdString());\n',
    "mainwindow.cpp generic report",
)

# Remove KTMA production-stage finalization from generic watcher.
start = text.index('            if (!pendingProductionStageAttemptId_.empty()) {\n')
end = text.index('            if (!dedicatedProductionFinalizer)\n', start)
text = text[:start] + text[end:]

# Replace product-aware base runner with a product-neutral runner. KTMA already
# disconnects this handler and installs KtmaMainWindow::runScenario.
start = text.index('void MainWindow::onRunScenario(\n')
end = text.index('\nvoid MainWindow::onStopScenario()\n', start)
replacement = '''void MainWindow::onRunScenario(\n    const QString& scenarioCode, const QString& objectSerial, bool allowPartial)\n{\n    if (!standRuntimeReady_ || !scenarioWatcher_ || scenarioWatcher_->isRunning()) return;\n    const auto iterator = scenarios_.constFind(scenarioCode);\n    if (iterator == scenarios_.cend()) {\n        testPage_->setRunInProgress(false);\n        log(QStringLiteral("Сценарий %1 не загружен").arg(scenarioCode));\n        return;\n    }\n\n    const auto scenario = iterator.value();\n    if (equipmentRegistry_->hasCapability("orbita.parameter_source")\n        && !orbita_->isRunning()) {\n        onStart();\n    }\n\n    scenarioEngine_->resetStop();\n    const std::string serial = objectSerial.trimmed().toUtf8().toStdString();\n    const bool partial = allowPartial;\n    testPage_->setRunInProgress(true, QStringLiteral("Выполняется: %1")\n        .arg(QString::fromStdString(scenario.title)));\n    log(QStringLiteral("Запуск сценария %1, объект %2")\n        .arg(QString::fromStdString(scenario.id),\n             objectSerial.isEmpty() ? QStringLiteral("без заводского номера") : objectSerial));\n    scenarioWatcher_->setFuture(QtConcurrent::run([this, scenario, serial, partial]() {\n        return scenarioEngine_->run(scenario, *equipmentRegistry_,\n            standProfile_.version, serial, partial,\n            [this](const orbita::stand::RunEvent& event) {\n                QMetaObject::invokeMethod(this, [this, event]() {\n                    testPage_->setRunEvent(event);\n                }, Qt::QueuedConnection);\n            });\n    }));\n}\n'''
text = text[:start] + replacement + text[end:]
write(path, text)

# --- KTMA owns registrar lifecycle -------------------------------------------
path = "apps/desktop/ktma_mainwindow.h"
text = read(path)
text = replace_once(
    text,
    'namespace ktma::ubsi {\nclass ProductionLedger;\n}\n',
    'namespace ktma::ubsi {\nclass ProductionLedger;\n}\n\n'
    'namespace ktma::registrar {\nclass Registrar;\n}\n',
    "ktma_mainwindow.h forward registrar",
)
text = replace_once(
    text,
    '    std::unique_ptr<ktma::ubsi::ProductionLedger> productionLedger_;\n',
    '    std::unique_ptr<ktma::registrar::Registrar> registrar_;\n'
    '    std::unique_ptr<ktma::ubsi::ProductionLedger> productionLedger_;\n',
    "ktma_mainwindow.h registrar member",
)
write(path, text)

path = "apps/desktop/ktma_mainwindow.cpp"
text = read(path)
text = replace_once(
    text,
    '#include "orbita_stand/config.h"\n',
    '#include "orbita_stand/config.h"\n#include "registrar.h"\n',
    "ktma_mainwindow.cpp registrar include",
)

# Create and publish registrar before any production/TU UI callbacks use it.
needle = '    integrationEnsureStandRuntime();\n    loadTuScenarios();\n    loadProductionScenarios();\n\n'
insert = '''    integrationEnsureStandRuntime();\n    loadTuScenarios();\n    loadProductionScenarios();\n\n    try {\n        const QDir root(QCoreApplication::applicationDirPath());\n        registrar_ = std::make_unique<ktma::registrar::Registrar>(\n            root.filePath(QStringLiteral("registrar.db")).toStdString());\n        if (auto* registrarPage = integrationRegistrarPage())\n            registrarPage->setRegistrar(registrar_.get());\n    } catch (const std::exception& error) {\n        integrationLog(QStringLiteral("Registrar не готов: %1")\n            .arg(QString::fromUtf8(error.what())));\n    }\n\n    if (auto* tools = integrationToolsMenu()) {\n        auto* registrarAction = tools->addAction(\n            QStringLiteral("Регистратор КТМА: состав и этапы"));\n        registrarAction->setToolTip(QStringLiteral(\n            "Открыть администрирование состава изделия и производственных этапов."));\n        connect(registrarAction, &QAction::triggered, this, [this] {\n            integrationOpenRegistrar();\n        });\n    }\n\n'''
text = replace_once(text, needle, insert, "ktma_mainwindow.cpp registrar bootstrap")

text = text.replace('integrationRegistrar()', 'registrar_.get()')
write(path, text)

print("Registrar bootstrap moved to KTMA application composition")
