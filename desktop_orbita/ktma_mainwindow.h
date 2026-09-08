#pragma once

#include "mainwindow.h"
#include "ktma/ubsi/production.h"

#include <memory>
#include <optional>
#include <string>

namespace ktma::ubsi {
class ProductionLedger;
}

class KtmaMainWindow final : public MainWindow
{
    Q_OBJECT

public:
    explicit KtmaMainWindow(QWidget* parent = nullptr);
    ~KtmaMainWindow() override;

private slots:
    void runScenario(const QString& scenarioCode, const QString& objectSerial,
                     bool allowPartial);
    void finalizeProductionRun();
    void checkRigolGenerator();

private:
    void loadProductionScenarios();
    void configureProductionSelector();
    void restoreTuSelector();
    void applyProductionScenario();
    QString productionCodeForScope(const QString& scope) const;
    void clearPendingProduction() noexcept;

    std::unique_ptr<ktma::ubsi::ProductionLedger> productionLedger_;
    std::string pendingProductionRunId_;
    std::optional<ktma::ubsi::ProductionRunContext> pendingProductionContext_;
};
