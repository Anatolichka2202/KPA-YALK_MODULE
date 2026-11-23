"""Autonomous station operational SQLite; does NOT modify legacy RunStore/Registrar databases.

Transactions cover the local attempt + local change log. A separate existing outbox.db
is reconciled via bridge_run_to_outbox after Core has durably committed its run.
"""
from __future__ import annotations

import base64
import hashlib
import json
import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone, timedelta
from importlib.resources import files
from pathlib import Path
from typing import Callable
from uuid import UUID, uuid4

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

from miltech_server.contracts import Profile, RunPayload
from miltech_edge.preflight import validate_environment


def canonical(obj: dict | list) -> str:
    return json.dumps(obj, ensure_ascii=False, sort_keys=True, separators=(',', ':'))


def checksum(obj: dict | list) -> str:
    return hashlib.sha256(canonical(obj).encode('utf-8')).hexdigest()


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def parse_utc(value: str) -> datetime:
    dt = datetime.fromisoformat(value.replace('Z', '+00:00'))
    if dt.tzinfo is None:
        raise ValueError('timezone required')
    return dt.astimezone(timezone.utc)


def uuid_text(value: str) -> str:
    return str(UUID(value))


def _verify_assignment(assignment: dict, signature_b64: str, trusted_ed25519_public_key: bytes) -> str:
    """Verify immutable signed JSON from Enterprise. Key must be pinned externally."""
    if len(trusted_ed25519_public_key) != 32:
        raise ValueError('expected 32-byte Ed25519 public key')
    try:
        signature = base64.b64decode(signature_b64, validate=True)
        Ed25519PublicKey.from_public_bytes(trusted_ed25519_public_key).verify(
            signature, canonical(assignment).encode('utf-8'))
    except (InvalidSignature, ValueError) as exc:
        raise ValueError('offline assignment signature invalid') from exc
    return hashlib.sha256(trusted_ed25519_public_key).hexdigest()


class StationStore:
    def __init__(self, db_path: str | Path):
        self.path = Path(db_path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.conn = sqlite3.connect(str(self.path), timeout=30, isolation_level=None)
        self.conn.row_factory = sqlite3.Row
        self.conn.execute('PRAGMA foreign_keys=ON')
        self.conn.execute('PRAGMA journal_mode=WAL')
        self.conn.execute('PRAGMA synchronous=FULL')
        self.conn.execute('PRAGMA busy_timeout=30000')
        if self.conn.execute('PRAGMA foreign_keys').fetchone()[0] != 1:
            raise RuntimeError('SQLite foreign keys disabled')
        self.migrate()

    def close(self):
        self.conn.close()

    @contextmanager
    def atomic(self):
        self.conn.execute('BEGIN IMMEDIATE')
        try:
            yield
            self.conn.execute('COMMIT')
        except BaseException:
            self.conn.execute('ROLLBACK')
            raise

    def migrate(self):
        existing={row[0] for row in self.conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")}
        if existing and 'schema_migrations' not in existing:
            raise RuntimeError('refusing to add Station schema to an unrelated/legacy SQLite database')
        self.conn.execute('''CREATE TABLE IF NOT EXISTS schema_migrations (
            version INTEGER PRIMARY KEY, sha256 TEXT NOT NULL, applied_at TEXT NOT NULL
        ) STRICT''')
        root = files('miltech_edge.db')
        for version, name in [(1,'001_station.sql'),(2,'002_local_changes.sql')]:
            sql = root.joinpath(name).read_text(encoding='utf-8')
            digest = hashlib.sha256(sql.encode('utf-8')).hexdigest()
            old = self.conn.execute('SELECT sha256 FROM schema_migrations WHERE version=?',(version,)).fetchone()
            if old:
                if old['sha256'] != digest:
                    raise RuntimeError(f'Station schema migration {version} checksum mismatch')
                continue
            # executescript() commits prior transactions; explicit BEGIN/COMMIT in script ensures atomic DDL.
            script = ('BEGIN IMMEDIATE;\n'+sql+'\nINSERT INTO schema_migrations(version,sha256,applied_at) '
                      + f"VALUES ({version},'{digest}',strftime('%Y-%m-%dT%H:%M:%fZ','now'));\nCOMMIT;")
            try:
                self.conn.executescript(script)
            except BaseException:
                if self.conn.in_transaction:
                    self.conn.execute('ROLLBACK')
                raise

    def initialize(self, station_id: str, station_code: str, site_code: str):
        station_id = uuid_text(station_id)
        if not station_code or not site_code:
            raise ValueError('codes cannot be empty')
        with self.atomic():
            existing = self.conn.execute('SELECT * FROM station_identity WHERE singleton=1').fetchone()
            if existing:
                if (existing['station_id'],existing['station_code'],existing['site_code']) != (station_id,station_code,site_code):
                    raise ValueError('Station already enrolled: explicit re-provisioning required')
                return
            self.conn.execute('INSERT INTO station_identity VALUES (1,?,?,?,?)',
                              (station_id,station_code,site_code,utc_now()))
            self._audit('STATION_ENROLLED','station',station_id,{'station_code':station_code})

    def _identity(self):
        row=self.conn.execute('SELECT * FROM station_identity WHERE singleton=1').fetchone()
        if row is None:
            raise RuntimeError('Station not enrolled')
        return row

    def _audit(self,kind,aggregate_kind,aggregate_id,payload):
        self.conn.execute('INSERT INTO local_audit VALUES (?,?,?,?,?,?)',
                          (str(uuid4()),kind,aggregate_kind,aggregate_id,utc_now(),canonical(payload)))

    def cache_profile(self, profile_data: dict, revision: int, expected_sha256: str):
        if revision < 1:
            raise ValueError('revision must be positive')
        profile=Profile.model_validate(profile_data)
        normalized=profile.model_dump(mode='json')
        digest=checksum(normalized)
        identity=self._identity()
        if profile.station_code != identity['station_code']:
            raise ValueError('profile station_code mismatch')
        if digest != expected_sha256:
            raise ValueError('profile digest mismatch')
        with self.atomic():
            old=self.conn.execute('SELECT sha256 FROM config_profiles WHERE revision=?',(revision,)).fetchone()
            if old:
                if old['sha256']!=digest:
                    raise ValueError('existing profile revision is immutable')
                return {'status':'existing','revision':revision}
            latest=self.conn.execute('SELECT MAX(revision) FROM config_profiles').fetchone()[0]
            if latest is not None and revision <= latest:
                raise ValueError('profile revision rollback prohibited without controlled recovery')
            self.conn.execute('INSERT INTO config_profiles(revision,sha256,body_json,received_at) VALUES (?,?,?,?)',
                              (revision,digest,canonical(normalized),utc_now()))
            for iface in normalized['interfaces']:
                self.conn.execute('''INSERT INTO profile_interfaces
                    (revision,alias,role,vlan_id,ipv4_cidr,gateway,default_route,skip_as_source)
                    VALUES (?,?,?,?,?,?,?,?)''',
                    (revision,iface['alias'],iface['role'],iface.get('vlan_id'),iface['ipv4'],
                     iface.get('gateway'),int(iface.get('default_route',False)),int(iface.get('skip_as_source',False))))
            for sock in normalized['sockets']:
                self.conn.execute('''INSERT INTO profile_sockets
                    (revision,name,protocol,direction,address,port,interface_role,owner)
                    VALUES (?,?,?,?,?,?,?,?)''',
                    (revision,sock['name'],sock['protocol'],sock['direction'],sock['address'],
                     sock['port'],sock['interface_role'],sock['owner']))
            for resource in normalized['resources']:
                self.conn.execute('''INSERT INTO profile_resources VALUES (?,?,?,?,?)''',
                    (revision,resource['id'],resource['kind'],resource['owner'],int(resource['exclusive'])))
            self._audit('PROFILE_CACHED','profile',str(revision),{'sha256':digest})
        return {'status':'cached','revision':revision}

    def activate_profile(self, revision:int, observed:dict|None=None):
        """Local read-only preflight, not a Windows NIC/VLAN provisioning operation."""
        row=self.conn.execute('SELECT body_json FROM config_profiles WHERE revision=?',(revision,)).fetchone()
        if row is None: raise ValueError('unknown profile')
        profile=Profile.model_validate_json(row['body_json'])
        report=validate_environment(profile,observed)
        if not report['ready']:
            raise ValueError('station environment preflight failed')
        # Warnings (e.g. PORT_IN_USE) need ResourceBroker verification outside this DB layer.
        with self.atomic():
            old=self.conn.execute("SELECT revision FROM config_profiles WHERE state='ACTIVE'").fetchone()
            if old and old['revision']==revision:
                return report
            if old and revision < old['revision']:
                raise ValueError('profile rollback needs separately authorized recovery procedure')
            self.conn.execute("UPDATE config_profiles SET state='SUPERSEDED' WHERE state='ACTIVE'")
            self.conn.execute("UPDATE config_profiles SET state='ACTIVE',activated_at=? WHERE revision=?",
                              (utc_now(),revision))
            self.conn.execute('''INSERT INTO environment_snapshots VALUES (?,?,?,?,?)''',
                (str(uuid4()),revision,utc_now(),int(report['ready']),canonical(report)))
            self._audit('PROFILE_ACTIVATED','profile',str(revision),{'preflight':report})
        return report

    def record_broker_event(self, resource_id:str,owner:str,action:str,details:dict):
        """Record an actual ResourceBroker event; DB row itself is NOT an OS lock."""
        if action not in {'ACQUIRED','RELEASED','CONFLICT','EXPIRED'}:
            raise ValueError('unsupported broker action')
        if not resource_id or not owner or not isinstance(details,dict):
            raise ValueError('resource id, owner and details required')
        with self.atomic():
            self.conn.execute('INSERT INTO broker_resource_events VALUES (?,?,?,?,?,?)',
                              (str(uuid4()),resource_id,owner,action,utc_now(),canonical(details)))

    def cache_signed_assignment(self, payload:dict, signature_b64:str, trusted_ed25519_public_key:bytes):
        fingerprint=_verify_assignment(payload,signature_b64,trusted_ed25519_public_key)
        identity=self._identity()
        required=['assignment_id','station_id','unit_serial','operation_code','method_sha256','issued_at','valid_until']
        if any(k not in payload for k in required):
            raise ValueError('signed assignment required fields missing')
        aid=uuid_text(payload['assignment_id']); sid=uuid_text(payload['station_id'])
        if sid!=identity['station_id']:raise ValueError('assignment for different station')
        issue=parse_utc(payload['issued_at']); expiry=parse_utc(payload['valid_until'])
        if expiry<=datetime.now(timezone.utc) or expiry<=issue:
            raise ValueError('expired or invalid assignment')
        if issue>datetime.now(timezone.utc)+timedelta(minutes=5):
            raise ValueError('future-dated assignment')
        if len(payload['method_sha256'])!=64 or any(c not in '0123456789abcdef' for c in payload['method_sha256']):
            raise ValueError('invalid method digest')
        serial=payload['unit_serial']; op=payload['operation_code']
        if not isinstance(serial,str) or not serial or not isinstance(op,str) or not op:
            raise ValueError('invalid assignment identity')
        if payload.get('unit_id') is not None: uuid_text(payload['unit_id'])
        with self.atomic():
            old=self.conn.execute('SELECT body_json,signature_b64 FROM offline_assignments WHERE assignment_id=?',(aid,)).fetchone()
            if old:
                if old['body_json']!=canonical(payload) or old['signature_b64']!=signature_b64:
                    raise ValueError('assignment UUID reused with different signed content')
                return {'status':'existing','assignment_id':aid}
            self.conn.execute('''INSERT INTO offline_assignments
                (assignment_id,station_id,unit_id,unit_serial,operation_code,method_sha256,
                issued_at,valid_until,body_json,signature_b64,signer_fingerprint,received_at)
                VALUES (?,?,?,?,?,?,?,?,?,?,?,?)''',
                (aid,sid,payload.get('unit_id'),serial,op,payload['method_sha256'],payload['issued_at'],
                 payload['valid_until'],canonical(payload),signature_b64,fingerprint,utc_now()))
            self._audit('ASSIGNMENT_CACHED','assignment',aid,{'signer_fingerprint':fingerprint})
        return {'status':'cached','assignment_id':aid}

    def start_attempt(self,assignment_id:str,composition:list[dict], trusted_ed25519_public_key:bytes)->dict:
        """One immutable component snapshot per actual attempt, never resolved from current composition later."""
        aid=uuid_text(assignment_id)
        with self.atomic():
            assignment=self.conn.execute('SELECT * FROM offline_assignments WHERE assignment_id=?',(aid,)).fetchone()
            if assignment is None or assignment['state']!='ACTIVE':
                raise ValueError('assignment missing or withdrawn')
            payload=json.loads(assignment['body_json'])
            _verify_assignment(payload,assignment['signature_b64'],trusted_ed25519_public_key)
            if parse_utc(assignment['valid_until']) <= datetime.now(timezone.utc):
                raise ValueError('offline assignment expired')
            profile=self.conn.execute("SELECT revision FROM config_profiles WHERE state='ACTIVE'").fetchone()
            if profile is None:
                raise ValueError('no active validated station profile')
            if not isinstance(composition,list):raise ValueError('component composition must be list')
            for part in composition:
                if not isinstance(part,dict) or not part.get('component_type') or not part.get('serial_number'):
                    raise ValueError('each composition entry must identify component_type and serial_number')
            attempt_no=self.conn.execute('SELECT COALESCE(MAX(attempt_no),0)+1 FROM local_attempts WHERE assignment_id=?',(aid,)).fetchone()[0]
            attempt_id=str(uuid4())
            snapshot=canonical(composition)
            self.conn.execute('''INSERT INTO local_attempts
                (attempt_id,assignment_id,attempt_no,profile_revision,method_sha256,
                 composition_json,composition_sha256,state,started_at) VALUES (?,?,?,?,?,?,?,?,?)''',
                 (attempt_id,aid,attempt_no,profile['revision'],assignment['method_sha256'],snapshot,
                  hashlib.sha256(snapshot.encode('utf-8')).hexdigest(),'RUNNING',utc_now()))
            self._audit('ATTEMPT_STARTED','attempt',attempt_id,
                        {'assignment_id':aid,'attempt_no':attempt_no,'composition_sha256':checksum(composition)})
        return {'attempt_id':attempt_id,'attempt_no':attempt_no}

    def finish_attempt(self,attempt_id:str,run_payload:dict, durable_run_exists:Callable[[str],bool])->dict:
        """Called after Core commits a run. `durable_run_exists` is Core adapter, not a DB guess."""
        aid=uuid_text(attempt_id)
        run=RunPayload.model_validate(run_payload)
        data=run.model_dump(mode='json')
        run_id=str(run.run_id)
        if not durable_run_exists(run_id):
            raise ValueError('Core RunStore has not durably committed this run')
        payload_hash=checksum(data)
        state={'STAND_ERROR':'STAND_ERROR','STOPPED':'STOPPED'}.get(run.verdict,'COMPLETED')
        now=utc_now()
        with self.atomic():
            attempt=self.conn.execute('SELECT * FROM local_attempts WHERE attempt_id=?',(aid,)).fetchone()
            if attempt is None:raise ValueError('attempt missing')
            if attempt['state']!='RUNNING':raise ValueError('attempt already completed')
            assignment=self.conn.execute('SELECT unit_serial FROM offline_assignments WHERE assignment_id=?',(attempt['assignment_id'],)).fetchone()
            if data['unit_serial']!=assignment['unit_serial']:
                raise ValueError('run unit serial does not match assignment')
            if run.finished_at < run.started_at:
                raise ValueError('run finished before start')
            if checksum(data['composition_snapshot']) != attempt['composition_sha256']:
                raise ValueError('run composition differs from immutable attempt snapshot')
            self.conn.execute('''UPDATE local_attempts SET state=?,finished_at=?,run_id=?,run_verdict=?
                WHERE attempt_id=? AND state='RUNNING' ''',(state,now,run_id,run.verdict,aid))
            self.conn.execute('''INSERT INTO committed_run_refs VALUES (?,?,?,?,?)''',
                              (run_id,run.source_run_id,aid,payload_hash,now))
            change={'attempt_id':aid,'run_id':run_id,'verdict':run.verdict,'composition_sha256':attempt['composition_sha256']}
            ordinal=self.conn.execute('SELECT COALESCE(MAX(ordinal),0)+1 FROM local_changes').fetchone()[0]
            self.conn.execute('''INSERT INTO local_changes(change_id,ordinal,change_type,aggregate_id,
                payload_sha256,payload_json,occurred_at) VALUES (?,?,?,?,?,?,?)''',
                (str(uuid4()),ordinal,'ATTEMPT_COMPLETED',aid,checksum(change),canonical(change),now))
            self._audit('ATTEMPT_COMPLETED','attempt',aid,change)
        return {'attempt_id':aid,'run_id':run_id,'state':state,'change_sequence':ordinal}

    def bridge_run_to_outbox(self,run_payload:dict,outbox)->dict:
        """Idempotent replay into legacy Outbox from a durable Core run payload after a crash.

        This does not mark local_changes ACKED: production event API is not implemented yet.
        """
        run=RunPayload.model_validate(run_payload)
        normalized=run.model_dump(mode='json')
        row=self.conn.execute('SELECT run_payload_sha256 FROM committed_run_refs WHERE run_id=?',(str(run.run_id),)).fetchone()
        if row is None:raise ValueError('run reference not committed to station.db')
        if row['run_payload_sha256']!=checksum(normalized):
            raise ValueError('run immutable payload digest changed')
        return outbox.enqueue(normalized)

    def backup(self, destination:str|Path) -> dict:
        """Online consistent copy of THIS SQLite DB. Back up Core DBs separately, quiesced."""
        dest=Path(destination)
        if dest.resolve()==self.path.resolve():
            raise ValueError('backup destination must differ from active DB')
        dest.parent.mkdir(parents=True,exist_ok=True)
        if dest.exists():
            raise FileExistsError('backup already exists; never overwrite previous evidence')
        # SQLite backup API includes committed data in the WAL. A file copy does not.
        backup_conn=sqlite3.connect(str(dest))
        try:
            self.conn.backup(backup_conn)
            good=backup_conn.execute('PRAGMA integrity_check').fetchone()[0]
            if good!='ok':raise RuntimeError('backup integrity check failed')
        finally:
            backup_conn.close()
        return {'path':str(dest),'sqlite_integrity':'ok','size_bytes':dest.stat().st_size}

    def verify_integrity(self)->dict:
        check=self.conn.execute('PRAGMA integrity_check').fetchone()[0]
        bad=self.conn.execute('PRAGMA foreign_key_check').fetchall()
        return {'sqlite_integrity':check,'foreign_key_violations':len(bad),
                'schema_version':self.conn.execute('SELECT MAX(version) FROM schema_migrations').fetchone()[0]}
