# -*- coding: utf-8 -*-
"""
Миграция широкой "коленочной" parameters.db -> нормализованная parameters_v2.db.

Схема:
  parameters(id, name, category, signal_type, units, min_nominal, max_nominal)
  addresses(id, param_id, is_zu, stream, a,b,c,d,e,x,t,p, informativnost, full_address)

Адрес строится из компонентов. full_address заполняется только если известна M,
иначе NULL (M подставит UI при выборе параметра).
"""
import sqlite3

SRC = "parameters.db"
DST = "parameters_v2.db"

# Порядок колонок широкой таблицы (см. PRAGMA table_info)
# 0 Информативность 1 Параметр 2 Поток 3 А 4 В 5 С 6 D 7 E 8 X 9 T 10 P
# 11 Полный адрес 12 Категория
# 13 Информативность_зу 14 Поток_зу 15 А_зу 16 В_зу 17 С_зу 18 D_зу 19 E_зу
# 20 X_зу 21 T_зу 22 P_зу 23 Полный адрес_зу
C = dict(inf=0, name=1, potok=2, a=3, b=4, c=5, d=6, e=7, x=8, t=9, p=10,
         full=11, cat=12,
         inf_zu=13, potok_zu=14, a_zu=15, b_zu=16, c_zu=17, d_zu=18, e_zu=19,
         x_zu=20, t_zu=21, p_zu=22, full_zu=23)


def clean(v):
    s = ("" if v is None else str(v)).strip()
    return "" if s.lower() in ("", "nan", "none") else s


def signal_type(t):
    t = clean(t).zfill(2) if clean(t) else ""
    return {
        "01": "analog10", "05": "contact", "11": "temperature",
        "21": "fast1", "22": "fast2", "23": "fast3", "24": "fast4",
        "25": "bus",
    }.get(t, "unknown")


def build_address(inf, potok, a, b, c, d, e, x, t, p):
    """Собирает нормализованный адрес M{inf}P{stream}{A..X}T{t}[P{p}].
       Возвращает None, если нет M или потока (тогда строим позже с выбранной M)."""
    inf = clean(inf)
    stream = clean(potok).replace("П", "").replace("P", "")
    if not inf or not stream:
        return None
    parts = ["M", inf, "P", stream]
    for letter, val in (("A", a), ("B", b), ("C", c), ("D", d), ("E", e), ("X", x)):
        v = clean(val)
        if v:
            parts += [letter, v]
    tt = clean(t)
    if tt:
        parts += ["T", tt.zfill(2)]
    pp = clean(p)
    if tt.zfill(2) == "05" and pp:        # бит только для контактных (T05)
        parts += ["P", pp.zfill(2)]
    return "".join(parts)


def main():
    src = sqlite3.connect(SRC)
    rows = src.execute("SELECT * FROM parameters").fetchall()
    src.close()

    dst = sqlite3.connect(DST)
    dst.executescript("""
        DROP TABLE IF EXISTS addresses;
        DROP TABLE IF EXISTS parameters;
        CREATE TABLE parameters (
            id          INTEGER PRIMARY KEY,
            name        TEXT NOT NULL,
            category    TEXT,
            signal_type TEXT,
            units       TEXT,
            min_nominal REAL,
            max_nominal REAL
        );
        CREATE TABLE addresses (
            id             INTEGER PRIMARY KEY,
            param_id       INTEGER NOT NULL REFERENCES parameters(id),
            is_zu          INTEGER NOT NULL DEFAULT 0,
            stream         TEXT,
            a TEXT, b TEXT, c TEXT, d TEXT, e TEXT, x TEXT, t TEXT, p TEXT,
            informativnost INTEGER,
            full_address   TEXT
        );
        CREATE INDEX idx_addr_param ON addresses(param_id);
        CREATE INDEX idx_addr_full  ON addresses(full_address);
    """)

    n_param = n_live = n_zu = n_full = 0
    for r in rows:
        name = clean(r[C['name']])
        if not name:
            continue
        cat = clean(r[C['cat']])
        st = signal_type(r[C['t']])
        cur = dst.execute(
            "INSERT INTO parameters(name,category,signal_type) VALUES(?,?,?)",
            (name, cat, st))
        pid = cur.lastrowid
        n_param += 1

        def add(is_zu, inf, potok, a, b, c, d, e, x, t, p):
            nonlocal n_live, n_zu, n_full
            if not clean(potok):
                return
            full = build_address(inf, potok, a, b, c, d, e, x, t, p)
            infi = int(clean(inf)) if clean(inf).isdigit() else None
            dst.execute(
                """INSERT INTO addresses
                   (param_id,is_zu,stream,a,b,c,d,e,x,t,p,informativnost,full_address)
                   VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)""",
                (pid, is_zu, clean(potok),
                 clean(a), clean(b), clean(c), clean(d), clean(e), clean(x),
                 clean(t), clean(p), infi, full))
            if is_zu: n_zu += 1
            else:     n_live += 1
            if full:  n_full += 1

        add(0, r[C['inf']], r[C['potok']], r[C['a']], r[C['b']], r[C['c']],
            r[C['d']], r[C['e']], r[C['x']], r[C['t']], r[C['p']])
        add(1, r[C['inf_zu']], r[C['potok_zu']], r[C['a_zu']], r[C['b_zu']],
            r[C['c_zu']], r[C['d_zu']], r[C['e_zu']], r[C['x_zu']],
            r[C['t_zu']], r[C['p_zu']])

    dst.commit()

    # Сверка: совпадает ли наш построенный адрес с готовым "Полный адрес" из старой БД
    src2 = sqlite3.connect(SRC)
    mismatch = 0
    checked = 0
    for r in src2.execute("SELECT * FROM parameters"):
        old = clean(r[C['full']])
        if not old:
            continue
        built = build_address(r[C['inf']], r[C['potok']], r[C['a']], r[C['b']],
                              r[C['c']], r[C['d']], r[C['e']], r[C['x']],
                              r[C['t']], r[C['p']])
        # нормализуем старый к латинице для сравнения
        norm_old = (old.replace("М", "M").replace("П", "P").replace("А", "A")
                    .replace("В", "B").replace("С", "C").replace("Е", "E")
                    .replace("Т", "T").replace("Х", "X").upper())
        checked += 1
        if built != norm_old:
            mismatch += 1
            if mismatch <= 5:
                print(f"  MISMATCH: built={built}  old={norm_old}")
    src2.close()
    dst.close()

    print(f"parameters: {n_param}")
    print(f"addresses:  live={n_live}  zu={n_zu}  with full_address={n_full}")
    print(f"sanity vs old 'Полный адрес': checked={checked} mismatch={mismatch}")


if __name__ == "__main__":
    main()
