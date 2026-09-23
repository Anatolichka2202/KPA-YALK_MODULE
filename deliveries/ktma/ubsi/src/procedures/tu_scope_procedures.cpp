#include "registration_layers.h"

#include <sstream>

namespace orbita::stand {
namespace {

ProcedureResult tuScopeGate(const ScenarioNode& node, ProcedureContext&)
{
    ProcedureResult result;
    const auto unresolved = node.arguments.find("unresolved");
    if (unresolved == node.arguments.end() || unresolved->second.empty()) {
        result.verdict = RunVerdict::Ok;
        result.message = "Обязательный объём ТУ подтверждён";
        return result;
    }

    result.verdict = RunVerdict::Incomplete;
    std::ostringstream message;
    message << "Полный нормативный результат по п. 5.6 недоступен. Не закрыты: "
            << unresolved->second;
    if (const auto reason = node.arguments.find("reason");
        reason != node.arguments.end() && !reason->second.empty()) {
        message << ". " << reason->second;
    }
    result.message = message.str();
    return result;
}

} // namespace

void registerTuScopeUbsiProcedures(ScenarioEngine& engine)
{
    // This is deliberately a delivery-level acceptance gate, not a station-core
    // primitive. It prevents a published UBSI run from becoming OK while a TU
    // requirement has no confirmed physical method/evidence.
    engine.registerProcedure("ubsi.tu_scope_gate", tuScopeGate);
}

} // namespace orbita::stand
