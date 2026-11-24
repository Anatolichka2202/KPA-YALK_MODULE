#include "run_types.h"

namespace tu {
namespace {

int verdictRank(RunVerdict verdict) noexcept
{
    switch (verdict) {
    case RunVerdict::NotRun: return 0;
    case RunVerdict::Ok: return 1;
    case RunVerdict::Incomplete: return 2;
    case RunVerdict::Fail: return 3;
    case RunVerdict::Error: return 4;
    case RunVerdict::Aborted: return 5;
    }
    return 4;
}

} // namespace

const char* toString(RunVerdict verdict) noexcept
{
    switch (verdict) {
    case RunVerdict::NotRun: return "NOT_RUN";
    case RunVerdict::Ok: return "OK";
    case RunVerdict::Fail: return "FAIL";
    case RunVerdict::Incomplete: return "INCOMPLETE";
    case RunVerdict::Error: return "ERROR";
    case RunVerdict::Aborted: return "ABORTED";
    }
    return "ERROR";
}

RunVerdict combineVerdicts(RunVerdict current, RunVerdict next) noexcept
{
    return verdictRank(next) > verdictRank(current) ? next : current;
}


} // namespace tu
