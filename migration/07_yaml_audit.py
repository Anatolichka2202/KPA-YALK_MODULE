from pathlib import Path
import yaml

ROOT = Path.cwd()
errors = 0

for path in sorted((ROOT / "data").rglob("*.yaml")):
    try:
        text = path.read_text(encoding="utf-8")
        list(yaml.safe_load_all(text))
        print(f"OK   {path.relative_to(ROOT)}")
    except UnicodeDecodeError as e:
        errors += 1
        print(f"ENC  {path.relative_to(ROOT)}: {e}")
    except yaml.YAMLError as e:
        errors += 1
        mark = getattr(e, "problem_mark", None)
        if mark:
            print(
                f"BAD  {path.relative_to(ROOT)}:"
                f"{mark.line + 1}:{mark.column + 1}: {e}"
            )
        else:
            print(f"BAD  {path.relative_to(ROOT)}: {e}")

print()
print(f"шибок: {errors}")
raise SystemExit(1 if errors else 0)
