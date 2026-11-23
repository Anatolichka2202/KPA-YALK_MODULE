"""Read-only structural audit of pre-existing Station Core SQLite databases.

Does not migrate, insert, modify, or infer values. Absent per-delivery DBs are allowed.
"""
import argparse
import json
import sqlite3
from pathlib import Path

EXPECTED={
 'runs.db': {'test_runs':{'run_id','scenario_id','scenario_version','object_serial'},
             'run_steps':{'run_id'}, 'run_measurements':{'parameter_key'},'run_events':{'timestamp'}},
 'ktma/registrar.db': {'products':set(),'components':set(),
                       'product_components':set(), 'stage_attempts':set(), 'stage_runs':{'run_id'}},
 'parameters.db': {},
}


def inspect_file(path:Path, expected:dict)->dict:
    result={'path':str(path),'present':path.is_file()}
    if not path.is_file():return result
    db=sqlite3.connect(f'file:{path.resolve().as_posix()}?mode=ro',uri=True)
    try:
        tables={row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")}
        result['tables']={}
        result['schema_version']=db.execute('PRAGMA schema_version').fetchone()[0]
        result['missing_tables']=sorted(set(expected)-tables)
        for name in sorted(tables):
            columns={row[1] for row in db.execute('PRAGMA table_info("'+name.replace('"','""')+'")')}
            missing=sorted(expected.get(name,set())-columns)
            result['tables'][name]={'columns':sorted(columns),'missing_expected_columns':missing}
    finally:db.close()
    return result


def inspect_legacy_root(data_root:Path)->dict:
    root=Path(data_root)
    return {name:inspect_file(root/'db'/name,expected) for name,expected in EXPECTED.items()}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data-root',required=True)
    args=p.parse_args()
    print(json.dumps(inspect_legacy_root(Path(args.data_root)),ensure_ascii=False,indent=2))

if __name__=='__main__':main()
