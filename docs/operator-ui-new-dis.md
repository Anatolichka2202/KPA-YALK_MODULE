# KTMA operator UI — `new-dis`

This branch keeps the accepted dark industrial operator design and attaches it to the existing backend contract instead of introducing a second scenario engine.

## Production operator path

`Home -> Production -> production session -> Preparation -> Power -> YALK-96 -> YTP -> YVP-8 -> Finish`

The production session accepts an operator name and several UBSI serial numbers. Every actual check remains an ordinary backend scenario run; the UI queue only selects the next serial and records the operator/session association in `runs/operator_sessions.csv` (`timestamp; operator; serial; scenario; status; run_id`).

There is no user-facing `YP-P` test. The production composition/backend may still require the installed `YP-P` component.

Visible scopes are:

- full UBSI;
- YALK-96;
- YTP;
- YVP-8.

The standalone YALK sub-check cards are visible for HMI review, but only the currently registered production scenario is runnable; the UI does not invent scenario IDs for analog-only / discrete-only / overload-only / 6.2 V reference runs.

## Persistent workspace

Equipment readiness is the first state of the same workspace, not a separate wizard page. The workspace then changes its central visualization according to `ScenarioEngine::RunEvent.nodeId` / `stage`.

### Power

`SUPPLY` events feed:

- setpoint / measured voltage trend;
- current consumption trend;
- step indicator for 24 / 27 / 35 / 19 / 27 / 37 / 27 V.

### YALK-96

Top-level YALK phases are mapped from scenario node IDs:

- stream / initialization;
- calibration;
- initial state;
- analog channels;
- contact thresholds;
- overload;
- 6.2 V reference.

Existing YALK `MEASUREMENT` events feed the current-channel sample trace and the all-channel overview using `ulk_address`, `command_v`, `v7_v`, `yalk_v`, `signal`, and `value_samples`. V7 remains the physical reference for the actual ISD stimulus; the 80-channel overview represents YALK-adapter data.

### YTP

Existing YTP `MEASUREMENT` events feed:

- current selected-channel sample oscillations (`value_samples`);
- the 30-channel overview;
- measured resistance vs the confirmed reference.

The lower R4831 graph shows 0 / 120 / 240 ohm stimulus points. `OPERATOR` events move its active point and show the requested value.

The actual resistance-change modal is **not duplicated in TestPage**. The existing `operator.manual_input / confirm_value` binding in `MainWindow` continues to call the modal `QInputDialog::getDouble` on the UI thread; therefore the backend remains the single authority for operator confirmation.

### YVP-8

The workspace is present, but the UI does not bypass current commissioning gates. If the backend returns `INCOMPLETE`, that is shown as the actual result.

## TU mode

TU uses the same read-only plots and `RunEvent` mapping. The UI does not expose direct hardware/stimulus buttons in graph pages; stimulus and switching remain scenario-driven. YTP resistance changes still use the existing backend modal confirmation.

## Compatibility boundary

`TestPage` keeps the existing public API and signals used by `MainWindow` / `KtmaMainWindow`:

- `equipmentCheckRequested()`;
- `runRequested(scenarioCode, serial, allowPartial)`;
- `stopRequested()`;
- `setRunEvent(...)`;
- `setRunResult(...)`;
- `setScenarioInfo(...)`;
- production scope flags.

Hidden bridge combo boxes retain the object names `testObject`, `testScope`, `testType`, and `testMode` so the existing Ktma production selector can continue to select backend scenarios without exposing the old dropdown UI.

## Verification status

The branch contains an updated `test_page_smoke_test.cpp` using real `RunEvent` field shapes for power, YALK, the YTP operator transition, and YTP measurements. Compilation/runtime still must be verified by CI or a Qt build environment; no claim is made here that the branch has already compiled successfully.
