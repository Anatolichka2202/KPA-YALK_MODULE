-- Durable operation-change log in same SQLite transaction as local attempt completion.
-- Outgoing RunCompleted remains in existing outbox.db (separate recovery bridge).
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
CREATE INDEX ix_local_changes_pending ON local_changes(export_state,ordinal);
CREATE TRIGGER local_changes_immutable_payload BEFORE UPDATE OF
  ordinal,change_type,aggregate_id,payload_sha256,payload_json,occurred_at
  ON local_changes BEGIN SELECT RAISE(ABORT,'local change payload is immutable'); END;
CREATE TRIGGER local_changes_no_delete BEFORE DELETE ON local_changes
  BEGIN SELECT RAISE(ABORT,'local change cannot be deleted'); END;
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
CREATE INDEX ix_directives_state ON incoming_directives(state,revision);

CREATE TRIGGER assignments_immutable_body BEFORE UPDATE OF
  station_id,unit_id,unit_serial,operation_code,method_sha256,issued_at,valid_until,
  body_json,signature_b64,signer_fingerprint,received_at ON offline_assignments
  BEGIN SELECT RAISE(ABORT,'signed assignment payload is immutable'); END;
CREATE TRIGGER assignments_no_delete BEFORE DELETE ON offline_assignments
  BEGIN SELECT RAISE(ABORT,'assignment removal prohibited; withdraw explicitly'); END;
CREATE TABLE environment_snapshots (
  id TEXT PRIMARY KEY,
  profile_revision INTEGER NOT NULL REFERENCES config_profiles(revision),
  observed_at TEXT NOT NULL, ready INTEGER NOT NULL CHECK(ready IN (0,1)),
  report_json TEXT NOT NULL CHECK(json_valid(report_json))
) STRICT;
CREATE INDEX ix_environment_snapshots_time ON environment_snapshots(observed_at DESC);
CREATE TABLE broker_resource_events (
  id TEXT PRIMARY KEY, resource_id TEXT NOT NULL,
  owner TEXT NOT NULL, action TEXT NOT NULL CHECK(action IN ('ACQUIRED','RELEASED','CONFLICT','EXPIRED')),
  observed_at TEXT NOT NULL, details_json TEXT NOT NULL CHECK(json_valid(details_json))
) STRICT;
CREATE TRIGGER broker_events_no_update BEFORE UPDATE ON broker_resource_events
  BEGIN SELECT RAISE(ABORT,'broker resource history is append-only'); END;
CREATE TRIGGER broker_events_no_delete BEFORE DELETE ON broker_resource_events
  BEGIN SELECT RAISE(ABORT,'broker resource history is append-only'); END;
