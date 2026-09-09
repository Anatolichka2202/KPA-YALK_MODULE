from pathlib import Path
import yaml

ROOT = Path.cwd()

def mojibake_score(text: str) -> int:
    markers = (
        "", "С", "вЂ", "в„", "в€™",
        " ", "Џ", "Ў", "Ї", "Ј", "љ"
    )
    return sum(text.count(x) for x in markers)

def try_reverse(text: str):
    candidates = []

    # Типичный случай:
    # исходный UTF-8 -> ошибочно прочитан как CP1251 -> снова записан в UTF-8
    for encoding in ("cp1251", "cp866"):
        try:
            fixed = text.encode(encoding).decode("utf-8")
        except (UnicodeEncodeError, UnicodeDecodeError):
            continue

        candidates.append(fixed)

    if not candidates:
        return None

    original_score = mojibake_score(text)

    candidates.sort(key=mojibake_score)

    best = candidates[0]

    if mojibake_score(best) < original_score:
        return best

    return None


errors = 0
changed = 0

scenario_root = ROOT / "data" / "scenarios"

for path in sorted(scenario_root.glob("*.yaml")):
    original = path.read_text(encoding="utf-8-sig")

    fixed = try_reverse(original)

    if fixed is None:
        print(f"OK   {path.relative_to(ROOT)}")
        continue

    try:
        list(yaml.safe_load_all(fixed))
    except yaml.YAMLError as exc:
        errors += 1
        print(f"SKIP {path.relative_to(ROOT)}")
        print(f"     после восстановления YAML невалиден: {exc}")
        continue

    path.write_text(
        fixed,
        encoding="utf-8",
        newline="\n",
    )

    changed += 1
    print(f"FIX  {path.relative_to(ROOT)}")

print()
print(f"справлено: {changed}")
print(f"ропущено:  {errors}")

raise SystemExit(1 if errors else 0)
