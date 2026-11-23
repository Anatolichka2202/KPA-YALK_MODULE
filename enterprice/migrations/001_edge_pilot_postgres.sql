-- MilTech Station Enterprise pilot SQL. Intended to coexist with the wider draft schema.
-- REVIEW BEFORE PRODUCTION. Requires PostgreSQL 16+ and a dedicated DB owner role.
-- No UPDATE of prior data and no automatic destructive migration.
BEGIN;
CREATE SCHEMA IF NOT EXISTS infra;
CREATE SCHEMA IF NOT EXISTS integration;
CREATE SCHEMA IF NOT EXISTS evidence;
CREATE TABLE IF NOT EXISTS infra.sites (
    id uuid PRIMARY KEY, code text NOT NULL UNIQUE,
    title text NOT NULL, active boolean NOT NULL DEFAULT true
);
CREATE TABLE IF NOT EXISTS infra.stations (
    id uuid PRIMARY KEY,
    site_id uuid NOT NULL REFERENCES infra.sites(id),
    station_code text NOT NULL, title text NOT NULL,
    last_seen_at timestamptz, enabled boolean NOT NULL DEFAULT true,
    UNIQUE(site_id,station_code)
);
CREATE TABLE IF NOT EXISTS integration.station_credentials (
    station_id uuid PRIMARY KEY REFERENCES infra.stations(id),
    token_sha256 varchar(64) NOT NULL,
    issued_at timestamptz NOT NULL, revoked_at timestamptz
);
CREATE TABLE IF NOT EXISTS integration.station_profiles (
    id uuid PRIMARY KEY, station_id uuid NOT NULL REFERENCES infra.stations(id),
    revision integer NOT NULL CHECK (revision > 0),
    profile_sha256 varchar(64) NOT NULL, profile_json jsonb NOT NULL,
    created_at timestamptz NOT NULL,
    UNIQUE(station_id,revision)
);
CREATE TABLE IF NOT EXISTS integration.station_sync_state (
    station_id uuid PRIMARY KEY REFERENCES infra.stations(id),
    last_applied_sequence bigint NOT NULL DEFAULT 0 CHECK(last_applied_sequence >= 0)
);
CREATE TABLE IF NOT EXISTS integration.sync_inbox (
    station_id uuid NOT NULL REFERENCES infra.stations(id),
    event_id uuid NOT NULL,
    stream_sequence bigint NOT NULL CHECK(stream_sequence > 0),
    event_type text NOT NULL,
    payload_sha256 varchar(64) NOT NULL,
    received_at timestamptz NOT NULL,
    applied_at timestamptz,
    apply_state varchar(16) NOT NULL CHECK(apply_state IN ('RECEIVED','APPLIED','REJECTED','CONFLICT')),
    failure_reason text,
    PRIMARY KEY(station_id,event_id), UNIQUE(station_id,stream_sequence)
);
CREATE TABLE IF NOT EXISTS evidence.imported_runs (
    run_id uuid PRIMARY KEY,
    station_id uuid NOT NULL REFERENCES infra.stations(id),
    source_run_id text NOT NULL,
    unit_serial text NOT NULL, unit_model text NOT NULL,
    started_at timestamptz NOT NULL, finished_at timestamptz NOT NULL,
    verdict varchar(16) NOT NULL CHECK(verdict IN ('NORM','NOT_NORM','STAND_ERROR','INCOMPLETE','STOPPED','NOT_EXECUTED')),
    payload_json jsonb NOT NULL, payload_sha256 varchar(64) NOT NULL,
    received_at timestamptz NOT NULL,
    UNIQUE(station_id,source_run_id)
);
CREATE INDEX IF NOT EXISTS ix_imported_runs_station_time ON evidence.imported_runs(station_id,received_at DESC);
CREATE TABLE IF NOT EXISTS infra.station_observations (
    id uuid PRIMARY KEY,
    station_id uuid NOT NULL REFERENCES infra.stations(id),
    observed_at timestamptz NOT NULL,
    received_at timestamptz NOT NULL,
    agent_version text NOT NULL, inventory jsonb NOT NULL,
    clock_status varchar(24) NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_observations_station_time ON infra.station_observations(station_id, received_at DESC);
COMMIT;
