-- MilTech Station Enterprise — PostgreSQL logical schema, Draft 0.1 (2026-11-23)
-- NOT a production migration; review/migration strategy and access model required.
-- PostgreSQL 16+ recommended. Some referential policies require service-level validators.

CREATE EXTENSION IF NOT EXISTS btree_gist;
CREATE SCHEMA IF NOT EXISTS identity;
CREATE SCHEMA IF NOT EXISTS infra;
CREATE SCHEMA IF NOT EXISTS catalog;
CREATE SCHEMA IF NOT EXISTS ops;
CREATE SCHEMA IF NOT EXISTS evidence;
CREATE SCHEMA IF NOT EXISTS quality;
CREATE SCHEMA IF NOT EXISTS integration;
CREATE SCHEMA IF NOT EXISTS audit;

CREATE TABLE identity.users (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    external_subject text UNIQUE,
    display_name text NOT NULL,
    active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE identity.roles (
    code text PRIMARY KEY,
    description text NOT NULL
);
CREATE TABLE identity.user_roles (
    user_id uuid NOT NULL REFERENCES identity.users(id),
    role_code text NOT NULL REFERENCES identity.roles(code),
    PRIMARY KEY(user_id, role_code)
);

CREATE TABLE infra.sites (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    code text NOT NULL UNIQUE,
    title text NOT NULL,
    active boolean NOT NULL DEFAULT true
);
CREATE TABLE infra.stations (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    site_id uuid NOT NULL REFERENCES infra.sites(id),
    station_code text NOT NULL,
    title text NOT NULL,
    last_seen_at timestamptz,
    enabled boolean NOT NULL DEFAULT true,
    UNIQUE(site_id, station_code)
);
CREATE TABLE infra.equipment (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    site_id uuid NOT NULL REFERENCES infra.sites(id),
    equipment_type text NOT NULL,
    manufacturer text,
    model text,
    serial_number text NOT NULL,
    inventory_number text,
    active boolean NOT NULL DEFAULT true,
    UNIQUE(site_id, equipment_type, serial_number)
);
CREATE TABLE infra.calibration_records (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    equipment_id uuid NOT NULL REFERENCES infra.equipment(id),
    record_kind text NOT NULL CHECK (record_kind IN ('CALIBRATION','VERIFICATION','INSPECTION')),
    valid_from timestamptz NOT NULL,
    valid_until timestamptz,
    document_ref text,
    document_sha256 text,
    status text NOT NULL CHECK (status IN ('VALID','INVALID','CANCELLED')),
    CHECK(valid_until IS NULL OR valid_until > valid_from),
    CHECK(document_sha256 IS NULL OR document_sha256 ~ '^[0-9a-f]{64}$')
);

CREATE TABLE catalog.product_models (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    model_code text NOT NULL UNIQUE,
    title text NOT NULL,
    is_serialized boolean NOT NULL DEFAULT true
);
CREATE TABLE catalog.model_revisions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    model_id uuid NOT NULL REFERENCES catalog.product_models(id),
    revision_code text NOT NULL,
    specification_ref text,
    approved_at timestamptz,
    UNIQUE(model_id, revision_code),
    UNIQUE(id, model_id)
);
CREATE TABLE catalog.method_revisions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    method_code text NOT NULL,
    revision_code text NOT NULL,
    specification_ref text NOT NULL,
    definition_sha256 text NOT NULL CHECK(definition_sha256 ~ '^[0-9a-f]{64}$'),
    approved_at timestamptz,
    UNIQUE(method_code, revision_code)
);
CREATE TABLE catalog.route_versions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    model_revision_id uuid NOT NULL REFERENCES catalog.model_revisions(id),
    route_code text NOT NULL,
    revision_code text NOT NULL,
    approved_at timestamptz,
    UNIQUE(model_revision_id, route_code, revision_code)
);
CREATE TABLE catalog.route_operations (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    route_version_id uuid NOT NULL REFERENCES catalog.route_versions(id),
    operation_code text NOT NULL,
    operation_title text NOT NULL,
    sequence_no integer NOT NULL CHECK(sequence_no > 0),
    method_revision_id uuid REFERENCES catalog.method_revisions(id),
    required boolean NOT NULL DEFAULT true,
    UNIQUE(route_version_id, operation_code),
    UNIQUE(route_version_id, sequence_no)
);
CREATE TABLE catalog.route_dependencies (
    route_operation_id uuid NOT NULL REFERENCES catalog.route_operations(id),
    prerequisite_operation_id uuid NOT NULL REFERENCES catalog.route_operations(id),
    PRIMARY KEY(route_operation_id, prerequisite_operation_id),
    CHECK(route_operation_id <> prerequisite_operation_id)
    -- Enforce a shared route_version and DAG acyclicity in route publishing validation.
);

CREATE TABLE ops.units (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    model_id uuid NOT NULL REFERENCES catalog.product_models(id),
    model_revision_id uuid NOT NULL,
    serial_number text NOT NULL,
    site_id uuid NOT NULL REFERENCES infra.sites(id),
    registered_at timestamptz NOT NULL DEFAULT now(),
    registered_by uuid REFERENCES identity.users(id),
    FOREIGN KEY (model_revision_id, model_id)
        REFERENCES catalog.model_revisions(id, model_id),
    UNIQUE(model_id, serial_number)
);
CREATE TABLE ops.unit_installations (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    parent_unit_id uuid NOT NULL REFERENCES ops.units(id),
    component_unit_id uuid NOT NULL REFERENCES ops.units(id),
    slot_code text NOT NULL,
    installed_at timestamptz NOT NULL,
    removed_at timestamptz,
    removal_reason text,
    recorded_by uuid REFERENCES identity.users(id),
    CHECK(parent_unit_id <> component_unit_id),
    CHECK(removed_at IS NULL OR removed_at > installed_at),
    EXCLUDE USING gist (
        component_unit_id WITH =,
        tstzrange(installed_at, COALESCE(removed_at,'infinity'::timestamptz),'[)') WITH &&
    ),
    EXCLUDE USING gist (
        parent_unit_id WITH =,
        slot_code WITH =,
        tstzrange(installed_at, COALESCE(removed_at,'infinity'::timestamptz),'[)') WITH &&
    )
    -- Validate acyclic recursive assemblies at service level.
);
CREATE INDEX idx_installations_parent ON ops.unit_installations(parent_unit_id, installed_at DESC);

CREATE TABLE ops.work_orders (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    site_id uuid NOT NULL REFERENCES infra.sites(id),
    order_code text NOT NULL,
    route_version_id uuid NOT NULL REFERENCES catalog.route_versions(id),
    external_erp_ref text,
    status text NOT NULL CHECK(status IN ('PLANNED','OPEN','HOLD','CLOSED','CANCELLED')),
    created_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE(site_id, order_code)
);
CREATE TABLE ops.work_order_units (
    work_order_id uuid NOT NULL REFERENCES ops.work_orders(id),
    unit_id uuid NOT NULL REFERENCES ops.units(id),
    assigned_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY(work_order_id, unit_id)
);
CREATE TABLE ops.operation_attempts (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    work_order_id uuid NOT NULL REFERENCES ops.work_orders(id),
    unit_id uuid NOT NULL REFERENCES ops.units(id),
    route_operation_id uuid NOT NULL REFERENCES catalog.route_operations(id),
    attempt_number integer NOT NULL CHECK(attempt_number > 0),
    lifecycle_status text NOT NULL CHECK(lifecycle_status IN ('PLANNED','IN_PROGRESS','COMPLETED','CANCELLED')),
    started_at timestamptz,
    finished_at timestamptz,
    operator_user_id uuid REFERENCES identity.users(id),
    created_at timestamptz NOT NULL DEFAULT now(),
    CHECK(finished_at IS NULL OR started_at IS NULL OR finished_at >= started_at),
    UNIQUE(work_order_id, unit_id, route_operation_id, attempt_number),
    -- Needed to scope quality decisions to the correct unit.
    UNIQUE(id, unit_id),
    FOREIGN KEY(work_order_id, unit_id) REFERENCES ops.work_order_units(work_order_id, unit_id)
    -- Validate operation belongs to the work-order route at service level.
);

CREATE TABLE evidence.test_runs (
    run_id uuid PRIMARY KEY,
    source_station_id uuid NOT NULL REFERENCES infra.stations(id),
    source_run_id text,
    unit_id uuid NOT NULL REFERENCES ops.units(id),
    method_revision_id uuid REFERENCES catalog.method_revisions(id),
    scenario_id text NOT NULL,
    scenario_version text NOT NULL,
    catalog_version text NOT NULL,
    profile_version text NOT NULL,
    software_release text NOT NULL,
    firmware_snapshot jsonb NOT NULL DEFAULT '{}'::jsonb,
    composition_snapshot jsonb NOT NULL DEFAULT '[]'::jsonb,
    station_config_snapshot jsonb NOT NULL DEFAULT '{}'::jsonb,
    operator_user_id uuid REFERENCES identity.users(id),
    started_at timestamptz NOT NULL,
    finished_at timestamptz,
    received_at timestamptz NOT NULL DEFAULT now(),
    verdict text NOT NULL CHECK(verdict IN ('NORM','NOT_NORM','STAND_ERROR','INCOMPLETE','STOPPED','NOT_EXECUTED')),
    clock_status text NOT NULL DEFAULT 'UNKNOWN',
    CHECK(finished_at IS NULL OR finished_at >= started_at),
    UNIQUE(source_station_id, source_run_id)
);
CREATE INDEX idx_test_runs_unit_time ON evidence.test_runs(unit_id, started_at DESC);
CREATE TABLE evidence.run_measurements (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    run_id uuid NOT NULL REFERENCES evidence.test_runs(run_id),
    step_key text NOT NULL,
    parameter_key text NOT NULL,
    measured_numeric numeric,
    measured_text text,
    lower_limit numeric,
    upper_limit numeric,
    unit_symbol text,
    verdict text NOT NULL,
    attributes jsonb NOT NULL DEFAULT '{}'::jsonb,
    CHECK(lower_limit IS NULL OR upper_limit IS NULL OR lower_limit <= upper_limit)
);
CREATE INDEX idx_measurements_run_parameter ON evidence.run_measurements(run_id,parameter_key);
CREATE TABLE evidence.run_artifacts (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    run_id uuid NOT NULL REFERENCES evidence.test_runs(run_id),
    artifact_kind text NOT NULL,
    object_key text NOT NULL UNIQUE,
    sha256 text NOT NULL CHECK(sha256 ~ '^[0-9a-f]{64}$'),
    media_type text NOT NULL,
    size_bytes bigint NOT NULL CHECK(size_bytes >= 0),
    received_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE(run_id, artifact_kind, sha256)
);
CREATE TABLE evidence.run_equipment (
    run_id uuid NOT NULL REFERENCES evidence.test_runs(run_id),
    equipment_id uuid NOT NULL REFERENCES infra.equipment(id),
    calibration_record_id uuid REFERENCES infra.calibration_records(id),
    PRIMARY KEY(run_id, equipment_id)
    -- Validate calibration record belongs to equipment and was applicable to run.
);
CREATE TABLE ops.operation_runs (
    operation_attempt_id uuid NOT NULL REFERENCES ops.operation_attempts(id),
    run_id uuid NOT NULL UNIQUE REFERENCES evidence.test_runs(run_id),
    PRIMARY KEY(operation_attempt_id, run_id)
    -- Validate attempted unit_id matches run unit_id.
);

CREATE TABLE quality.nonconformities (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    unit_id uuid NOT NULL REFERENCES ops.units(id),
    operation_attempt_id uuid REFERENCES ops.operation_attempts(id),
    defect_code text,
    description text NOT NULL,
    status text NOT NULL CHECK(status IN ('OPEN','INVESTIGATING','REWORK','RESOLVED','CLOSED')),
    raised_at timestamptz NOT NULL DEFAULT now(),
    raised_by uuid REFERENCES identity.users(id)
);
CREATE TABLE quality.quality_decisions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    unit_id uuid NOT NULL REFERENCES ops.units(id),
    operation_attempt_id uuid NOT NULL,
    disposition text NOT NULL CHECK(disposition IN ('ACCEPTED','REJECTED','HOLD','REWORK_REQUIRED')),
    basis text NOT NULL,
    policy_code text NOT NULL,
    policy_revision text NOT NULL,
    decided_by uuid NOT NULL REFERENCES identity.users(id),
    decided_at timestamptz NOT NULL DEFAULT now(),
    supersedes_decision_id uuid REFERENCES quality.quality_decisions(id),
    FOREIGN KEY(operation_attempt_id,unit_id) REFERENCES ops.operation_attempts(id,unit_id)
);
CREATE TABLE quality.acceptance_records (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    unit_id uuid NOT NULL REFERENCES ops.units(id),
    decision text NOT NULL CHECK(decision IN ('PENDING','APPROVED','BLOCKED','REVOKED')),
    policy_code text NOT NULL,
    policy_revision text NOT NULL,
    evidence_manifest_sha256 text,
    basis text NOT NULL,
    decided_by uuid REFERENCES identity.users(id),
    decided_at timestamptz NOT NULL DEFAULT now(),
    supersedes_record_id uuid REFERENCES quality.acceptance_records(id),
    CHECK(evidence_manifest_sha256 IS NULL OR evidence_manifest_sha256 ~ '^[0-9a-f]{64}$'),
    CHECK(decision <> 'APPROVED' OR (decided_by IS NOT NULL AND evidence_manifest_sha256 IS NOT NULL))
    -- Approval eligibility and approved policy cannot be proven with DDL alone.
);

CREATE TABLE integration.legacy_id_map (
    source_system text NOT NULL,
    entity_type text NOT NULL,
    source_local_id text NOT NULL,
    enterprise_id uuid NOT NULL,
    mapped_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY(source_system,entity_type,source_local_id),
    UNIQUE(source_system,entity_type,enterprise_id)
);
CREATE TABLE integration.sync_inbox (
    station_id uuid NOT NULL REFERENCES infra.stations(id),
    event_id uuid NOT NULL,
    stream_sequence bigint NOT NULL CHECK(stream_sequence > 0),
    event_type text NOT NULL,
    payload_sha256 text NOT NULL CHECK(payload_sha256 ~ '^[0-9a-f]{64}$'),
    received_at timestamptz NOT NULL DEFAULT now(),
    applied_at timestamptz,
    apply_state text NOT NULL CHECK(apply_state IN ('RECEIVED','APPLIED','REJECTED','CONFLICT')),
    failure_reason text,
    PRIMARY KEY(station_id,event_id),
    UNIQUE(station_id,stream_sequence)
);
CREATE TABLE audit.audit_events (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    occurred_at timestamptz NOT NULL DEFAULT now(),
    actor_user_id uuid REFERENCES identity.users(id),
    source_station_id uuid REFERENCES infra.stations(id),
    action_code text NOT NULL,
    aggregate_type text NOT NULL,
    aggregate_id uuid NOT NULL,
    reason text,
    correlation_id uuid,
    payload jsonb NOT NULL DEFAULT '{}'::jsonb
);
CREATE INDEX idx_audit_aggregate ON audit.audit_events(aggregate_type, aggregate_id, occurred_at);

-- Production hardening TODO:
-- 1. Secure schema owners, GRANT/REVOKE, per-service accounts and audit DB roles.
-- 2. Add immutable/append-only triggers and constrained state-transition procedures.
-- 3. Validate unit DAG, revision/route consistency, work-order membership, calibration applicability.
-- 4. Create migration tests, seeded reference roles, online backup/restore and retention policy.
-- 5. Decide instrument/telemetry schemas; use object storage for high-volume time-series/raw traffic.
-- 6. Add RLS for multi-tenant operation only if actually required (not implied by multi-site).
