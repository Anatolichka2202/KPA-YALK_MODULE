from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}: {old[:100]!r}")
    return text.replace(old, new, 1)


def replace_count(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected} matches, got {count}: {old[:100]!r}")
    return text.replace(old, new)


# 1) Make the KTMA runtime fixture resource-aware so migrated scenarios are
# exercised through logical roles instead of only global capability routing.
path = "deliveries/ktma/ubsi/tests/scenario_runtime_test.cpp"
text = read(path)
old = '''    void safeStopAll() noexcept override { stopped = true; }
    std::set<std::string> capabilities;
    std::vector<std::string> operations;
'''
new = '''    bool resourceHasCapability(
        const std::string& resource,
        const std::string& capability) const override
    {
        static const std::map<std::string, std::set<std::string>> bindings = {
            {"power.dut", {"power.dc_supply"}},
            {"dut.parameter_source", {"ulk.parameter_source"}},
            {"switch_matrix.primary", {"stand.switch_matrix"}},
            {"measure.reference", {
                "measure.reference_voltage", "measure.dc_current",
                "measure.reference_ac_voltage", "measure.reference_frequency"}},
            {"signal.primary", {"signal.generator"}},
            {"measure.waveform.primary", {"measure.waveform"}},
        };
        const auto found = bindings.find(resource);
        return found != bindings.end()
            && found->second.count(capability) != 0
            && hasCapability(capability);
    }

    std::string invokeResource(
        const std::string& resource,
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override
    {
        if (!resourceHasCapability(resource, capability)) {
            throw std::runtime_error(
                "unexpected resource/capability: " + resource + ":" + capability);
        }
        resourceOperations.push_back(resource + ":" + capability + ":" + operation);
        return invoke(capability, operation, arguments);
    }

    void safeStopAll() noexcept override { stopped = true; }
    std::set<std::string> capabilities;
    std::vector<std::string> operations;
    std::vector<std::string> resourceOperations;
'''
text = replace_once(text, old, new, path)

# Prove the standalone YTP scenario really used the three physical delivery
# roles, and execute the fixed-120 diagnostic through the same resource-aware
# fixture as well.
anchor = '''    require(!ytpEquipment.supplyOutputEnabled,
            "YTP scenario must switch the AKIP output off after the test");
    const auto repeatedYtpRun = engine.run(
        ytpScenario, ytpEquipment, profile.version, "", false);
    require(repeatedYtpRun.verdict == RunVerdict::Ok
                && ytpEquipment.supplyEnableCount == 1
                && ytpEquipment.supplyDisableCount >= 2
                && !ytpEquipment.supplyOutputEnabled,
            "A repeated YTP run must restore AKIP output and switch it off again");
'''
replacement = anchor + '''    const auto routedVia = [&ytpEquipment](const std::string& prefix) {
        return std::any_of(ytpEquipment.resourceOperations.begin(),
            ytpEquipment.resourceOperations.end(), [&prefix](const std::string& value) {
                return value.rfind(prefix, 0) == 0;
            });
    };
    require(routedVia("power.dut:power.dc_supply:"),
            "YTP power operations must be routed through power.dut");
    require(routedVia("dut.parameter_source:ulk.parameter_source:"),
            "YTP adapter operations must be routed through dut.parameter_source");
    require(routedVia("switch_matrix.primary:stand.switch_matrix:"),
            "YTP ISD operations must be routed through switch_matrix.primary");

    FakeEquipment ytp120Equipment;
    ytp120Equipment.capabilities = {
        "ulk.parameter_source", "stand.switch_matrix",
        "catalog.parameter_resolver", "operator.manual_input", "power.dc_supply"};
    const auto ytp120Run = engine.run(
        ytp120, ytp120Equipment, profile.version, "", false);
    require(ytp120Run.verdict == RunVerdict::Ok,
            "Fixed 120-ohm YTP scenario must execute through delivery resources");
'''
text = replace_once(text, anchor, replacement, path)
write(path, text)


# 2) Migrate both standalone YTP scenarios to the same physical resource
# contracts already used by production YTP. Built-in catalog/manual services
# intentionally remain capability-only.
def migrate_ytp(path: str, version_old: str) -> None:
    text = read(path)
    text = replace_once(text, f"version: {version_old}\n", "version: 1.1.0\n", path)

    text = replace_count(
        text,
        '''    requires:\n      - power.dc_supply\n''',
        '''    resources:\n      - resource: power.dut\n        capability: power.dc_supply\n''',
        2,
        path,
    )
    text = replace_count(
        text,
        '''    requires:\n      - ulk.parameter_source\n      - stand.switch_matrix\n''',
        '''    resources:\n      - resource: dut.parameter_source\n        capability: ulk.parameter_source\n      - resource: switch_matrix.primary\n        capability: stand.switch_matrix\n''',
        2,
        path,
    )
    # Match the longer channel block before the calibration prefix it contains.
    text = replace_once(
        text,
        '''    requires:\n      - catalog.parameter_resolver\n      - ulk.parameter_source\n      - operator.manual_input\n''',
        '''    requires:\n      - catalog.parameter_resolver\n      - operator.manual_input\n    resources:\n      - resource: dut.parameter_source\n        capability: ulk.parameter_source\n''',
        path,
    )
    text = replace_once(
        text,
        '''    requires:\n      - catalog.parameter_resolver\n      - ulk.parameter_source\n''',
        '''    requires:\n      - catalog.parameter_resolver\n    resources:\n      - resource: dut.parameter_source\n        capability: ulk.parameter_source\n''',
        path,
    )
    write(path, text)


migrate_ytp("data/scenarios/ubsi_ytp_tu_5_6.yaml", "1.0.1")
migrate_ytp("data/scenarios/ubsi_ytp_120_check.yaml", "1.0.0")


# 3) Keep the active architecture plan synchronized with the code slice.
path = "plans/active/universal-miltechstation-backend.md"
text = read(path)
text = replace_once(
    text,
    '''- [x] standalone published YALK TU использует delivery resources;\n''',
    '''- [x] standalone published YALK TU использует delivery resources;\n- [x] standalone published YTP TU и fixed-120 diagnostic используют `power.dut`, `dut.parameter_source`, `switch_matrix.primary`;\n- [x] KTMA runtime test fixture поддерживает resource-aware routing и прогоняет оба standalone YTP сценария через logical roles;\n''',
    path,
)
text = replace_once(
    text,
    '''- [ ] перевести standalone YTP и оставшиеся KTMA/TU scenarios;\n- [ ] перед этим перевести их runtime tests/fakes на resource-aware provider;\n- [ ] после полной миграции убрать физические capability-only `requires:` и запретить неоднозначный global default routing.\n''',
    '''- [ ] перевести оставшиеся KTMA/TU scenarios;\n- [ ] после полной миграции убрать физические capability-only `requires:` и запретить неоднозначный global default routing.\n''',
    path,
)
text = replace_once(
    text,
    '''3. закончить resource migration standalone YTP и остальных TU-сценариев после resource-aware test fixture;\n''',
    '''3. закончить resource migration оставшихся KTMA/TU-сценариев и затем убрать physical capability-only compatibility routing;\n''',
    path,
)
write(path, text)
