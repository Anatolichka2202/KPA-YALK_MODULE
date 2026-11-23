-- MilTech Station Enterprise: production data model, extends 001_edge_pilot_postgres.sql
-- PostgreSQL 16+. Apply with scripts/apply_pg_migrations.py AFTER migration 001.
-- DO NOT apply reference/postgres_schema_v0_1.sql: it overlaps these existing schemas.
CREATE SCHEMA IF NOT EXISTS identity;
CREATE SCHEMA IF NOT EXISTS catalog;
CREATE SCHEMA IF NOT EXISTS ops;
CREATE SCHEMA IF NOT EXISTS quality;
CREATE SCHEMA IF NOT EXISTS audit;
CREATE EXTENSION IF NOT EXISTS btree_gist;

CREATE TABLE identity.users (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), external_subject text UNIQUE,
  display_name text NOT NULL, active boolean NOT NULL DEFAULT true,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE identity.roles (code text PRIMARY KEY, description text NOT NULL);
CREATE TABLE identity.user_roles (
  user_id uuid NOT NULL REFERENCES identity.users(id),
  role_code text NOT NULL REFERENCES identity.roles(code),
  PRIMARY KEY(user_id,role_code)
);

CREATE TABLE catalog.product_models (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), code text NOT NULL UNIQUE,
  title text NOT NULL, serialized boolean NOT NULL DEFAULT true
);
CREATE TABLE catalog.model_revisions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), model_id uuid NOT NULL REFERENCES catalog.product_models(id),
  revision_code text NOT NULL, approved_at timestamptz, specification_ref text,
  UNIQUE(model_id,revision_code), UNIQUE(id,model_id)
);
CREATE TABLE catalog.method_revisions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), method_code text NOT NULL,
  revision_code text NOT NULL, specification_ref text NOT NULL,
  package_sha256 char(64) NOT NULL CHECK(package_sha256 ~ '^[0-9a-f]{64}$'),
  approved_at timestamptz, UNIQUE(method_code,revision_code)
);
CREATE TABLE catalog.route_versions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), model_revision_id uuid NOT NULL REFERENCES catalog.model_revisions(id),
  code text NOT NULL, revision_code text NOT NULL, approved_at timestamptz,
  UNIQUE(model_revision_id,code,revision_code), UNIQUE(id,model_revision_id)
);
CREATE TABLE catalog.route_operations (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), route_version_id uuid NOT NULL REFERENCES catalog.route_versions(id),
  code text NOT NULL, sequence_no integer NOT NULL CHECK(sequence_no>0),
  operation_kind text NOT NULL CHECK(operation_kind IN ('ASSEMBLY','INSPECTION','TEST','REWORK','ACCEPTANCE','OTHER')),
  method_revision_id uuid REFERENCES catalog.method_revisions(id),
  mandatory boolean NOT NULL DEFAULT true, required_capabilities jsonb NOT NULL DEFAULT '[]'::jsonb,
  UNIQUE(route_version_id,code), UNIQUE(route_version_id,sequence_no), UNIQUE(id,route_version_id)
);

CREATE TABLE infra.equipment (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), site_id uuid NOT NULL REFERENCES infra.sites(id),
  equipment_kind text NOT NULL, manufacturer text, model text, serial_number text NOT NULL,
  inventory_number text, enabled boolean NOT NULL DEFAULT true,
  UNIQUE(site_id,equipment_kind,serial_number)
);
CREATE TABLE infra.equipment_calibration (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), equipment_id uuid NOT NULL REFERENCES infra.equipment(id),
  kind text NOT NULL CHECK(kind IN ('CALIBRATION','VERIFICATION','INSPECTION')),
  valid_from timestamptz NOT NULL, valid_until timestamptz,
  state text NOT NULL CHECK(state IN ('VALID','INVALID','CANCELLED')),
  doc_ref text, doc_sha256 char(64) CHECK(doc_sha256 IS NULL OR doc_sha256 ~ '^[0-9a-f]{64}$'),
  CHECK(valid_until IS NULL OR valid_until>valid_from), UNIQUE(id,equipment_id)
);
CREATE INDEX ix_equipment_calibration_expiry ON infra.equipment_calibration(equipment_id,valid_until DESC);
CREATE TABLE infra.station_equipment (
  station_id uuid NOT NULL REFERENCES infra.stations(id),
  equipment_id uuid NOT NULL REFERENCES infra.equipment(id),
  assigned_at timestamptz NOT NULL, detached_at timestamptz,
  CHECK(detached_at IS NULL OR detached_at>assigned_at),
  PRIMARY KEY(station_id,equipment_id,assigned_at)
);
CREATE UNIQUE INDEX ux_station_equipment_active ON infra.station_equipment(equipment_id) WHERE detached_at IS NULL;

CREATE TABLE ops.units (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  model_id uuid NOT NULL REFERENCES catalog.product_models(id),
  model_revision_id uuid NOT NULL,
  site_id uuid NOT NULL REFERENCES infra.sites(id),
  serial_number text NOT NULL,
  registered_at timestamptz NOT NULL DEFAULT now(),
  registered_by uuid REFERENCES identity.users(id),
  FOREIGN KEY(model_revision_id,model_id) REFERENCES catalog.model_revisions(id,model_id),
  UNIQUE(model_id,serial_number), UNIQUE(id,model_revision_id)
);
CREATE INDEX ix_units_sn ON ops.units(serial_number);
CREATE TABLE ops.unit_installations (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  parent_unit_id uuid NOT NULL REFERENCES ops.units(id),
  component_unit_id uuid NOT NULL REFERENCES ops.units(id),
  slot_code text NOT NULL, installed_at timestamptz NOT NULL,
  removed_at timestamptz, removal_reason text,
  recorded_by uuid REFERENCES identity.users(id),
  CHECK(parent_unit_id<>component_unit_id),
  CHECK(removed_at IS NULL OR removed_at>installed_at),
  EXCLUDE USING gist (component_unit_id WITH =, tstzrange(installed_at,COALESCE(removed_at,'infinity'::timestamptz),'[)') WITH &&),
  EXCLUDE USING gist (parent_unit_id WITH =, slot_code WITH =, tstzrange(installed_at,COALESCE(removed_at,'infinity'::timestamptz),'[)') WITH &&)
);
CREATE INDEX ix_unit_installations_parent ON ops.unit_installations(parent_unit_id,installed_at DESC);
CREATE TABLE ops.work_orders (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  site_id uuid NOT NULL REFERENCES infra.sites(id), code text NOT NULL,
  route_version_id uuid NOT NULL REFERENCES catalog.route_versions(id),
  external_ref text, state text NOT NULL CHECK(state IN ('PLANNED','OPEN','HOLD','CLOSED','CANCELLED')),
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE(site_id,code), UNIQUE(id,route_version_id)
);
CREATE TABLE ops.work_order_units (
  work_order_id uuid NOT NULL REFERENCES ops.work_orders(id),
  unit_id uuid NOT NULL REFERENCES ops.units(id), assigned_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(work_order_id,unit_id)
);
-- Signed dispatch can be cached at a station and executed during loss of connection.
-- Signature covers canonical JSON: assignment_id, station_id, unit_serial,
-- operation_code, method_sha256, issued_at, valid_until and optional unit_id.
CREATE TABLE ops.station_assignments (
  assignment_id uuid PRIMARY KEY,
  station_id uuid NOT NULL REFERENCES infra.stations(id),
  work_order_id uuid NOT NULL, unit_id uuid NOT NULL,
  route_version_id uuid NOT NULL, route_operation_id uuid NOT NULL,
  profile_revision integer NOT NULL,
  payload_json jsonb NOT NULL, payload_sha256 char(64) NOT NULL
      CHECK(payload_sha256 ~ '^[0-9a-f]{64}$'),
  signature_b64 text NOT NULL, signer_key_sha256 char(64) NOT NULL
      CHECK(signer_key_sha256 ~ '^[0-9a-f]{64}$'),
  issued_at timestamptz NOT NULL, valid_until timestamptz NOT NULL,
  revoked_at timestamptz,
  issued_by uuid REFERENCES identity.users(id),
  CHECK(valid_until>issued_at),
  FOREIGN KEY(work_order_id,unit_id) REFERENCES ops.work_order_units(work_order_id,unit_id),
  FOREIGN KEY(work_order_id,route_version_id) REFERENCES ops.work_orders(id,route_version_id),
  FOREIGN KEY(route_operation_id,route_version_id) REFERENCES catalog.route_operations(id,route_version_id),
  FOREIGN KEY(station_id,profile_revision) REFERENCES integration.station_profiles(station_id,revision)
);
CREATE INDEX ix_assignments_station_expiry ON ops.station_assignments(station_id,valid_until)
  WHERE revoked_at IS NULL;

CREATE TABLE ops.operation_attempts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  work_order_id uuid NOT NULL, unit_id uuid NOT NULL,
  route_version_id uuid NOT NULL, route_operation_id uuid NOT NULL,
  attempt_no integer NOT NULL CHECK(attempt_no>0),
  station_id uuid REFERENCES infra.stations(id),
  lifecycle_state text NOT NULL CHECK(lifecycle_state IN ('PLANNED','IN_PROGRESS','COMPLETED','CANCELLED')),
  started_at timestamptz, finished_at timestamptz,
  operator_user_id uuid REFERENCES identity.users(id),
  CHECK(finished_at IS NULL OR started_at IS NULL OR finished_at>=started_at),
  FOREIGN KEY(work_order_id,unit_id) REFERENCES ops.work_order_units(work_order_id,unit_id),
  FOREIGN KEY(work_order_id,route_version_id) REFERENCES ops.work_orders(id,route_version_id),
  FOREIGN KEY(route_operation_id,route_version_id) REFERENCES catalog.route_operations(id,route_version_id),
  UNIQUE(work_order_id,unit_id,route_operation_id,attempt_no), UNIQUE(id,unit_id)
);
CREATE INDEX ix_attempts_unit ON ops.operation_attempts(unit_id,started_at DESC);

-- Evidence is inserted ONLY after imported_runs validated and matched to an identified unit.
-- The incoming payload stays in immutable staging, not automatically a manufacturing acceptance.
CREATE TABLE evidence.test_runs (
  run_id uuid PRIMARY KEY REFERENCES evidence.imported_runs(run_id),
  unit_id uuid NOT NULL REFERENCES ops.units(id),
  method_revision_id uuid REFERENCES catalog.method_revisions(id),
  scenario_id text NOT NULL, scenario_version text NOT NULL,
  catalog_version text, profile_version text, software_release text,
  composition_snapshot jsonb NOT NULL, station_config_snapshot jsonb NOT NULL DEFAULT '{}'::jsonb,
  composition_sha256 char(64) NOT NULL CHECK(composition_sha256 ~ '^[0-9a-f]{64}$'),
  linked_at timestamptz NOT NULL DEFAULT now(), linked_by uuid REFERENCES identity.users(id),
  UNIQUE(run_id,unit_id)
);
CREATE TABLE evidence.run_measurements (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), run_id uuid NOT NULL REFERENCES evidence.test_runs(run_id),
  step_key text NOT NULL, parameter_key text NOT NULL, measured_numeric numeric, measured_text text,
  lower_limit numeric, upper_limit numeric, unit_symbol text,
  verdict text NOT NULL, attributes jsonb NOT NULL DEFAULT '{}'::jsonb,
  CHECK(measured_numeric IS NOT NULL OR measured_text IS NOT NULL),
  CHECK(lower_limit IS NULL OR upper_limit IS NULL OR lower_limit<=upper_limit)
);
CREATE INDEX ix_measurements_run_key ON evidence.run_measurements(run_id,parameter_key);
CREATE TABLE evidence.run_artifacts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), run_id uuid NOT NULL REFERENCES evidence.imported_runs(run_id),
  artifact_kind text NOT NULL, object_key text NOT NULL UNIQUE,
  sha256 char(64) NOT NULL CHECK(sha256 ~ '^[0-9a-f]{64}$'),
  media_type text NOT NULL, size_bytes bigint NOT NULL CHECK(size_bytes>=0),
  created_at timestamptz NOT NULL DEFAULT now(), UNIQUE(run_id,artifact_kind,sha256)
);
CREATE TABLE evidence.run_equipment (
  run_id uuid NOT NULL REFERENCES evidence.test_runs(run_id),
  equipment_id uuid NOT NULL REFERENCES infra.equipment(id),
  calibration_id uuid,
  PRIMARY KEY(run_id,equipment_id),
  FOREIGN KEY(calibration_id,equipment_id) REFERENCES infra.equipment_calibration(id,equipment_id)
);
CREATE TABLE ops.operation_runs (
  operation_attempt_id uuid NOT NULL REFERENCES ops.operation_attempts(id),
  run_id uuid NOT NULL UNIQUE REFERENCES evidence.test_runs(run_id),
  PRIMARY KEY(operation_attempt_id,run_id)
);

CREATE TABLE quality.nonconformities (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), unit_id uuid NOT NULL REFERENCES ops.units(id),
  operation_attempt_id uuid REFERENCES ops.operation_attempts(id),
  code text, description text NOT NULL,
  state text NOT NULL CHECK(state IN ('OPEN','INVESTIGATING','REWORK','RESOLVED','CLOSED')),
  created_at timestamptz NOT NULL DEFAULT now(), created_by uuid REFERENCES identity.users(id)
);
CREATE TABLE quality.operation_decisions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  unit_id uuid NOT NULL REFERENCES ops.units(id), operation_attempt_id uuid NOT NULL,
  disposition text NOT NULL CHECK(disposition IN ('ACCEPTED','REJECTED','HOLD','REWORK_REQUIRED')),
  policy_code text NOT NULL, policy_revision text NOT NULL, basis text NOT NULL,
  decided_by uuid NOT NULL REFERENCES identity.users(id),
  decided_at timestamptz NOT NULL DEFAULT now(),
  supersedes_id uuid REFERENCES quality.operation_decisions(id),
  FOREIGN KEY(operation_attempt_id,unit_id) REFERENCES ops.operation_attempts(id,unit_id)
);
CREATE TABLE quality.acceptance_records (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), unit_id uuid NOT NULL REFERENCES ops.units(id),
  decision text NOT NULL CHECK(decision IN ('PENDING','APPROVED','BLOCKED','REVOKED')),
  policy_code text NOT NULL, policy_revision text NOT NULL,
  evidence_manifest_sha256 char(64), basis text NOT NULL,
  decided_by uuid REFERENCES identity.users(id), decided_at timestamptz NOT NULL DEFAULT now(),
  supersedes_id uuid REFERENCES quality.acceptance_records(id),
  CHECK(evidence_manifest_sha256 IS NULL OR evidence_manifest_sha256 ~ '^[0-9a-f]{64}$'),
  CHECK(decision<>'APPROVED' OR (decided_by IS NOT NULL AND evidence_manifest_sha256 IS NOT NULL))
);
CREATE TABLE integration.legacy_id_map (
  station_id uuid NOT NULL REFERENCES infra.stations(id),
  source_db text NOT NULL, entity_kind text NOT NULL, source_local_id text NOT NULL,
  enterprise_id uuid NOT NULL, mapped_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(station_id,source_db,entity_kind,source_local_id)
);
CREATE TABLE audit.audit_events (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(), occurred_at timestamptz NOT NULL DEFAULT now(),
  actor_user_id uuid REFERENCES identity.users(id), source_station_id uuid REFERENCES infra.stations(id),
  action_code text NOT NULL, aggregate_kind text NOT NULL, aggregate_id uuid NOT NULL,
  correlation_id uuid, reason text, payload jsonb NOT NULL DEFAULT '{}'::jsonb
);
CREATE INDEX ix_audit_aggregate ON audit.audit_events(aggregate_kind,aggregate_id,occurred_at DESC);

-- On durable evidence and decisions, prohibit updates/deletes; revisions become additional rows.
CREATE FUNCTION audit.reject_evidence_mutation() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE EXCEPTION 'append-only table %.%: corrections require a new recorded decision/event', TG_TABLE_SCHEMA,TG_TABLE_NAME;
END; $$;
CREATE TRIGGER imported_runs_immutable BEFORE UPDATE OR DELETE ON evidence.imported_runs
  FOR EACH ROW EXECUTE FUNCTION audit.reject_evidence_mutation();
CREATE TRIGGER test_runs_immutable BEFORE UPDATE OR DELETE ON evidence.test_runs
  FOR EACH ROW EXECUTE FUNCTION audit.reject_evidence_mutation();
CREATE TRIGGER run_measurements_immutable BEFORE UPDATE OR DELETE ON evidence.run_measurements
  FOR EACH ROW EXECUTE FUNCTION audit.reject_evidence_mutation();
CREATE TRIGGER run_artifacts_immutable BEFORE UPDATE OR DELETE ON evidence.run_artifacts
  FOR EACH ROW EXECUTE FUNCTION audit.reject_evidence_mutation();
CREATE TRIGGER quality_decisions_immutable BEFORE UPDATE OR DELETE ON quality.operation_decisions
  FOR EACH ROW EXECUTE FUNCTION audit.reject_evidence_mutation();
CREATE TRIGGER acceptance_records_immutable BEFORE UPDATE OR DELETE ON quality.acceptance_records
  FOR EACH ROW EXECUTE FUNCTION audit.reject_evidence_mutation();
CREATE TRIGGER station_assignments_immutable BEFORE UPDATE OF
  station_id,work_order_id,unit_id,route_version_id,route_operation_id,profile_revision,
  payload_json,payload_sha256,signature_b64,signer_key_sha256,issued_at,valid_until,issued_by
  ON ops.station_assignments FOR EACH ROW EXECUTE FUNCTION audit.reject_evidence_mutation();
CREATE TRIGGER station_assignments_no_delete BEFORE DELETE ON ops.station_assignments
  FOR EACH ROW EXECUTE FUNCTION audit.reject_evidence_mutation();
CREATE TRIGGER audit_events_immutable BEFORE UPDATE OR DELETE ON audit.audit_events
  FOR EACH ROW EXECUTE FUNCTION audit.reject_evidence_mutation();

-- Service-level checks are still mandatory: cyclic unit containment, station/site correspondence,
-- method eligibility, calibration applicability at run time, policy approval and route completion.
