#!/usr/bin/env python3
"""
Import tolerance values (min_nominal, max_nominal) from CSV to parameters.db.

CSV format (minimum required columns):
  - full_address (optional): exact address string for matching
  - min_nominal: minimum value
  - max_nominal: maximum value
  - units (optional): units string

Matching strategy:
  1. Try to match by addresses.full_address (case/whitespace normalized)
  2. If not found, construct key from address components (stream,a,b,c,d,e,x,t,p without M)
     and match by that key

By default runs in --dry-run mode (no DB changes).
Use --apply to actually update the database.

Usage:
    python3 import_tolerances.py CSV_FILE [--db DB_PATH] [--dry-run|--apply]
    python3 import_tolerances.py tolerances.csv
    python3 import_tolerances.py tolerances.csv --db /path/to/parameters.db --apply
"""

import sqlite3
import csv
import sys
import argparse
from pathlib import Path
from typing import Dict, Tuple, Optional, List


def normalize_address(addr: str) -> str:
    """Normalize address for comparison: lowercase, trim whitespace, replace Cyrillic with Latin."""
    if not addr:
        return ""
    # Replace Cyrillic letters with Latin equivalents
    # П (Cyrillic) -> P (Latin)
    # М (Cyrillic) -> M (Latin)
    # В (Cyrillic) -> B (Latin)
    # А (Cyrillic) -> A (Latin)
    # С (Cyrillic) -> C (Latin)
    cyrillic_to_latin = {
        'П': 'P',
        'п': 'p',
        'М': 'M',
        'м': 'm',
        'В': 'B',
        'в': 'b',
        'А': 'A',
        'а': 'a',
        'С': 'C',
        'с': 'c',
    }
    normalized = addr.strip()
    for cyr, lat in cyrillic_to_latin.items():
        normalized = normalized.replace(cyr, lat)
    return normalized.lower()


def build_address_key(row: Dict) -> str:
    """Build address key from components (stream,a,b,c,d,e,x,t,p without M)."""
    parts = []
    cyrillic_to_latin = {
        'П': 'P', 'п': 'p',
        'М': 'M', 'м': 'm',
        'В': 'B', 'в': 'b',
        'А': 'A', 'а': 'a',
        'С': 'C', 'с': 'c',
    }

    for key in ['stream', 'a', 'b', 'c', 'd', 'e', 'x', 't', 'p']:
        if key in row:
            val = row.get(key, '').strip()
            # Normalize Cyrillic to Latin
            for cyr, lat in cyrillic_to_latin.items():
                val = val.replace(cyr, lat)
            if val and val != 'M':
                parts.append(val)
    return "|".join(parts)


def load_db_addresses(db_path: str) -> Tuple[Dict, Dict]:
    """
    Load all addresses from DB.

    Returns:
        (full_address_map, component_key_map)
        where:
        - full_address_map: {normalized_full_address -> param_id}
        - component_key_map: {component_key -> param_id}
    """
    full_address_map = {}
    component_key_map = {}

    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()

    cursor.execute("""
        SELECT a.id, a.param_id, a.full_address, a.stream, a.a, a.b, a.c, a.d, a.e, a.x, a.t, a.p
        FROM addresses a
    """)

    for row_id, param_id, full_address, stream, a, b, c, d, e, x, t, p in cursor.fetchall():
        # Map by full_address
        if full_address:
            norm_addr = normalize_address(full_address)
            full_address_map[norm_addr] = param_id

        # Map by component key
        parts = []
        for val in [stream, a, b, c, d, e, x, t, p]:
            if val and val.strip() and val.strip() != 'M':
                parts.append(val.strip())
        if parts:
            key = "|".join(parts)
            component_key_map[key] = param_id

    conn.close()

    return full_address_map, component_key_map


def match_csv_row(csv_row: Dict, full_address_map: Dict, component_key_map: Dict) -> Optional[int]:
    """
    Match CSV row to param_id.

    Returns:
        param_id if found, None otherwise
    """
    # Strategy 1: match by full_address
    if 'full_address' in csv_row:
        norm_csv_addr = normalize_address(csv_row['full_address'])
        if norm_csv_addr in full_address_map:
            return full_address_map[norm_csv_addr]

    # Strategy 2: match by component key
    csv_key = build_address_key(csv_row)
    if csv_key and csv_key in component_key_map:
        return component_key_map[csv_key]

    return None


def import_tolerances(csv_path: str, db_path: str, dry_run: bool = True) -> Dict:
    """
    Import tolerances from CSV to DB.

    Returns:
        dict with statistics: matched, not_found, rows_total
    """
    stats = {
        'rows_total': 0,
        'matched': 0,
        'not_found': 0,
        'not_found_addresses': [],
        'updates': []
    }

    # Load DB addresses
    print(f"Loading addresses from {db_path}...")
    full_address_map, component_key_map = load_db_addresses(db_path)
    print(f"  Loaded {len(full_address_map)} full_address mappings, {len(component_key_map)} component key mappings")

    # Read CSV
    print(f"\nReading {csv_path}...")
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)

        if not reader.fieldnames:
            print("Error: CSV has no headers")
            return stats

        # Validate required columns
        required = {'min_nominal', 'max_nominal'}
        if not required.issubset(set(reader.fieldnames)):
            print(f"Error: CSV must have columns: {required}")
            print(f"  Found: {reader.fieldnames}")
            return stats

        rows = list(reader)

    stats['rows_total'] = len(rows)
    print(f"  Loaded {len(rows)} rows from CSV")

    # Match and prepare updates
    for row_idx, csv_row in enumerate(rows, start=1):
        # Skip empty rows
        if not any(csv_row.values()):
            continue

        param_id = match_csv_row(csv_row, full_address_map, component_key_map)

        if param_id is None:
            stats['not_found'] += 1
            addr = csv_row.get('full_address', '')
            if not addr:
                addr = build_address_key(csv_row)
            if addr:
                stats['not_found_addresses'].append(addr)
            continue

        stats['matched'] += 1

        min_val = csv_row.get('min_nominal', '').strip()
        max_val = csv_row.get('max_nominal', '').strip()
        units = csv_row.get('units', '').strip() if 'units' in csv_row else None

        # Try to convert to float
        min_val = float(min_val) if min_val else None
        max_val = float(max_val) if max_val else None

        stats['updates'].append({
            'param_id': param_id,
            'min_nominal': min_val,
            'max_nominal': max_val,
            'units': units,
            'csv_addr': csv_row.get('full_address', build_address_key(csv_row))
        })

    # Apply updates if not dry-run
    if not dry_run:
        print(f"\nApplying {len(stats['updates'])} updates to DB...")
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()

        for update in stats['updates']:
            if update['units']:
                cursor.execute("""
                    UPDATE parameters
                    SET min_nominal=?, max_nominal=?, units=?
                    WHERE id=?
                """, (update['min_nominal'], update['max_nominal'], update['units'], update['param_id']))
            else:
                cursor.execute("""
                    UPDATE parameters
                    SET min_nominal=?, max_nominal=?
                    WHERE id=?
                """, (update['min_nominal'], update['max_nominal'], update['param_id']))

        conn.commit()
        conn.close()
        print(f"  Done!")

    return stats


def main():
    parser = argparse.ArgumentParser(
        description='Import tolerance values from CSV to parameters.db'
    )
    parser.add_argument('csv_file', help='Input CSV file with tolerances')
    parser.add_argument('--db', default='orbita/config/address/parameters.db',
                        help='Path to parameters.db (default: orbita/config/address/parameters.db)')
    parser.add_argument('--dry-run', action='store_true', default=True,
                        help='Show what would be updated without changing DB (default)')
    parser.add_argument('--apply', action='store_true',
                        help='Actually update the database')

    args = parser.parse_args()

    # Resolve paths
    csv_path = Path(args.csv_file)
    db_path = Path(args.db)

    if not csv_path.exists():
        print(f"Error: {csv_path} not found")
        sys.exit(1)

    if not db_path.exists():
        print(f"Error: {db_path} not found")
        sys.exit(1)

    # Determine mode
    dry_run = not args.apply

    print("="*60)
    if dry_run:
        print("MODE: DRY-RUN (no database changes)")
    else:
        print("MODE: APPLY (database will be updated)")
    print("="*60)

    # Run import
    stats = import_tolerances(str(csv_path), str(db_path), dry_run=dry_run)

    # Report
    print("\n" + "="*60)
    print("IMPORT REPORT")
    print("="*60)
    print(f"Total CSV rows:     {stats['rows_total']}")
    print(f"Matched:            {stats['matched']}")
    print(f"Not found:          {stats['not_found']}")
    print(f"Match rate:         {stats['matched']}/{stats['rows_total']} ({100*stats['matched']/max(1,stats['rows_total']):.1f}%)")

    if stats['not_found'] > 0:
        print(f"\nNot found addresses (first 20):")
        for addr in stats['not_found_addresses'][:20]:
            print(f"  - {addr}")
        if len(stats['not_found_addresses']) > 20:
            print(f"  ... and {len(stats['not_found_addresses'])-20} more")

    if dry_run:
        print(f"\nDry-run mode: no database changes.")
        print(f"To apply {len(stats['updates'])} updates, run with --apply")
        if len(stats['updates']) > 0:
            print(f"\nExample updates (first 3):")
            for update in stats['updates'][:3]:
                print(f"  param_id {update['param_id']}: min={update['min_nominal']}, max={update['max_nominal']}")
                if update['units']:
                    print(f"    (units: {update['units']})")

    print()


if __name__ == '__main__':
    main()
