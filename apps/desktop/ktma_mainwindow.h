#pragma once

#include "mainwindow.h"
#include "ktma/ubsi/production.h"

#include <memory>
#include <optional>
#include <string>

namespace ktma::ubsi {
class ProductionLedger;
}

namespace ktma::registrar {
class Registrar;
}

class KtmaMainWindow : public MainWindow
{
    Q_OBJECT

public:
    explicit KtmaMainWindow(QWidget* parent = nullptr);
    ~KtmaMainWindow() override;

protected:
    bool integrationUsesDedicatedProductionFinalizer() const override
    {
        return integrationProductionWorkflowActive();
    }

    // Universal/free scenarios use the current delivery's physical readiness
    // policy without becoming part of the delivery registry.  The ordering is
    // still KTMA/UBSI-owned (power -> independent devices -> boot wait -> ULK).
    void integrationPrepareEquipmentForScenario(
        const orbita::stand::ScenarioDefinition& scenario);

private slots:
    void runScenario(const QString& scenarioCode, const QString& objectSerial,
                     bool allowPartial);
    void finalizeProductionRun();
    void checkSelectedEquipment();

private:
    void loadTuScenarios();
    void loadProductionScenarios();
    void configureProductionSelector();
    void restoreTuSelector();
    void applyProductionScenario();
    QString productionCodeForScope(const QString& scope) const;
    void clearPendingProduction() noexcept;

    std::unique_ptr<ktma::registrar::Registrar> registrar_;
    std::unique_ptr<ktma::ubsi::ProductionLedger> productionLedger_;
    std::string pendingProductionRunId_;
    std::string pendingTuProductId_;
    std::optional<ktma::ubsi::ProductionRunContext> pendingProductionContext_;
};