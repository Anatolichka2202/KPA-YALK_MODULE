#pragma once

#include "ktma/ubsi/production.h"

#include <memory>
#include <string>
#include <vector>

namespace ktma::ubsi {

struct ProductionRunRecord
{
    std::string id;
    ProductionRunContext context;
    ProductionRunStatus status = ProductionRunStatus::InProgress;
    std::string runId;
    std::string openedAt;
    std::string finishedAt;
};

// Persistent product-level Production lifecycle. This table set may live in
// registrar.db, but it is owned by the KTMA/UBSI application layer rather than
// by legacy Orbita telemetry. Every record stores an immutable composition
// snapshot taken before the measurement run starts.
class ProductionLedger
{
public:
    explicit ProductionLedger(std::string databasePath);
    ~ProductionLedger();

    ProductionLedger(const ProductionLedger&) = delete;
    ProductionLedger& operator=(const ProductionLedger&) = delete;

    std::string begin(const ProductionRunContext& context);
    void attachRun(const std::string& productionRunId, const std::string& runId);
    void finish(const std::string& productionRunId, ProductionRunStatus status);

    ProductionRunRecord get(const std::string& productionRunId) const;
    std::vector<ProductionRunRecord> listForProduct(const std::string& productId) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ktma::ubsi
