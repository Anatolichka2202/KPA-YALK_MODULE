from pathlib import Path

ROOT = Path.cwd()

files = [
    ROOT / "data/scenarios/ubsi_production_yalk.yaml",
    ROOT / "data/scenarios/ubsi_tu_5_6.yaml",
    ROOT / "data/scenarios/ubsi_ulk_combined_check.yaml",
    ROOT / "data/scenarios/ubsi_yalk_contact_thresholds.yaml",
]

for path in files:
    raw = path.read_bytes()

    print("=" * 80)
    print(path.relative_to(ROOT))
    print("bytes:", len(raw))

    # UTF-8 BOM
    print("BOM:", raw.startswith(b"\xef\xbb\xbf"))

    try:
        text = raw.decode("utf-8")
        print("UTF-8: OK")
    except UnicodeDecodeError as e:
        print("UTF-8: FAIL", e)
        start = max(0, e.start - 32)
        end = min(len(raw), e.end + 32)
        print("HEX:", raw[start:end].hex(" "))

        chunk = raw[start:end]
        print("REPR:", repr(chunk))

        continue

    # щем типичный UTF-8 -> cp1251 mojibake
    suspicious = []
    for i, line in enumerate(text.splitlines(), 1):
        if "" in line or "љ" in line or "Ї" in line or "вЂ" in line:
            suspicious.append((i, line))

    print("mojibake-looking lines:", len(suspicious))

    for number, line in suspicious[:10]:
        print(f"{number}: {line}")
