-- New common Station DB, without mutating existing runs.db, parameters.db, or ktma/registrar.db.
CREATE TABLE station_identity (
  singleton INTEGER PRIMARY KEY CHECK(singleton=1),
  station_id TEXT NOT NULL UNIQUE, station_code TEXT NOT NULL,
  site_code TEXT NOT NULL, created_at TEXT NOT NULL,
  CHECK(length(station_code)>0), CHECK(length(site_code)>0)
) STRICT;
CREATE TABLE config_profiles (
  revision INTEGER PRIMARY KEY CHECK(revision>0),
  sha256 TEXT NOT NULL CHECK(length(sha256)=64),
  body_json TEXT NOT NULL CHECK(json_valid(body_json)),
  received_at TEXT NOT NULL,
  state TEXT NOT NULL DEFAULT 'CACHED' CHECK(state IN ('CACHED','ACTIVE','SUPERSEDED')),
  activated_at TEXT
) STRICT;
CREATE UNIQUE INDEX ux_profile_active ON config_profiles(state) WHERE state='ACTIVE';
CREATE TABLE profile_interfaces (
  revision INTEGER NOT NULL REFERENCES config_profiles(revision) ON DELETE RESTRICT,
  alias TEXT NOT NULL, role TEXT NOT NULL CHECK(role IN ('enterprise','ot','service','loopback')),
  vlan_id INTEGER CHECK(vlan_id IS NULL OR vlan_id BETWEEN 1 AND 4094),
  ipv4_cidr TEXT NOT NULL, gateway TEXT,
  default_route INTEGER NOT NULL DEFAULT 0 CHECK(default_route IN (0,1)),
  skip_as_source INTEGER NOT NULL DEFAULT 0 CHECK(skip_as_source IN (0,1)),
  PRIMARY KEY(revision,alias,ipv4_cidr)
) STRICT;
CREATE TABLE profile_sockets (
  revision INTEGER NOT NULL REFERENCES config_profiles(revision) ON DELETE RESTRICT,
  name TEXT NOT NULL, protocol TEXT NOT NULL CHECK(protocol IN ('tcp','udp')),
  direction TEXT NOT NULL CHECK(direction IN ('listen','connect')),
  address TEXT NOT NULL, port INTEGER NOT NULL CHECK(port BETWEEN 1 AND 65535),
  interface_role TEXT NOT NULL, owner TEXT NOT NULL,
  PRIMARY KEY(revision,name)
) STRICT;
CREATE UNIQUE INDEX ux_local_listens ON profile_sockets(revision,protocol,address,port)
  WHERE direction='listen';
CREATE TABLE profile_resources (
  revision INTEGER NOT NULL REFERENCES config_profiles(revision) ON DELETE RESTRICT,
  resource_id TEXT NOT NULL, kind TEXT NOT NULL, owner TEXT NOT NULL,
  exclusive INTEGER NOT NULL DEFAULT 1 CHECK(exclusive IN (0,1)),
  PRIMARY KEY(revision,resource_id)
) STRICT;
CREATE TABLE equipment_bindings (
  resource_id TEXT PRIMARY KEY,
  station_id TEXT NOT NULL, hardware_kind TEXT NOT NULL,
  endpoint TEXT NOT NULL, device_serial TEXT,
  calibration_valid_until TEXT,
  updated_at TEXT NOT NULL,
  FOREIGN KEY(station_id) REFERENCES station_identity(station_id)
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
CREATE TABLE committed_run_refs (
  run_id TEXT PRIMARY KEY, source_run_id TEXT NOT NULL UNIQUE,
  attempt_id TEXT NOT NULL UNIQUE REFERENCES local_attempts(attempt_id),
  run_payload_sha256 TEXT NOT NULL CHECK(length(run_payload_sha256)=64),
  imported_at TEXT NOT NULL
) STRICT;
CREATE TABLE local_audit (
  event_id TEXT PRIMARY KEY, kind TEXT NOT NULL, aggregate_kind TEXT NOT NULL,
  aggregate_id TEXT NOT NULL, occurred_at TEXT NOT NULL,
  data_json TEXT NOT NULL CHECK(json_valid(data_json))
) STRICT;
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
