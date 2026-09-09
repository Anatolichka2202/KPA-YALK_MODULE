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

import codecs, json

PHASE = "05-legacy-text"
ap = argparse.ArgumentParser()
ap.add_argument("--apply", action="store_true")
args = ap.parse_args()

def decode(raw):
    if raw.startswith(codecs.BOM_UTF8):
        return raw.decode("utf-8-sig"), "utf-8-sig"
    for enc in ("utf-8", "cp1251", "cp866"):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            pass
    return None, None

rows = []
bases = [
    ROOT / "orbita" / "config" / "address",
    ROOT / "data" / "catalog" / "address_sets",
]

for base in bases:
    if not base.exists():
        continue
    for f in base.rglob("*"):
        if not f.is_file() or f.suffix.lower() not in {".txt",".tol"}:
            continue
        raw = f.read_bytes()
        text, enc = decode(raw)
        rows.append({"path": str(f.relative_to(ROOT)), "encoding": enc, "bytes": len(raw)})
        if text is None:
            print("SKIP unknown encoding:", f.relative_to(ROOT))
            continue
        normalized = text.replace("\\r\\n","\\n").replace("\\r","\\n")
        if not normalized.endswith("\\n"):
            normalized += "\\n"
        data = normalized.encode("utf-8")
        if data != raw:
            print(("NORMALIZE " if args.apply else "WOULD NORMALIZE ") + str(f.relative_to(ROOT)))
            if args.apply:
                backup(f, PHASE)
                f.write_bytes(data)

out = ROOT / "data" / "legacy-address-inventory.json"
write(out, json.dumps(rows, ensure_ascii=False, indent=2) + "\\n", PHASE, args.apply)
