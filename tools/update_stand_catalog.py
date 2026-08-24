#!/usr/bin/env python3
"""Обновляет справочную часть рабочей SQLite из data/catalog/*.csv."""

from __future__ import annotations

import csv
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DB_PATH = ROOT / "orbita" / "config" / "address" / "parameters.db"
CATALOG = ROOT / "data" / "catalog"


def rows(name: str) -> list[dict[str, str]]:
    with (CATALOG / name).open("r", encoding="utf-8-sig", newline="") as source:
        return list(csv.DictReader(source))


def main() -> None:
    connection = sqlite3.connect(DB_PATH)
    connection.execute("PRAGMA foreign_keys = ON")

    parameter_count = connection.execute("SELECT COUNT(*) FROM parameters").fetchone()[0]
    address_count = connection.execute("SELECT COUNT(*) FROM addresses").fetchone()[0]
    if parameter_count != 383 or address_count != 723:
        raise RuntimeError(
            f"Неожиданный состав исходной базы: parameters={parameter_count}, "
            f"addresses={address_count}"
        )

    connection.executescript(
        """
        CREATE TABLE IF NOT EXISTS stand_blocks (
            code TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            designation TEXT,
            number TEXT,
            transport TEXT,
            status TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS cell_types (
            code TEXT PRIMARY KEY
        );
        CREATE TABLE IF NOT EXISTS block_cells (
            block_code TEXT NOT NULL REFERENCES stand_blocks(code) ON DELETE CASCADE,
            sort_order INTEGER NOT NULL,
            cell_code TEXT NOT NULL REFERENCES cell_types(code),
            PRIMARY KEY (block_code, sort_order)
        );
        CREATE TABLE IF NOT EXISTS equipment_catalog (
            code TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            adapter TEXT NOT NULL,
            transport TEXT NOT NULL,
            status TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS test_types (
            code TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            status TEXT NOT NULL
        );
        """
    )

    blocks = rows("blocks.csv")
    cells = rows("block_cells.csv")
    equipment = rows("equipment.csv")
    test_types = rows("test_types.csv")

    with connection:
        connection.execute("DELETE FROM block_cells")
        connection.execute("DELETE FROM cell_types")
        connection.execute("DELETE FROM stand_blocks")
        connection.execute("DELETE FROM equipment_catalog")
        connection.execute("DELETE FROM test_types")

        connection.executemany(
            "INSERT INTO stand_blocks VALUES (:code, :name, :designation, :number, :transport, :status)",
            blocks,
        )
        connection.executemany(
            "INSERT INTO cell_types(code) VALUES (?)",
            [(item["cell_code"],) for item in {row["cell_code"]: row for row in cells}.values()],
        )
        connection.executemany(
            "INSERT INTO block_cells VALUES (:block_code, :sort_order, :cell_code)",
            cells,
        )
        connection.executemany(
            "INSERT INTO equipment_catalog VALUES (:code, :name, :adapter, :transport, :status)",
            equipment,
        )
        connection.executemany(
            "INSERT INTO test_types VALUES (:code, :name, :status)",
            test_types,
        )

    print(
        f"Обновлено: {len(blocks)} блока, {len(cells)} позиций состава, "
        f"{len(equipment)} единиц оборудования, {len(test_types)} вида испытаний."
    )


if __name__ == "__main__":
    main()
