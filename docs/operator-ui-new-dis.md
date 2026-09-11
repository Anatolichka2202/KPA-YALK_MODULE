# KTMA operator UI — `new-dis`

This branch keeps the accepted dark industrial operator design and attaches it to the existing backend contract instead of introducing a second scenario engine.

## Production operator path

`Home -> Production -> production session -> Preparation -> Power -> YALK-96 -> YTP -> YVP-8 -> Finish`

The production session accepts an operator full name and several UBSI serial numbers. The personnel number is not part of this screen. Recent unique operator names are loaded from `runs/operator_sessions.csv`, the latest name is selected by default, and every actual check remains an ordinary backend scenario run. The UI queue only selects the next serial and records the operator/session association (`timestamp; operator; serial; scenario; status; run_id`).

There is no user-facing `YP-P` test. The production composition/backend may still require the installed `YP-P` component.

Visible scopes are:

- full UBSI;
- YALK-96;
- YTP;
- YVP-8.

The standalone YALK sub-check cards are visible for HMI review, but only the currently registered production scenario is runnable; the UI does not invent scenario IDs for analog-only / discrete-only / overload-only / 6.2 V reference runs.

## Persistent workspace

Equipment readiness is the first state of the same workspace, not a separate wizard page. The workspace then changes its central visualization according to `ScenarioEngine::RunEvent.nodeId` / `stage`.

Opening the production session does not probe or arm equipment. The check starts only from the explicit preparation action. Legacy Orbita/E20 telemetry is not part of UBSI equipment readiness or production scenarios; E20 is opened only by the separate legacy telemetry screen's start action.

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

Existing YALK `MEASUREMENT` events feed the 80-channel histogram using `ulk_address`, `command_v`, `v7_v`, `yalk_v`, `signal`, and `value_samples`. The absolute bar scale is `0...6.2 V`; the separate amplified range strip shows channel fluctuations. Existing `BACKGROUND` events provide `background_mean`, `background_min`, and `background_max` without a second measurement backend. Discrete `signal` values are shown as a separate textual `0/1` row. V7 remains the physical reference for the actual ISD stimulus; the overview represents YALK-adapter data.

### YTP

Existing YTP `MEASUREMENT` and `BACKGROUND` events feed:

- current selected-channel sample oscillations (`value_samples`);
- the 30-channel histogram on an absolute `0...240 ohm` scale;
- the separate amplified `min...max` range strip;
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

The branch contains an updated `test_page_smoke_test.cpp` using real `RunEvent` field shapes for power, YALK background and measurements, the YTP operator transition, and YTP measurements. It is verified with the existing Qt 6.8.0 MinGW build tree and `desktop.test_page_smoke`.
