#pragma once

#include "orbita_stand/execution_runtime.h"
#include "orbita_stand/scenario.h"

namespace orbita::stand {

// Registers the generic station.execute procedure against one execution-runtime
// component. The procedure translates scenario arguments into ExecutionRequest,
// captures stdout/stderr as run-event evidence and maps process lifecycle to the
// station verdict model.
//
// Supported ScenarioNode arguments:
//   target                 optional script/entrypoint; runtime profile may supply it
//   working_directory      optional override
//   timeout_ms             optional positive integer
//   expected_exit_code     optional integer, default 0
//   arg.<N>                ordered process arguments (0, 1, 2, ...)
//   env.<NAME>             process environment overrides
//   nonzero_verdict        "fail" (default) or "error"
//
// Process start failure and timeout are infrastructure errors. Cancellation is
// ABORTED. A completed process with an unexpected exit code is FAIL by default,
// because legacy bench programs commonly encode a test failure in their exit
// status; deliveries may opt into ERROR with nonzero_verdict=error.
void registerExecutionProcedure(ScenarioEngine& engine, IExecutionRuntime& runtime);

} // namespace orbita::stand
