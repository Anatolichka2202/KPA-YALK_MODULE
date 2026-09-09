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

import json, re

PHASE = "03-yaml"
ap = argparse.ArgumentParser()
ap.add_argument("--apply", action="store_true")
ap.add_argument("--yaml-cpp", required=True)
args = ap.parse_args()

src = Path(args.yaml_cpp)
if not (src / "CMakeLists.txt").exists():
    raise SystemExit("yaml-cpp source dir expected")

dst = ROOT / "third_party" / "yaml-cpp"
if not dst.exists():
    print(("COPY " if args.apply else "WOULD COPY ") + f"{src} -> {dst}")
    if args.apply:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(src, dst)

top = ROOT / "CMakeLists.txt"
text = top.read_text(encoding="utf-8", errors="replace")
token = "add_subdirectory(third_party/yaml-cpp)"
if token not in text:
    anchor = "add_subdirectory(orbita)"
    text = text.replace(anchor, token + "\\n" + anchor, 1) if anchor in text else token + "\\n" + text
    write(top, text, PHASE, args.apply)

# Make current files valid YAML where plain title/description contains ': '
for y in (ROOT / "data").rglob("*.yaml"):
    text = y.read_text(encoding="utf-8")
    out = []
    changed = False
    for line in text.splitlines():
        m = re.match(r'^(\\s*(?:-\\s*)?(?:title|description|message):\\s*)(.+)$', line)
        if m:
            value = m.group(2)
            if ": " in value and not value.lstrip().startswith(("'", '"', "|", ">", "[", "{")):
                line = m.group(1) + json.dumps(value, ensure_ascii=False)
                changed = True
        out.append(line)
    if changed:
        write(y, "\\n".join(out) + "\\n", PHASE, args.apply)

print("ВАЖНО: этот этап только подготавливает настоящий YAML + vendored yaml-cpp.")
print("Сам yaml_lite не удаляется автоматически: его adapter лучше заменить после первой зелёной сборки.")
print("Причина: точный public Node API надо сверить по твоему текущему station/include.")
