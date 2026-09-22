#pragma once

#include "model/run_types.h"

#include <stdexcept>
#include <string_view>

namespace tu::procedures::detail {

enum class YvpVerdictPolicy {
    Strict,
    ManualConfirmed,
};

inline YvpVerdictPolicy parseYvpVerdictPolicy(std::string_view value)
{
    if (value.empty() || value == "strict") return YvpVerdictPolicy::Strict;
    if (value == "manual_confirmed") return YvpVerdictPolicy::ManualConfirmed;
    throw std::invalid_argument("ЯВП: verdict_policy должен быть strict или manual_confirmed");
}

struct YvpVerdictDecision {
    RunVerdict rawVerdict = RunVerdict::Fail;
    RunVerdict acceptanceVerdict = RunVerdict::Fail;
    bool manuallyAccepted = false;
};

inline YvpVerdictDecision yvpVerdict(YvpVerdictPolicy policy, RunVerdict rawVerdict)
{
    const bool accepted = policy == YvpVerdictPolicy::ManualConfirmed
        && rawVerdict == RunVerdict::Fail;
    return {rawVerdict, accepted ? RunVerdict::Ok : rawVerdict, accepted};
}

inline const char* yvpVerdictPolicyName(YvpVerdictPolicy policy) noexcept
{
    return policy == YvpVerdictPolicy::ManualConfirmed ? "manual_confirmed" : "strict";
}

} // namespace tu::procedures::detail
