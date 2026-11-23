"""Static PG migration contract tests: database execution requires PostgreSQL instance."""
from miltech_server.apply_pg_migrations import apply,normalized,MIGRATIONS


def test_pg_migration_manifest_contains_all_three_and_is_hash_anchored():
    rows=apply('',dry_run=True)
    assert [row['version'] for row in rows]==[1,2,3]
    assert len({row['sha256'] for row in rows})==3


def test_full_manufacturing_schema_contains_foreign_keys_and_immutable_evidence():
    txt=MIGRATIONS[1].read_text('utf-8')
    assert 'CREATE TABLE ops.station_assignments' in txt
    assert 'CREATE TABLE ops.unit_installations' in txt
    assert 'EXCLUDE USING gist' in txt
    assert 'CREATE TABLE quality.acceptance_records' in txt
    assert 'CREATE TRIGGER audit_events_immutable' in txt
    assert 'CREATE TRIGGER imported_runs_immutable' in txt
    assert 'FOREIGN KEY(work_order_id,route_version_id)' in txt
    assert 'CREATE TABLE integration.legacy_id_map' in txt


def test_001_can_be_wrapped_in_transaction_for_applier():
    original=MIGRATIONS[0].read_text('utf-8')
    migration=normalized(original)
    assert migration.startswith('--')
    assert not migration.endswith('COMMIT;')
