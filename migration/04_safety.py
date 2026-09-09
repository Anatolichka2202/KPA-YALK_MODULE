from pathlib import Path
import argparse, shutil

ROOT = Path.cwd()
BACKUP = ROOT / ".migration-backup"

def backup(path: Path, phase: str):
    if not path.exists():
        return
    dst = BACKUP / phase / path.relative_to(ROOT)
    if dst.exists():
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    if path.is_dir():
        shutil.copytree(path, dst)
    else:
        shutil.copy2(path, dst)

def write(path: Path, text: str, phase: str, apply: bool):
    old = path.read_text(encoding="utf-8", errors="replace") if path.exists() else None
    if old == text:
        return
    print(("WRITE " if apply else "WOULD WRITE ") + str(path.relative_to(ROOT)))
    if apply:
        if path.exists():
            backup(path, phase)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")

def remove(path: Path, phase: str, apply: bool):
    if not path.exists():
        return
    print(("REMOVE " if apply else "WOULD REMOVE ") + str(path.relative_to(ROOT)))
    if apply:
        backup(path, phase)
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()

import re

PHASE = "04-safety"
ap = argparse.ArgumentParser()
ap.add_argument("--apply", action="store_true")
args = ap.parse_args()

station = ROOT / ("station" if (ROOT / "station").exists() else "stand")
support = station / "plugins" / "plugin_support.h"

if not support.exists():
    raise SystemExit("plugin_support.h not found")

text = support.read_text(encoding="utf-8", errors="replace")
pattern = re.compile(r'inline void requireActiveOutputs\\(.*?\\n\\}', re.S)
m = pattern.search(text)
if not m:
    raise SystemExit("requireActiveOutputs() not found; patch manually")

replacement = """inline void requireActiveOutputs(
    const std::map<std::string, std::string>& configuration)
{
    // Global station profile is the MASTER gate.
    // A device flag can restrict it, never override global=false.
    if (!booleanValue(configuration, "profile.active_outputs_confirmed", false)) {
        throw std::runtime_error(
            "Active outputs are disabled by station profile master gate");
    }

    const auto device = configuration.find("device.active_commands_confirmed");
    if (device != configuration.end()
        && !booleanValue(configuration, "device.active_commands_confirmed", false)) {
        throw std::runtime_error(
            "Active outputs are disabled for this device");
    }
}"""

text = text[:m.start()] + replacement + text[m.end():]
write(support, text, PHASE, args.apply)

print("REVIEW вручную после apply:")
print("1. safeStop/reset/OFF не должны требовать active permission")
print("2. equipment probe/readiness не должен включать АКИП")
print("3. active_outputs_confirmed=false должен блокировать ВСЕ ON/set active operations")
