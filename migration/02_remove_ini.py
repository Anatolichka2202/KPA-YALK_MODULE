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

PHASE = "02-remove-ini"
ap = argparse.ArgumentParser()
ap.add_argument("--apply", action="store_true")
args = ap.parse_args()

desktop = ROOT / ("apps/desktop" if (ROOT / "apps/desktop").exists() else "desktop_orbita")
ini = desktop / "resources" / "stand.ini"
remove(ini, PHASE, args.apply)

cmake = desktop / "CMakeLists.txt"
if cmake.exists():
    text = cmake.read_text(encoding="utf-8", errors="replace")
    text = re.sub(
        r'(?ms)^\\s*COMMAND\\s+\\$\\{CMAKE_COMMAND\\}\\s+-E\\s+copy_if_different\\s*\\n'
        r'\\s*\\$\\{CMAKE_CURRENT_SOURCE_DIR\\}/resources/stand\\.ini\\s*\\n'
        r'\\s*\\$<TARGET_FILE_DIR:MilTechStation>/stand\\.ini\\s*',
        '',
        text
    )
    write(cmake, text, PHASE, args.apply)

for p in ROOT.rglob("*"):
    if not p.is_file() or p.suffix.lower() not in {".cpp",".h",".cmake",".md",".txt"}:
        continue
    try:
        t = p.read_text(encoding="utf-8")
    except Exception:
        continue
    if "stand.ini" in t:
        print("REVIEW stand.ini reference:", p.relative_to(ROOT))
