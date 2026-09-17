from pathlib import Path
import re

PHYSICAL_RESOURCES = {
    "power.dc_supply": "power.dut",
    "ulk.parameter_source": "dut.parameter_source",
    "stand.switch_matrix": "switch_matrix.primary",
    "measure.reference_voltage": "measure.reference",
    "measure.dc_current": "measure.reference",
    "measure.reference_ac_voltage": "measure.reference",
    "measure.reference_frequency": "measure.reference",
    "signal.generator": "signal.primary",
    "measure.waveform": "measure.waveform.primary",
}


def read_preserving_bom(path: str):
    data = Path(path).read_bytes()
    bom = data.startswith(b"\xef\xbb\xbf")
    return data.decode("utf-8-sig"), bom


def write_preserving_bom(path: str, text: str, bom: bool) -> None:
    data = text.encode("utf-8")
    if bom:
        data = b"\xef\xbb\xbf" + data
    Path(path).write_bytes(data)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}: {old[:120]!r}")
    return text.replace(old, new, 1)


def migrate_physical_requirements(path: str, old_version: str, new_version: str) -> int:
    text, bom = read_preserving_bom(path)
    text = replace_once(
        text,
        f"version: {old_version}\n",
        f"version: {new_version}\n",
        path,
    )
    lines = text.splitlines(keepends=True)
    index = 0
    migrated = 0

    while index < len(lines):
        match = re.match(r"^(\s*)requires:\s*\n$", lines[index])
        if not match:
            index += 1
            continue

        indent = match.group(1)
        item_indent = indent + "  "
        cursor = index + 1
        requirements = []
        while cursor < len(lines):
            item = re.match(r"^" + re.escape(item_indent) + r"-\s+([^#\n]+?)\s*\n$", lines[cursor])
            if not item:
                break
            requirements.append(item.group(1).strip())
            cursor += 1

        physical = [value for value in requirements if value in PHYSICAL_RESOURCES]
        if not physical:
            index = cursor
            continue

        kept = [value for value in requirements if value not in PHYSICAL_RESOURCES]
        replacement = []
        if kept:
            replacement.append(indent + "requires:\n")
            replacement.extend(item_indent + "- " + value + "\n" for value in kept)
        replacement.append(indent + "resources:\n")
        for capability in physical:
            replacement.append(item_indent + "- resource: " + PHYSICAL_RESOURCES[capability] + "\n")
            replacement.append(item_indent + "  capability: " + capability + "\n")

        lines[index:cursor] = replacement
        index += len(replacement)
        migrated += 1

    text = "".join(lines)

    # The selected files are fully migrated in this slice: no physical device
    # capability may remain in legacy requires:. Built-in services stay there.
    for capability in PHYSICAL_RESOURCES:
        pattern = re.compile(
            r"(?m)^\s*requires:\s*$\n(?:\s+-\s+[^\n]+\n)*?\s+-\s+"
            + re.escape(capability)
            + r"\s*$"
        )
        if pattern.search(text):
            raise SystemExit(f"{path}: physical capability remained in requires: {capability}")

    write_preserving_bom(path, text, bom)
    return migrated


contact_count = migrate_physical_requirements(
    "data/scenarios/ubsi_yalk_contact_thresholds.yaml", "1.0.0", "1.1.0")
legacy_count = migrate_physical_requirements(
    "data/scenarios/ubsi_tu_5_6.yaml", "1.6.1", "1.7.0")
if contact_count != 6:
    raise SystemExit(f"contact-threshold scenario: expected 6 migrated requires blocks, got {contact_count}")
if legacy_count != 13:
    raise SystemExit(f"legacy trace scenario: expected 13 migrated requires blocks, got {legacy_count}")


# Extend the KTMA scenario contract test so a physical capability-only require
# cannot silently reappear without an explicit delivery resource.
path = "deliveries/ktma/ubsi/tests/production_scenario_test.cpp"
text = Path(path).read_text(encoding="utf-8")
text = replace_once(
    text,
    "#include <stdexcept>\n#include <string>\n",
    "#include <stdexcept>\n#include <string>\n#include <set>\n",
    path,
)
component_helper = '''bool componentProvides(
    const ComponentProfile& component,
    const std::string& capability)
{
    const auto& capabilities = component.capabilities.empty()
        ? component.bindings
        : component.capabilities;
    return std::find(capabilities.begin(), capabilities.end(), capability)
        != capabilities.end();
}
'''
physical_helper = component_helper + '''
bool isPhysicalCapability(const std::string& capability)
{
    static const std::set<std::string> physical = {
        "power.dc_supply",
        "ulk.parameter_source",
        "stand.switch_matrix",
        "measure.reference_voltage",
        "measure.dc_current",
        "measure.reference_ac_voltage",
        "measure.reference_frequency",
        "signal.generator",
        "measure.waveform",
    };
    return physical.count(capability) != 0;
}
'''
text = replace_once(text, component_helper, physical_helper, path)
old_loop = '''    for (const auto& capability : node.requiredCapabilities) {
        require(capability != "orbita.parameter_source",
            "KTMA acceptance scenario must not require legacy Orbita/E20");
    }
'''
new_loop = '''    for (const auto& capability : node.requiredCapabilities) {
        require(capability != "orbita.parameter_source",
            "KTMA acceptance scenario must not require legacy Orbita/E20");
        if (isPhysicalCapability(capability)) {
            const bool routed = std::any_of(
                node.requiredResources.begin(), node.requiredResources.end(),
                [&](const ResourceRequirement& requirement) {
                    return requirement.capability == capability;
                });
            require(routed,
                "Physical capability requirement has no delivery resource: "
                    + capability + " in step " + node.id);
        }
    }
'''
text = replace_once(text, old_loop, new_loop, path)
old_tail = '''        const auto yalkTu = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_yalk_tu_5_6.yaml");
        require(yalkTu.publicationState == PublicationState::Published,
            "Standalone YALK TU scenario must be published");
        verifyScenarioContracts(yalkTu, profile, engine, "Standalone YALK TU");

        std::cout << "KTMA UBSI production/TU resource contracts OK\\n";
'''
new_tail = '''        const auto yalkTu = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_yalk_tu_5_6.yaml");
        require(yalkTu.publicationState == PublicationState::Published,
            "Standalone YALK TU scenario must be published");
        verifyScenarioContracts(yalkTu, profile, engine, "Standalone YALK TU");

        const auto ytpTu = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_ytp_tu_5_6.yaml");
        require(ytpTu.publicationState == PublicationState::Published,
            "Standalone YTP TU scenario must be published");
        verifyScenarioContracts(ytpTu, profile, engine, "Standalone YTP TU");

        const auto ytp120 = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_ytp_120_check.yaml");
        require(ytp120.publicationState == PublicationState::Published,
            "Fixed-120 YTP diagnostic must be published");
        verifyScenarioContracts(ytp120, profile, engine, "Fixed-120 YTP");

        const auto contactThresholds = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_yalk_contact_thresholds.yaml");
        require(contactThresholds.publicationState == PublicationState::Published,
            "Optional YALK contact-threshold scenario must be published");
        verifyScenarioContracts(
            contactThresholds, profile, engine, "Optional YALK contact thresholds");

        const auto legacyTrace = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_tu_5_6.yaml");
        require(legacyTrace.publicationState == PublicationState::Draft,
            "Legacy trace scenario must remain a draft");
        verifyScenarioContracts(legacyTrace, profile, engine, "Legacy TU trace");

        std::cout << "KTMA UBSI production/TU resource contracts OK\\n";
'''
text = replace_once(text, old_tail, new_tail, path)
Path(path).write_text(text, encoding="utf-8")


# Keep the active plan aligned with the verified code state.
path = "plans/active/universal-miltechstation-backend.md"
text = Path(path).read_text(encoding="utf-8")
text = replace_once(
    text,
    '''- [x] standalone published YTP TU и fixed-120 diagnostic используют `power.dut`, `dut.parameter_source`, `switch_matrix.primary`;\n- [x] KTMA runtime test fixture поддерживает resource-aware routing и прогоняет оба standalone YTP сценария через logical roles;\n''',
    '''- [x] standalone published YTP TU и fixed-120 diagnostic используют `power.dut`, `dut.parameter_source`, `switch_matrix.primary`;\n- [x] KTMA runtime test fixture поддерживает resource-aware routing и прогоняет оба standalone YTP сценария через logical roles;\n- [x] optional YALK contact-threshold scenario и legacy TU trace переведены на delivery resources;\n- [x] KTMA production/TU contract test охватывает production, canonical TU, standalone YALK/YTP, fixed-120, optional contact thresholds и legacy trace и не допускает physical `requires:` без соответствующего resource;\n''',
    path,
)
text = replace_once(
    text,
    '''- [ ] перевести оставшиеся KTMA/TU scenarios;\n- [ ] после полной миграции убрать физические capability-only `requires:` и запретить неоднозначный global default routing.\n''',
    '''- [ ] убрать дублирующие physical capability-only `requires:` из уже resource-aware canonical/standalone YALK сценариев;\n- [ ] после этого запретить неоднозначный global default routing там, где delivery resource не выбран.\n''',
    path,
)
text = replace_once(
    text,
    '''3. закончить resource migration оставшихся KTMA/TU-сценариев и затем убрать physical capability-only compatibility routing;\n''',
    '''3. убрать дублирующие physical capability-only `requires:` из canonical/standalone YALK и сузить compatibility routing;\n''',
    path,
)
Path(path).write_text(text, encoding="utf-8")
