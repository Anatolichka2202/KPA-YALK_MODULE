from pathlib import Path

files = [
    "data/scenarios/ubsi_production_yalk.yaml",
    "data/scenarios/ubsi_tu_5_6.yaml",
    "data/scenarios/ubsi_ulk_combined_check.yaml",
    "data/scenarios/ubsi_yalk_contact_thresholds.yaml",
]

for name in files:
    p = Path(name)
    raw = p.read_bytes()
    text = raw.decode("utf-8-sig")

    print("=" * 80)
    print(name)

    for i, ch in enumerate(text):
        if ord(ch) == 0x0098 or ord(ch) == 0xFFFD:
            start = max(0, i - 30)
            end = min(len(text), i + 30)
            chunk = text[start:end]

            print("position:", i)
            print("repr:", repr(chunk))
            print("chars:", [f"U+{ord(c):04X}" for c in chunk])
            print("utf8:", chunk.encode("utf-8").hex(" "))
