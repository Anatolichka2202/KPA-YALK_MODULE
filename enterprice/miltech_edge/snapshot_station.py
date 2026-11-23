"""Back up a quiesced multi-DB station with sqlite3 online backup and a SHA-256 manifest.

No cross-DB transaction is possible with independently operating WAL databases.
The caller MUST put the station into maintenance/quiesced mode first.
"""
import argparse
import hashlib
import json
import sqlite3
from datetime import datetime,timezone
from pathlib import Path

RELATIVE_DBS = (
    ('parameters.db', 'db/parameters.db', False),
    ('runs.db', 'db/runs.db', True),
    ('registrar.db', 'db/ktma/registrar.db', False),
    ('station.db', 'db/edge/station.db', True),
    ('outbox.db', 'db/edge/outbox.db', True),
)


def snapshot_station(data_root:str|Path,destination:str|Path,*,quiesced:bool=False)->dict:
    if not quiesced:
        raise ValueError('maintenance quiescence confirmation required before multi-DB backup')
    root=Path(data_root).resolve();dest=Path(destination).resolve()
    if dest==root or root in dest.parents:
        raise ValueError('backup must be outside the station data root')
    if dest.exists():
        raise FileExistsError('destination exists; never overwrite old backups')
    for title,path,required in RELATIVE_DBS:
        if required and not (root/path).is_file():
            raise FileNotFoundError(f'required station DB missing: {path}')
    dest.mkdir(parents=True)
    manifest={'format_version':1,'created_at':datetime.now(timezone.utc).isoformat(),
              'quiescence_confirmed_by_caller':True,'databases':[]}
    for title,relative,required in RELATIVE_DBS:
        source=root/relative
        if not source.is_file():
            manifest['databases'].append({'name':title,'path':relative,'present':False,'optional':not required})
            continue
        target=dest/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        src=sqlite3.connect(f'file:{source.as_posix()}?mode=ro',uri=True)
        backup=sqlite3.connect(str(target))
        try:
            src.backup(backup)
            if backup.execute('PRAGMA integrity_check').fetchone()[0]!='ok':
                raise RuntimeError(f'integrity_check failed for {relative}')
            violations=backup.execute('PRAGMA foreign_key_check').fetchall()
            # Legacy tables may lack FKs; report findings without asserting zero for unknown DB.
        finally:
            src.close();backup.close()
        digest=hashlib.sha256(target.read_bytes()).hexdigest()
        manifest['databases'].append({'name':title,'path':relative,'present':True,'size_bytes':target.stat().st_size,
                                      'sha256':digest,'foreign_key_violations':len(violations)})
    (dest/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    return manifest


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data-root',required=True)
    p.add_argument('--destination',required=True)
    p.add_argument('--confirm-quiesced',action='store_true',help='I have stopped/persisted every station writer')
    a=p.parse_args()
    print(json.dumps(snapshot_station(a.data_root,a.destination,quiesced=a.confirm_quiesced),
                     ensure_ascii=False,indent=2))

if __name__=='__main__':main()
