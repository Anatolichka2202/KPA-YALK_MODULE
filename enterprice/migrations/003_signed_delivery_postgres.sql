-- Slice 3: online/offline signed release distribution, PostgreSQL 16+.
-- Execute after 001, 002 with the reviewed migration tool.
BEGIN;
CREATE SCHEMA IF NOT EXISTS delivery;
CREATE TABLE delivery.releases (
  id uuid PRIMARY KEY,
  version varchar(80) NOT NULL,
  target_platform varchar(40) NOT NULL,
  bundle_sha256 varchar(64) NOT NULL UNIQUE CHECK(bundle_sha256 ~ '^[a-f0-9]{64}$'),
  bundle_size bigint NOT NULL CHECK(bundle_size > 0),
  manifest_sha256 varchar(64) NOT NULL CHECK(manifest_sha256 ~ '^[a-f0-9]{64}$'),
  manifest_json jsonb NOT NULL,
  signature_json jsonb NOT NULL,
  imported_at timestamptz NOT NULL
);
CREATE TABLE delivery.station_targets (
  station_id uuid PRIMARY KEY REFERENCES infra.stations(id),
  release_id uuid NOT NULL REFERENCES delivery.releases(id),
  revision integer NOT NULL CHECK(revision>0),
  channel varchar(16) NOT NULL CHECK(channel IN ('pilot','stable')),
  assigned_at timestamptz NOT NULL
);
CREATE TABLE delivery.target_history (
  id uuid PRIMARY KEY,
  station_id uuid NOT NULL REFERENCES infra.stations(id),
  release_id uuid NOT NULL REFERENCES delivery.releases(id),
  revision integer NOT NULL CHECK(revision>0),
  channel varchar(16) NOT NULL CHECK(channel IN ('pilot','stable')),
  assigned_at timestamptz NOT NULL,
  UNIQUE(station_id,revision)
);
CREATE TABLE delivery.station_reports (
  id uuid PRIMARY KEY,
  station_id uuid NOT NULL REFERENCES infra.stations(id),
  event_id uuid NOT NULL,
  release_id uuid NOT NULL REFERENCES delivery.releases(id),
  target_revision integer NOT NULL CHECK(target_revision>0),
  state varchar(24) NOT NULL CHECK(state IN ('STAGED','PENDING_REBOOT','ACTIVE','FAILED','ROLLED_BACK')),
  detail varchar(500),
  reported_at timestamptz NOT NULL,
  UNIQUE(station_id,event_id)
);
CREATE INDEX ix_station_reports_station_time ON delivery.station_reports(station_id,reported_at DESC);
COMMIT;
