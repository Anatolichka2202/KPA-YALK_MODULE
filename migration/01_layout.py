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

PHASE = "01-layout"

ap = argparse.ArgumentParser()
ap.add_argument("--apply", action="store_true")
args = ap.parse_args()

if not (ROOT / "CMakeLists.txt").exists():
    raise SystemExit("Запускать из корня repo")

moves = [
    ("desktop_orbita", "apps/desktop"),
    ("stand", "station"),
    ("registrar", "deliveries/ktma/registrar"),
    ("ktma/ubsi", "deliveries/ktma/ubsi"),
]

for src_rel, dst_rel in moves:
    src = ROOT / src_rel
    dst = ROOT / dst_rel
    if not src.exists():
        continue
    if dst.exists():
        raise SystemExit(f"Destination exists: {dst_rel}")
    print(("MOVE " if args.apply else "WOULD MOVE ") + f"{src_rel} -> {dst_rel}")
    if args.apply:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), str(dst))

cmake = ROOT / "CMakeLists.txt"
text = cmake.read_text(encoding="utf-8", errors="replace")
repls = {
    "add_subdirectory(desktop_orbita)": "add_subdirectory(apps/desktop)",
    "add_subdirectory(stand)": "add_subdirectory(station)",
    "add_subdirectory(registrar)": "add_subdirectory(deliveries/ktma/registrar)",
    "add_subdirectory(ktma/ubsi)": "add_subdirectory(deliveries/ktma/ubsi)",
}
for a, b in repls.items():
    text = text.replace(a, b)
write(cmake, text, PHASE, args.apply)

desktop = ROOT / ("apps/desktop" if (ROOT / "apps/desktop").exists() else "desktop_orbita")
dc = desktop / "CMakeLists.txt"
if dc.exists():
    text = dc.read_text(encoding="utf-8", errors="replace")
    text = text.replace("project(OrbitaDesktop)", "project(MilTechStationDesktop)")
    text = text.replace("OrbitaDesktop", "MilTechStation")
    text = text.replace("${CMAKE_SOURCE_DIR}/registrar/include",
                        "${CMAKE_SOURCE_DIR}/deliveries/ktma/registrar/include")
    write(dc, text, PHASE, args.apply)

print("После apply: Qt Creator -> existing Kit -> Configure -> Build -> CTest")
