"""Real SQLite tests: FK, WAL, immutable evidence, offline signatures, migration and recovery."""
import base64
import json
import sqlite3
from copy import deepcopy
from datetime import datetime, timezone, timedelta
from pathlib import Path
from uuid import uuid4

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

from miltech_edge.station_store import StationStore,canonical,checksum
from miltech_edge.outbox import Outbox


@pytest.fixture
def edge(tmp_path):
    store=StationStore(tmp_path/'edge'/'station.db')
    station_id=str(uuid4())
    store.initialize(station_id,'STAND-01','FACTORY-01')
    profile={'schema_version':1,'station_code':'STAND-01',
             'interfaces':[{'role':'loopback','alias':'Loopback','ipv4':'127.0.0.1/8'}],
             'sockets':[],'resources':[],'required_capabilities':[]}
    from miltech_server.contracts import Profile
    normalized=Profile.model_validate(profile).model_dump(mode='json')
    store.cache_profile(profile,1,checksum(normalized))
    store.activate_profile(1,observed={'platform':'test','interfaces':{},'prefixes':{},'sockets':[]})
    private=Ed25519PrivateKey.generate()
    public=private.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
    assignment={
       'assignment_id':str(uuid4()),'station_id':station_id,
       'unit_serial':'KTMA-0007','unit_id':str(uuid4()),
       'operation_code':'PRIMARY','method_sha256':'e'*64,
       'issued_at':(datetime.now(timezone.utc)-timedelta(minutes=2)).isoformat(),
       'valid_until':(datetime.now(timezone.utc)+timedelta(days=3)).isoformat(),
    }
    def signed(payload=assignment):
        return base64.b64encode(private.sign(canonical(payload).encode('utf-8'))).decode('ascii')
    store.cache_signed_assignment(assignment,signed(),public)
    yield store,station_id,assignment,signed,public,tmp_path
    store.close()


def sample_run(comp,serial='KTMA-0007'):
    now=datetime.now(timezone.utc)
    return {'schema_version':1,'run_id':str(uuid4()),'source_run_id':'RUN-'+uuid4().hex,
            'unit_serial':serial,'unit_model':'KTMA','scenario_id':'stage-primary',
            'scenario_version':'1','catalog_version':'1','profile_version':'1','software_release':'0.1',
            'started_at':(now-timedelta(seconds=4)).isoformat(),'finished_at':now.isoformat(),
            'verdict':'NORM','composition_snapshot':comp,'artifacts':[],'clock_status':'SYNCED'}


def test_db_is_versioned_and_reopens(edge):
    store,*_=edge
    assert store.verify_integrity()=={'sqlite_integrity':'ok','foreign_key_violations':0,'schema_version':2}
    other=StationStore(store.path)
    assert other.verify_integrity()['schema_version']==2
    other.close()
    assert store.conn.execute('PRAGMA journal_mode').fetchone()[0]=='wal'
    assert store.conn.execute('PRAGMA foreign_keys').fetchone()[0]==1


def test_station_cannot_silently_change_enrollment(edge):
    store,*_=edge
    with pytest.raises(ValueError,match='re-provisioning'):
        store.initialize(str(uuid4()),'STAND-99','FACTORY-01')


def test_signature_and_expiry_are_verified(edge):
    store,sid,a,sign,pub,_=edge
    changed=dict(a,unit_serial='OTHER')
    with pytest.raises(ValueError,match='signature invalid'):
        store.cache_signed_assignment(changed,sign(),pub)
    expired=dict(a,assignment_id=str(uuid4()),valid_until=(datetime.now(timezone.utc)-timedelta(hours=1)).isoformat())
    with pytest.raises(ValueError,match='expired'):
        store.cache_signed_assignment(expired,sign(expired),pub)
    assert store.cache_signed_assignment(a,sign(),pub)['status']=='existing'


def test_attempt_snapshot_run_bridge_and_idempotency(edge):
    store,sid,a,sign,pub,tmp=edge
    comp=[{'component_type':'YSM','serial_number':'YSM-44','slot':'A'}]
    t=store.start_attempt(a['assignment_id'],comp,pub)
    assert t['attempt_no']==1
    run=sample_run(comp)
    with pytest.raises(ValueError,match='durably committed'):
        store.finish_attempt(t['attempt_id'],run,lambda run_id:False)
    assert store.conn.execute("SELECT state FROM local_attempts WHERE attempt_id=?",(t['attempt_id'],)).fetchone()[0]=='RUNNING'
    with pytest.raises(ValueError,match='composition differs'):
        store.finish_attempt(t['attempt_id'],dict(run,composition_snapshot=[]),lambda _:True)
    done=store.finish_attempt(t['attempt_id'],run,lambda rid:rid==run['run_id'])
    assert done['state']=='COMPLETED'
    assert store.conn.execute('SELECT COUNT(*) FROM local_changes').fetchone()[0]==1
    assert store.conn.execute('SELECT COUNT(*) FROM committed_run_refs').fetchone()[0]==1
    outbox=Outbox(str(tmp/'sync'/'outbox.db'))
    try:
        first=store.bridge_run_to_outbox(run,outbox)
        second=store.bridge_run_to_outbox(run,outbox)
        assert first['status']=='queued' and second['status']=='existing'
        assert len(outbox.pending())==1
        modified=dict(run,verdict='NOT_NORM')
        with pytest.raises(ValueError,match='digest changed'):
            store.bridge_run_to_outbox(modified,outbox)
    finally:outbox.close()
    with pytest.raises(sqlite3.IntegrityError,match='immutable'):
        store.conn.execute('UPDATE local_attempts SET composition_json=? WHERE attempt_id=?',
                           ('[]',t['attempt_id']))
    with pytest.raises(sqlite3.IntegrityError,match='immutable'):
        store.conn.execute('DELETE FROM committed_run_refs WHERE run_id=?',(run['run_id'],))
    with pytest.raises(sqlite3.IntegrityError,match='append-only'):
        store.conn.execute('DELETE FROM local_audit')


def test_wrong_unit_and_duplicate_run_blocked(edge):
    store,sid,a,_,pub,tmp=edge
    t=store.start_attempt(a['assignment_id'],[],pub)
    with pytest.raises(ValueError,match='serial does not match'):
        store.finish_attempt(t['attempt_id'],sample_run([],serial='OTHER'),lambda _:True)
    run=sample_run([])
    store.finish_attempt(t['attempt_id'],run,lambda _:True)
    t2=store.start_attempt(a['assignment_id'],[],pub)
    with pytest.raises(sqlite3.IntegrityError):
        store.finish_attempt(t2['attempt_id'],run,lambda _:True)
    assert store.conn.execute('SELECT state FROM local_attempts WHERE attempt_id=?',(t2['attempt_id'],)).fetchone()[0]=='RUNNING'


def test_profile_immutable_and_rollback_blocked(edge):
    store,sid,a,_,pub,tmp=edge
    with pytest.raises(sqlite3.IntegrityError,match='immutable'):
        store.conn.execute("UPDATE config_profiles SET sha256=? WHERE revision=1",('f'*64,))
    from miltech_server.contracts import Profile
    profile={'station_code':'STAND-01','interfaces':[{'role':'loopback','alias':'Loopback','ipv4':'127.0.0.1/8'}]}
    normalized=Profile.model_validate(profile).model_dump(mode='json')
    store.cache_profile(profile,2,checksum(normalized))
    store.activate_profile(2,observed={'platform':'test','interfaces':{},'prefixes':{},'sockets':[]})
    with pytest.raises(ValueError,match='rollback'):
        store.activate_profile(1,observed={'platform':'test','interfaces':{},'prefixes':{},'sockets':[]})


def test_sqlite_fk_rejects_broken_attempt(edge):
    store,*_=edge
    with pytest.raises(sqlite3.IntegrityError):
        store.conn.execute('''INSERT INTO committed_run_refs VALUES (?,?,?,?,?)''',
            (str(uuid4()),'SOURCE-ABSENT',str(uuid4()),'b'*64,'2026-01-01T00:00:00Z'))


def test_schema_consistency_against_legacy_outbox():
    from miltech_edge.outbox import Outbox
    import tempfile
    with tempfile.TemporaryDirectory() as root:
        db=Outbox(root+'/outbox.db')
        try:
            tables={row[0] for row in db.conn.execute("SELECT name FROM sqlite_master WHERE type='table'")}
            assert 'outbox' in tables and 'station_identity' not in tables
        finally: db.close()


def test_online_backup_includes_wal_and_preserves_integrity(edge):
    store,sid,assignment,sign,pub,tmp=edge
    store.start_attempt(assignment['assignment_id'],[{'component_type':'YFK','serial_number':'YK-44'}],pub)
    destination=tmp/'backup'/'station-backup.db'
    receipt=store.backup(destination)
    assert receipt['sqlite_integrity']=='ok' and receipt['size_bytes']>0
    copy=StationStore(destination)
    try:
        assert copy.verify_integrity()['foreign_key_violations']==0
        assert copy.conn.execute('SELECT COUNT(*) FROM local_attempts').fetchone()[0]==1
    finally:copy.close()
    with pytest.raises(FileExistsError):store.backup(destination)


def test_environment_snapshots_and_broker_events(edge):
    store,*_=edge
    # Profile activation records preflight observation and audit.
    assert store.conn.execute('SELECT COUNT(*) FROM environment_snapshots').fetchone()[0]==1
    store.record_broker_event('COM7','ktma-yfk','ACQUIRED',{'pid':1234,'os_lock':True})
    assert store.conn.execute('SELECT action FROM broker_resource_events').fetchone()[0]=='ACQUIRED'
    with pytest.raises(sqlite3.IntegrityError,match='append-only'):
        store.conn.execute('DELETE FROM broker_resource_events')


def test_quiescent_multi_db_backup(tmp_path):
    from miltech_edge.snapshot_station import snapshot_station
    root=tmp_path/'station-root'
    (root/'db/edge').mkdir(parents=True)
    (root/'db').mkdir(exist_ok=True)
    # Simulate legacy RunStore persisted DB without pretending to know its full tables.
    core=sqlite3.connect(root/'db/runs.db')
    core.execute('CREATE TABLE test_runs(run_id TEXT PRIMARY KEY)')
    core.execute("INSERT INTO test_runs VALUES ('r-1')")
    core.commit();core.close()
    station=StationStore(root/'db/edge/station.db');station.close()
    box=Outbox(str(root/'db/edge/outbox.db'));box.close()
    with pytest.raises(ValueError,match='quiescence'):
        snapshot_station(root,tmp_path/'snapshot',quiesced=False)
    manifest=snapshot_station(root,tmp_path/'snapshot',quiesced=True)
    assert len([x for x in manifest['databases'] if x['present']])==3
    assert (tmp_path/'snapshot/db/runs.db').is_file()
    with pytest.raises(FileExistsError):
        snapshot_station(root,tmp_path/'snapshot',quiesced=True)


def test_legacy_audit_is_read_only_and_reports_unknown_schema(tmp_path):
    from miltech_edge.legacy_probe import inspect_legacy_root
    root=tmp_path/'station'; (root/'db').mkdir(parents=True)
    db=sqlite3.connect(root/'db/runs.db')
    db.execute('CREATE TABLE test_runs(run_id TEXT PRIMARY KEY, scenario_id TEXT,scenario_version TEXT,object_serial TEXT)')
    db.commit();db.close()
    before=(root/'db/runs.db').read_bytes()
    report=inspect_legacy_root(root)
    assert report['runs.db']['present'] is True
    assert 'run_steps' in report['runs.db']['missing_tables']
    assert report['ktma/registrar.db']['present'] is False
    assert (root/'db/runs.db').read_bytes()==before


def test_existing_slice1_outbox_is_upgraded_without_deletion(tmp_path):
    from miltech_edge.outbox import DDL
    file=tmp_path/'old-outbox.sqlite'
    db=sqlite3.connect(file)
    db.executescript(DDL)
    db.execute("INSERT INTO outbox(event_id,payload) VALUES ('old-id','{\"source_run_id\":\"S1\"}')")
    db.commit();db.close()
    box=Outbox(str(file))
    try:
        assert box.conn.execute('PRAGMA user_version').fetchone()[0]==1
        assert box.conn.execute('SELECT event_id FROM outbox').fetchone()[0]=='old-id'
    finally:box.close()


def test_never_mount_outbox_or_legacy_runstore_as_station_db(tmp_path):
    from miltech_edge.station_store import StationStore
    file=tmp_path/'outbox.db'
    out=Outbox(str(file));out.close()
    with pytest.raises(RuntimeError,match='unrelated/legacy'):
        StationStore(file)
    db=sqlite3.connect(file)
    try:
        names={row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        assert 'station_identity' not in names
    finally:db.close()
