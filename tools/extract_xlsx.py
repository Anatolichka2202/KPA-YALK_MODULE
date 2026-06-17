#!/usr/bin/env python3
"""
Extract all data from База_данных_финальная.xlsx and dump to CSV.

Reads the xlsx file using standard library (zipfile + xml.etree),
handles both shared strings and inline strings, numeric cells.

Usage:
    python3 extract_xlsx.py
"""

import zipfile
import xml.etree.ElementTree as ET
import csv
import sys
from pathlib import Path


def get_cell_value(cell, shared_strings):
    """Extract value from a cell, handling inline strings and shared strings."""
    # Try shared string reference first
    v = cell.find('{http://schemas.openxmlformats.org/spreadsheetml/2006/main}v')
    cell_type = cell.get('t', 'n')

    if cell_type == 's' and v is not None:
        str_idx = int(v.text)
        return shared_strings.get(str_idx, "")

    # Try inline string
    is_elem = cell.find('{http://schemas.openxmlformats.org/spreadsheetml/2006/main}is')
    if is_elem is not None:
        parts = []
        for t_elem in is_elem.findall('{http://schemas.openxmlformats.org/spreadsheetml/2006/main}t'):
            if t_elem.text:
                parts.append(t_elem.text)
        return "".join(parts)

    # Try direct value (numeric)
    if v is not None:
        return v.text

    return ""


def extract_xlsx(xlsx_path, csv_output_path):
    """Extract all data from xlsx and save to CSV."""

    print(f"Reading {xlsx_path}...")

    with zipfile.ZipFile(xlsx_path) as z:
        # Read shared strings
        shared_strings = {}
        try:
            with z.open('xl/sharedStrings.xml') as f:
                tree = ET.parse(f)
                root = tree.getroot()
                si_elements = root.findall('{http://schemas.openxmlformats.org/spreadsheetml/2006/main}si')
                for idx, si in enumerate(si_elements):
                    parts = []
                    for t_elem in si.findall('{http://schemas.openxmlformats.org/spreadsheetml/2006/main}t'):
                        if t_elem.text:
                            parts.append(t_elem.text)
                    if parts:
                        shared_strings[idx] = "".join(parts)
        except:
            pass

        print(f"  Loaded {len(shared_strings)} shared strings")

        # Read worksheet
        with z.open('xl/worksheets/sheet1.xml') as f:
            tree = ET.parse(f)
            root = tree.getroot()

            rows = root.findall('.//{http://schemas.openxmlformats.org/spreadsheetml/2006/main}row')
            print(f"  Found {len(rows)} rows in sheet1")

            # Extract all data
            all_data = []
            for row_idx, row in enumerate(rows):
                cells = row.findall('{http://schemas.openxmlformats.org/spreadsheetml/2006/main}c')
                values = [get_cell_value(cell, shared_strings) for cell in cells]
                all_data.append(values)

            # Find max column count
            max_cols = max(len(row) for row in all_data) if all_data else 0

            # Pad rows to same length
            for row in all_data:
                while len(row) < max_cols:
                    row.append("")

            # Write to CSV
            print(f"  Writing to {csv_output_path}...")
            with open(csv_output_path, 'w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerows(all_data)

            print(f"  Written {len(all_data)} rows × {max_cols} columns")

            # Analyze: find non-empty columns
            print("\n" + "="*60)
            print("XLSX STRUCTURE ANALYSIS")
            print("="*60)

            # Header (first row)
            header = all_data[0] if all_data else []
            print(f"\nColumn headers ({len(header)} total):")
            for i, h in enumerate(header):
                print(f"  {i:2d}: {h}")

            # Find columns with data
            print("\nColumns with data in rows 1-50:")
            col_has_data = [False] * max_cols
            for row_idx in range(1, min(51, len(all_data))):
                for col_idx in range(max_cols):
                    val = all_data[row_idx][col_idx] if col_idx < len(all_data[row_idx]) else ""
                    if val and val.strip():
                        col_has_data[col_idx] = True

            for i, has_data in enumerate(col_has_data):
                if has_data:
                    print(f"  {i:2d}: {header[i] if i < len(header) else 'N/A'}")

            # Check for min/max/nominal columns
            print("\nSearching for min/max/nominal/допуск columns...")
            keywords = ['min', 'max', 'допуск', 'номинал', 'предел', 'от', 'до', 'диапазон']
            found_keywords = []
            for i, h in enumerate(header):
                lower_h = h.lower()
                for kw in keywords:
                    if kw in lower_h:
                        found_keywords.append((i, h, kw))

            if found_keywords:
                print("  FOUND tolerance/nominal columns:")
                for col_idx, col_name, keyword in found_keywords:
                    print(f"    Column {col_idx}: {col_name} (matched '{keyword}')")
            else:
                print("  No tolerance/nominal columns found in headers.")
                print("  NOTE: Tolerance values need to be manually extracted from")
                print("        'Приложение 2 (цифра).docx' and provided as CSV.")

            # Show first 5 rows of data
            print("\nFirst 5 rows of data:")
            for row_idx in range(1, min(6, len(all_data))):
                print(f"  Row {row_idx}: {all_data[row_idx][:12]}")


if __name__ == '__main__':
    xlsx_path = Path("посмотри/о БД/База_данных_финальная.xlsx")
    csv_path = Path("tools/xlsx_dump.csv")

    if not xlsx_path.exists():
        print(f"Error: {xlsx_path} not found")
        sys.exit(1)

    extract_xlsx(xlsx_path, csv_path)
    print(f"\nDone! CSV saved to {csv_path}")
