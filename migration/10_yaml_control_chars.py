from pathlib import Path

ROOT = Path.cwd()

for path in sorted((ROOT / "data" / "scenarios").glob("*.yaml")):
    text = path.read_text(encoding="utf-8-sig")

    bad = [i for i, ch in enumerate(text) if ord(ch) < 32 and ch not in "\n\r\t"]

    if not bad:
        continue

    print("=" * 80)
    print(path.relative_to(ROOT))

    for pos in bad[:10]:
        start = max(0, pos - 20)
        end = min(len(text), pos + 20)
        chunk = text[start:end]

        print("position:", pos)
        print("repr:", repr(chunk))
        print("codes:", [f"U+{ord(c):04X}" for c in chunk])
