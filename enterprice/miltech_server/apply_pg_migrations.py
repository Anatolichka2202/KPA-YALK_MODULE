"""Controlled PostgreSQL schema initialization from migrations 001 + 002 + 003.

Requires a fresh dedicated DB (or manually audited baseline). Does NOT run automatically
at API startup. Schema changes are transactional and SHA-256 checked against the ledger.
"""
import argparse
import hashlib
import re
from pathlib import Path

MIGRATIONS = [
    Path(__file__).resolve().parents[1] / 'migrations' / '001_edge_pilot_postgres.sql',
    Path(__file__).resolve().parents[1] / 'migrations' / '002_enterprise_manufacturing.sql',
    Path(__file__).resolve().parents[1] / 'migrations' / '003_signed_delivery_postgres.sql',
]


def normalized(sql: str) -> str:
    # Prior migration 001 embeds BEGIN/COMMIT; strip only these top-level full lines.
    return re.sub(r'(?im)^\s*(?:BEGIN|COMMIT);\s*$', '', sql).strip()


def apply(url:str, *, dry_run:bool=False):
    plans=[]
    for idx,path in enumerate(MIGRATIONS,1):
        sql=path.read_text(encoding='utf-8')
        plans.append((idx,path.name,hashlib.sha256(sql.encode('utf-8')).hexdigest(),normalized(sql)))
    if dry_run:
        return [{'version':v,'file':name,'sha256':sha} for v,name,sha,_ in plans]
    try:
        import psycopg
    except ImportError as exc:
        raise RuntimeError('PostgreSQL migration requires pip install -e .[postgres]') from exc

    # Migration role must have CREATE on the dedicated database. Do not use runtime station token.
    with psycopg.connect(url,autocommit=False,connect_timeout=10) as conn:
        with conn.cursor() as cur:
            cur.execute('SELECT pg_advisory_xact_lock(704210,2)')
            cur.execute("SELECT to_regclass('integration.station_sync_state')")
            preexisting=cur.fetchone()[0] is not None
            cur.execute("SELECT to_regclass('integration.schema_migrations')")
            ledger_exists=cur.fetchone()[0] is not None
            if preexisting and not ledger_exists:
                raise RuntimeError('Existing pilot tables without tracked baseline: audit and baseline manually')
            cur.execute('CREATE SCHEMA IF NOT EXISTS integration')
            cur.execute('''CREATE TABLE IF NOT EXISTS integration.schema_migrations (
                version integer PRIMARY KEY, filename text NOT NULL,
                sha256 text NOT NULL, applied_at timestamptz NOT NULL DEFAULT now()
            )''')
            results=[]
            for version,name,digest,sql in plans:
                cur.execute('SELECT filename,sha256 FROM integration.schema_migrations WHERE version=%s',(version,))
                old=cur.fetchone()
                if old:
                    if old[0]!=name or old[1]!=digest:
                        raise RuntimeError(f'migration {version} changed since applied')
                    results.append({'version':version,'state':'existing'})
                    continue
                cur.execute(sql)
                cur.execute('INSERT INTO integration.schema_migrations(version,filename,sha256) VALUES (%s,%s,%s)',
                            (version,name,digest))
                results.append({'version':version,'state':'applied'})
        conn.commit()
        return results


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dsn',help='PostgreSQL connection string (prefer environment/secret store)')
    parser.add_argument('--dry-run',action='store_true')
    args=parser.parse_args()
    if not args.dry_run and not args.dsn:
        parser.error('--dsn required unless --dry-run')
    for row in apply(args.dsn or '',dry_run=args.dry_run):
        print(row)

if __name__=='__main__':main()
