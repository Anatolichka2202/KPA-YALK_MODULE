#pragma once

#include "model/run_types.h"

#include <stdexcept>
#include <string>

namespace tu::procedures::detail {

enum class YalkContactVerdictPolicy {
    Strict,
    FormalNorma,
};

struct YalkContactVerdictDecision {
    RunVerdict acceptanceVerdict = RunVerdict::Fail;
    bool expectedSignal = false;
    bool rawSignal = false;
    bool rawMatch = false;
    bool formalOverride = false;
};

inline YalkContactVerdictPolicy yalkContactVerdictPolicy(const std::string& value)
{
    if (value.empty() || value == "strict") return YalkContactVerdictPolicy::Strict;
    if (value == "formal_norma") return YalkContactVerdictPolicy::FormalNorma;
    throw std::invalid_argument("Некорректная политика verdict контактного признака ЯЛК");
}

inline YalkContactVerdictDecision yalkContactVerdict(
    YalkContactVerdictPolicy policy, bool expectedSignal, bool rawSignal)
{
    const bool rawMatch = expectedSignal == rawSignal;
    const bool formalOverride = policy == YalkContactVerdictPolicy::FormalNorma && !rawMatch;
    return {rawMatch || formalOverride ? RunVerdict::Ok : RunVerdict::Fail,
            expectedSignal, rawSignal, rawMatch, formalOverride};
}

} // namespace tu::procedures::detail
