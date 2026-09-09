from pathlib import Path
import sys

ROOT = Path.cwd()
problems = []

top = ROOT / "CMakeLists.txt"
if not top.exists():
    raise SystemExit("run from repo root")

t = top.read_text(encoding="utf-8", errors="replace")
for token in (
    "add_subdirectory(desktop_orbita)",
    "add_subdirectory(stand)",
    "add_subdirectory(registrar)",
    "add_subdirectory(ktma/ubsi)",
):
    if token in t:
        problems.append("old root boundary: " + token)

for p in ROOT.rglob("*"):
    if not p.is_file():
        continue
    if ".git" in p.parts or ".migration-backup" in p.parts:
        continue
    if p.suffix.lower() not in {".cpp",".h",".hpp",".cmake",".md",".txt",".yaml",".ps1"}:
        continue
    try:
        s = p.read_text(encoding="utf-8")
    except Exception:
        continue
    if "stand.ini" in s:
        problems.append("stand.ini reference: " + str(p.relative_to(ROOT)))

if problems:
    print("VERIFY FAILED")
    for x in sorted(set(problems)):
        print(" -", x)
    sys.exit(2)

print("VERIFY OK")
print("Теперь clean Configure/Build/CTest.")
