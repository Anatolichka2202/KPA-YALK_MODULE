-- READ-ONLY REFERENCE: freshly generated empty station.db schema; use Python migrations on existing DBs.
BEGIN TRANSACTION;
CREATE TABLE broker_resource_events (
  id TEXT PRIMARY KEY, resource_id TEXT NOT NULL,
  owner TEXT NOT NULL, action TEXT NOT NULL CHECK(action IN ('ACQUIRED','RELEASED','CONFLICT','EXPIRED')),
  observed_at TEXT NOT NULL, details_json TEXT NOT NULL CHECK(json_valid(details_json))
) STRICT;
CREATE TABLE committed_run_refs (
  run_id TEXT PRIMARY KEY, source_run_id TEXT NOT NULL UNIQUE,
  attempt_id TEXT NOT NULL UNIQUE REFERENCES local_attempts(attempt_id),
  run_payload_sha256 TEXT NOT NULL CHECK(length(run_payload_sha256)=64),
  imported_at TEXT NOT NULL
) STRICT;
CREATE TABLE config_profiles (
  revision INTEGER PRIMARY KEY CHECK(revision>0),
  sha256 TEXT NOT NULL CHECK(length(sha256)=64),
  body_json TEXT NOT NULL CHECK(json_valid(body_json)),
  received_at TEXT NOT NULL,
  state TEXT NOT NULL DEFAULT 'CACHED' CHECK(state IN ('CACHED','ACTIVE','SUPERSEDED')),
  activated_at TEXT
) STRICT;
CREATE TABLE environment_snapshots (
  id TEXT PRIMARY KEY,
  profile_revision INTEGER NOT NULL REFERENCES config_profiles(revision),
  observed_at TEXT NOT NULL, ready INTEGER NOT NULL CHECK(ready IN (0,1)),
  report_json TEXT NOT NULL CHECK(json_valid(report_json))
) STRICT;
CREATE TABLE equipment_bindings (
  resource_id TEXT PRIMARY KEY,
  station_id TEXT NOT NULL, hardware_kind TEXT NOT NULL,
  endpoint TEXT NOT NULL, device_serial TEXT,
  calibration_valid_until TEXT,
  updated_at TEXT NOT NULL,
  FOREIGN KEY(station_id) REFERENCES station_identity(station_id)
) STRICT;
CREATE TABLE incoming_directives (
  directive_id TEXT PRIMARY KEY,
  revision INTEGER NOT NULL CHECK(revision>0),
  kind TEXT NOT NULL,
  body_json TEXT NOT NULL CHECK(json_valid(body_json)),
  body_sha256 TEXT NOT NULL CHECK(length(body_sha256)=64),
  received_at TEXT NOT NULL,
  applied_at TEXT,
  state TEXT NOT NULL DEFAULT 'RECEIVED' CHECK(state IN ('RECEIVED','APPLIED','REJECTED'))
) STRICT;
CREATE TABLE local_attempts (
  attempt_id TEXT PRIMARY KEY,
  assignment_id TEXT NOT NULL REFERENCES offline_assignments(assignment_id),
  attempt_no INTEGER NOT NULL CHECK(attempt_no>0),
  profile_revision INTEGER NOT NULL REFERENCES config_profiles(revision),
  method_sha256 TEXT NOT NULL CHECK(length(method_sha256)=64),
  composition_json TEXT NOT NULL CHECK(json_valid(composition_json)),
  composition_sha256 TEXT NOT NULL CHECK(length(composition_sha256)=64),
  state TEXT NOT NULL CHECK(state IN ('RUNNING','COMPLETED','STOPPED','STAND_ERROR')),
  started_at TEXT NOT NULL, finished_at TEXT,
  run_id TEXT UNIQUE, run_verdict TEXT,
  UNIQUE(assignment_id,attempt_no),
  CHECK((state='RUNNING' AND finished_at IS NULL AND run_id IS NULL AND run_verdict IS NULL)
     OR (state<>'RUNNING' AND finished_at IS NOT NULL AND run_id IS NOT NULL AND run_verdict IS NOT NULL))
) STRICT;
CREATE TABLE local_audit (
  event_id TEXT PRIMARY KEY, kind TEXT NOT NULL, aggregate_kind TEXT NOT NULL,
  aggregate_id TEXT NOT NULL, occurred_at TEXT NOT NULL,
  data_json TEXT NOT NULL CHECK(json_valid(data_json))
) STRICT;
CREATE TABLE local_changes (
  change_id TEXT PRIMARY KEY,
  ordinal INTEGER NOT NULL UNIQUE,
  change_type TEXT NOT NULL,
  aggregate_id TEXT NOT NULL,
  payload_sha256 TEXT NOT NULL CHECK(length(payload_sha256)=64),
  payload_json TEXT NOT NULL CHECK(json_valid(payload_json)),
  occurred_at TEXT NOT NULL,
  export_state TEXT NOT NULL DEFAULT 'PENDING' CHECK(export_state IN ('PENDING','ACKED')),
  exported_at TEXT
) STRICT;
CREATE TABLE offline_assignments (
  assignment_id TEXT PRIMARY KEY, station_id TEXT NOT NULL,
  unit_id TEXT, unit_serial TEXT NOT NULL,
  operation_code TEXT NOT NULL, method_sha256 TEXT NOT NULL CHECK(length(method_sha256)=64),
  issued_at TEXT NOT NULL, valid_until TEXT NOT NULL,
  body_json TEXT NOT NULL CHECK(json_valid(body_json)),
  signature_b64 TEXT NOT NULL, signer_fingerprint TEXT NOT NULL,
  received_at TEXT NOT NULL,
  state TEXT NOT NULL DEFAULT 'ACTIVE' CHECK(state IN ('ACTIVE','WITHDRAWN')),
  FOREIGN KEY(station_id) REFERENCES station_identity(station_id)
) STRICT;
CREATE TABLE profile_interfaces (
  revision INTEGER NOT NULL REFERENCES config_profiles(revision) ON DELETE RESTRICT,
  alias TEXT NOT NULL, role TEXT NOT NULL CHECK(role IN ('enterprise','ot','service','loopback')),
  vlan_id INTEGER CHECK(vlan_id IS NULL OR vlan_id BETWEEN 1 AND 4094),
  ipv4_cidr TEXT NOT NULL, gateway TEXT,
  default_route INTEGER NOT NULL DEFAULT 0 CHECK(default_route IN (0,1)),
  skip_as_source INTEGER NOT NULL DEFAULT 0 CHECK(skip_as_source IN (0,1)),
  PRIMARY KEY(revision,alias,ipv4_cidr)
) STRICT;
CREATE TABLE profile_resources (
  revision INTEGER NOT NULL REFERENCES config_profiles(revision) ON DELETE RESTRICT,
  resource_id TEXT NOT NULL, kind TEXT NOT NULL, owner TEXT NOT NULL,
  exclusive INTEGER NOT NULL DEFAULT 1 CHECK(exclusive IN (0,1)),
  PRIMARY KEY(revision,resource_id)
) STRICT;
CREATE TABLE profile_sockets (
  revision INTEGER NOT NULL REFERENCES config_profiles(revision) ON DELETE RESTRICT,
  name TEXT NOT NULL, protocol TEXT NOT NULL CHECK(protocol IN ('tcp','udp')),
  direction TEXT NOT NULL CHECK(direction IN ('listen','connect')),
  address TEXT NOT NULL, port INTEGER NOT NULL CHECK(port BETWEEN 1 AND 65535),
  interface_role TEXT NOT NULL, owner TEXT NOT NULL,
  PRIMARY KEY(revision,name)
) STRICT;
CREATE TABLE schema_migrations (
            version INTEGER PRIMARY KEY, sha256 TEXT NOT NULL, applied_at TEXT NOT NULL
        ) STRICT;
INSERT INTO "schema_migrations" VALUES(1,'194eff1eb08d46e86a08cbf36ddb5be52e1b205a25c781cfe8ef5c0f4a1b0bc5','2026-11-23T11:32:14.516Z');
INSERT INTO "schema_migrations" VALUES(2,'ac4845bd0d1670dda853bfa65066beb8de7c026eebcf4314c55f9a4ce968dc14','2026-11-23T11:32:14.518Z');
CREATE TABLE station_identity (
  singleton INTEGER PRIMARY KEY CHECK(singleton=1),
  station_id TEXT NOT NULL UNIQUE, station_code TEXT NOT NULL,
  site_code TEXT NOT NULL, created_at TEXT NOT NULL,
  CHECK(length(station_code)>0), CHECK(length(site_code)>0)
) STRICT;
CREATE UNIQUE INDEX ux_profile_active ON config_profiles(state) WHERE state='ACTIVE';
CREATE UNIQUE INDEX ux_local_listens ON profile_sockets(revision,protocol,address,port)
  WHERE direction='listen';
CREATE TRIGGER local_audit_no_update BEFORE UPDATE ON local_audit
  BEGIN SELECT RAISE(ABORT,'local_audit is append-only'); END;
CREATE TRIGGER local_audit_no_delete BEFORE DELETE ON local_audit
  BEGIN SELECT RAISE(ABORT,'local_audit is append-only'); END;
CREATE TRIGGER attempts_snapshot_no_update BEFORE UPDATE OF
  assignment_id,profile_revision,method_sha256,composition_json,composition_sha256,attempt_no
  ON local_attempts BEGIN SELECT RAISE(ABORT,'attempt context is immutable'); END;
CREATE TRIGGER attempts_terminal_no_update BEFORE UPDATE ON local_attempts
  WHEN OLD.state<>'RUNNING' BEGIN SELECT RAISE(ABORT,'completed attempt is immutable'); END;
CREATE TRIGGER attempts_no_delete BEFORE DELETE ON local_attempts
  BEGIN SELECT RAISE(ABORT,'attempt is append-only'); END;
CREATE TRIGGER committed_runs_no_update BEFORE UPDATE ON committed_run_refs
  BEGIN SELECT RAISE(ABORT,'committed run reference is immutable'); END;
CREATE TRIGGER committed_runs_no_delete BEFORE DELETE ON committed_run_refs
  BEGIN SELECT RAISE(ABORT,'committed run reference is immutable'); END;
CREATE TRIGGER profile_body_no_update BEFORE UPDATE OF sha256,body_json,received_at,revision ON config_profiles
  BEGIN SELECT RAISE(ABORT,'profile payload is immutable'); END;
CREATE INDEX ix_local_changes_pending ON local_changes(export_state,ordinal);
CREATE TRIGGER local_changes_immutable_payload BEFORE UPDATE OF
  ordinal,change_type,aggregate_id,payload_sha256,payload_json,occurred_at
  ON local_changes BEGIN SELECT RAISE(ABORT,'local change payload is immutable'); END;
CREATE TRIGGER local_changes_no_delete BEFORE DELETE ON local_changes
  BEGIN SELECT RAISE(ABORT,'local change cannot be deleted'); END;
CREATE INDEX ix_directives_state ON incoming_directives(state,revision);
CREATE TRIGGER assignments_immutable_body BEFORE UPDATE OF
  station_id,unit_id,unit_serial,operation_code,method_sha256,issued_at,valid_until,
  body_json,signature_b64,signer_fingerprint,received_at ON offline_assignments
  BEGIN SELECT RAISE(ABORT,'signed assignment payload is immutable'); END;
CREATE TRIGGER assignments_no_delete BEFORE DELETE ON offline_assignments
  BEGIN SELECT RAISE(ABORT,'assignment removal prohibited; withdraw explicitly'); END;
CREATE INDEX ix_environment_snapshots_time ON environment_snapshots(observed_at DESC);
CREATE TRIGGER broker_events_no_update BEFORE UPDATE ON broker_resource_events
  BEGIN SELECT RAISE(ABORT,'broker resource history is append-only'); END;
CREATE TRIGGER broker_events_no_delete BEFORE DELETE ON broker_resource_events
  BEGIN SELECT RAISE(ABORT,'broker resource history is append-only'); END;
COMMIT;
