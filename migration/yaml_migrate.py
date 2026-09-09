#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

import argparse
import difflib
import json
import re
import shutil
from pathlib import Path

import yaml

ROOT = Path.cwd()
DATA = ROOT / "data"
BACKUP = ROOT / ".migration-backup" / "yaml_migrate"


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def mojibake_score(text: str) -> int:
    markers = (
        "Рђ","Р‘","Р’","Р“","Р”","Р•","Р–","Р—","Р","Р™","Рљ","Р›",
        "Рњ","Рќ","Рћ","Рџ","Р ","РЎ","Рў","РЈ","Р¤","РҐ","Р¦","Р§",
        "РЁ","Р©","РЄ","Р«","Р¬","Р­","Р®","РЇ",
        "Р°","Р±","РІ","Рі","Рґ","Рµ","Р¶","Р·","Рё","Р№","Рє","Р»",
        "Рј","РЅ","Рѕ","Рї","СЂ","СЃ","С‚","Сѓ","С„","С…","С†","С‡",
        "С€","С‰","СЉ","С‹","СЊ","СЌ","СЋ","СЏ",
        "вЂ","в„","в€™"
    )
    return sum(text.count(x) for x in markers)


def reverse_mojibake(text: str) -> str:
    """
    Типичный случай:
        original UTF-8
        -> decode CP1251
        -> encode UTF-8

    Обратно:
        encode CP1251
        -> decode UTF-8
    """
    try:
        candidate = text.encode("cp1251").decode("utf-8")
    except (UnicodeEncodeError, UnicodeDecodeError):
        return text

    if mojibake_score(candidate) < mojibake_score(text):
        return candidate

    return text


def normalize(text: str) -> str:
    text = text.lstrip("\ufeff")
    text = reverse_mojibake(text)
    text = text.replace("\r\n", "\n").replace("\r", "\n")

    lines = []
    for line in text.splitlines():
        m = re.match(
            r"^(\s*(?:-\s*)?(?:title|description|message):\s*)(.+)$",
            line,
        )
        if m:
            value = m.group(2).strip()

            if (
                ": " in value
                and not value.startswith((
                    "'", '"', "|", ">", "[", "{"
                ))
            ):
                value = json.dumps(value, ensure_ascii=False)
                line = m.group(1) + value

        lines.append(line)

    return "\n".join(lines) + "\n"


def validate(path: Path, text: str):
    try:
        list(yaml.safe_load_all(text))
        return None
    except yaml.YAMLError as exc:
        return exc


def show_diff(path: Path, old: str, new: str):
    diff = difflib.unified_diff(
        old.splitlines(),
        new.splitlines(),
        fromfile=rel(path),
        tofile=rel(path) + " [migrated]",
        lineterm="",
    )
    print("\n".join(diff))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Массовая безопасная миграция YAML"
    )
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    if not DATA.exists():
        print("ERROR: data/ not found")
        return 2

    files = sorted(DATA.rglob("*.yaml"))
    files = [
        p for p in files
        if "scenarios_backup" not in p.parts
        and ".migration-backup" not in p.parts
    ]

    changed = []
    errors = []

    print(f"YAML files: {len(files)}")
    print()

    for path in files:
        try:
            old = path.read_text(encoding="utf-8")
        except UnicodeDecodeError as exc:
            errors.append((path, exc))
            print(f"BAD  {rel(path)}: UTF-8 decode error: {exc}")
            continue

        new = normalize(old)
        error = validate(path, new)

        if error is not None:
            errors.append((path, error))
            mark = getattr(error, "problem_mark", None)

            if mark:
                print(
                    f"BAD  {rel(path)}:"
                    f"{mark.line + 1}:{mark.column + 1}: {error}"
                )
            else:
                print(f"BAD  {rel(path)}: {error}")

            if new != old:
                show_diff(path, old, new)

            continue

        if new != old:
            changed.append((path, old, new))
            print(f"CHANGE {rel(path)}")

            if not args.check:
                show_diff(path, old, new)
                print()

        else:
            print(f"OK     {rel(path)}")

    print()
    print("=" * 70)
    print(f"Изменяемых файлов: {len(changed)}")
    print(f"Ошибок:            {len(errors)}")
    print("=" * 70)

    if errors:
        print("Ничего не записываю: сначала устраняем невалидные YAML.")
        return 1

    if args.check:
        print("CHECK ONLY")
        return 0

    if not args.apply:
        print()
        print("Это PREVIEW.")
        print("Для применения:")
        print("  python migration/yaml_migrate.py --apply")
        return 0

    for path, old, new in changed:
        backup = BACKUP / path.relative_to(ROOT)
        backup.parent.mkdir(parents=True, exist_ok=True)

        if not backup.exists():
            shutil.copy2(path, backup)

        path.write_text(
            new,
            encoding="utf-8",
            newline="\n",
        )

    final_errors = []

    for path in files:
        text = path.read_text(encoding="utf-8")
        error = validate(path, text)
        if error is not None:
            final_errors.append((path, error))

    if final_errors:
        print("FATAL: post-write validation failed")
        for path, error in final_errors:
            print(f"  {rel(path)}: {error}")
        return 2

    print(f"APPLIED: {len(changed)}")
    print(f"BACKUP:  {BACKUP.relative_to(ROOT)}")
    print("ALL YAML VALID")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
