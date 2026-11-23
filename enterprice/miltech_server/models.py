"""Slice-1 models. Names agree with the previous MilTech draft schemas.

Not a replacement for the wider manufacturing schema. PostgreSQL uses dedicated
schemas; SQLite dev/tests translate those schemas to the default namespace.
"""
from datetime import datetime, timezone
from uuid import uuid4

from sqlalchemy import (
    Boolean, DateTime, ForeignKey, Integer, BigInteger, JSON, String, Text,
    UniqueConstraint, CheckConstraint, Uuid, func
)
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column


def now_utc():
    return datetime.now(timezone.utc)

JsonType = JSON().with_variant(JSONB(), 'postgresql')

class Base(DeclarativeBase):
    pass

class Site(Base):
    __tablename__ = 'sites'
    __table_args__ = {'schema': 'infra'}
    id: Mapped[str] = mapped_column(Uuid(as_uuid=False), primary_key=True, default=lambda: str(uuid4()))
    code: Mapped[str] = mapped_column(Text, nullable=False, unique=True)
    title: Mapped[str] = mapped_column(Text, nullable=False)
    active: Mapped[bool] = mapped_column(Boolean, nullable=False, default=True, server_default='true')

class Station(Base):
    __tablename__ = 'stations'
    __table_args__ = (UniqueConstraint('site_id', 'station_code'), {'schema':'infra'})
    id: Mapped[str] = mapped_column(Uuid(as_uuid=False), primary_key=True, default=lambda: str(uuid4()))
    site_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('infra.sites.id'), nullable=False)
    station_code: Mapped[str] = mapped_column(Text, nullable=False)
    title: Mapped[str] = mapped_column(Text, nullable=False)
    last_seen_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    enabled: Mapped[bool] = mapped_column(Boolean, nullable=False, default=True, server_default='true')

class StationCredential(Base):
    __tablename__ = 'station_credentials'
    __table_args__ = {'schema':'integration'}
    station_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('infra.stations.id'), primary_key=True)
    token_sha256: Mapped[str] = mapped_column(String(64), nullable=False)
    issued_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=now_utc, nullable=False)
    revoked_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

class StationProfile(Base):
    __tablename__ = 'station_profiles'
    __table_args__ = (UniqueConstraint('station_id', 'revision'), {'schema':'integration'})
    id: Mapped[str] = mapped_column(Uuid(as_uuid=False), primary_key=True, default=lambda: str(uuid4()))
    station_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('infra.stations.id'), nullable=False)
    revision: Mapped[int] = mapped_column(Integer, nullable=False)
    profile_sha256: Mapped[str] = mapped_column(String(64), nullable=False)
    profile_json: Mapped[dict] = mapped_column(JsonType, nullable=False)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, default=now_utc)

class StationSyncState(Base):
    __tablename__ = 'station_sync_state'
    __table_args__ = {'schema':'integration'}
    station_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('infra.stations.id'), primary_key=True)
    last_applied_sequence: Mapped[int] = mapped_column(BigInteger, nullable=False, default=0, server_default='0')

class SyncInbox(Base):
    __tablename__ = 'sync_inbox'
    __table_args__ = (UniqueConstraint('station_id', 'stream_sequence'),
                      CheckConstraint('stream_sequence > 0'), {'schema':'integration'})
    station_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('infra.stations.id'), primary_key=True)
    event_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), primary_key=True)
    stream_sequence: Mapped[int] = mapped_column(BigInteger, nullable=False)
    event_type: Mapped[str] = mapped_column(Text, nullable=False)
    payload_sha256: Mapped[str] = mapped_column(String(64), nullable=False)
    received_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, default=now_utc)
    applied_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    apply_state: Mapped[str] = mapped_column(String(16), nullable=False)
    failure_reason: Mapped[str | None] = mapped_column(Text)

class ImportedRun(Base):
    """Durable arrival staging, NOT a normative approved test_runs record."""
    __tablename__ = 'imported_runs'
    __table_args__ = (UniqueConstraint('station_id','source_run_id'), {'schema':'evidence'})
    run_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), primary_key=True)
    station_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('infra.stations.id'), nullable=False)
    source_run_id: Mapped[str] = mapped_column(Text, nullable=False)
    unit_serial: Mapped[str] = mapped_column(Text, nullable=False)
    unit_model: Mapped[str] = mapped_column(Text, nullable=False)
    started_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    finished_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    verdict: Mapped[str] = mapped_column(String(16), nullable=False)
    payload_json: Mapped[dict] = mapped_column(JsonType, nullable=False)
    payload_sha256: Mapped[str] = mapped_column(String(64), nullable=False)
    received_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, default=now_utc)

class StationObservation(Base):
    __tablename__ = 'station_observations'
    __table_args__ = {'schema':'infra'}
    id: Mapped[str] = mapped_column(Uuid(as_uuid=False), primary_key=True, default=lambda: str(uuid4()))
    station_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('infra.stations.id'), nullable=False)
    observed_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    received_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, default=now_utc)
    agent_version: Mapped[str] = mapped_column(Text, nullable=False)
    inventory: Mapped[dict] = mapped_column(JsonType, nullable=False)
    clock_status: Mapped[str] = mapped_column(String(24), nullable=False)

# ---------------------------------------------------------------------------
# Slice 3: signed software delivery. Trust roots and actual binary blobs are
# external to the relational DB. Assignment/status is NOT a QA acceptance.
# ---------------------------------------------------------------------------

class DeliveryRelease(Base):
    __tablename__ = 'releases'
    __table_args__ = {'schema': 'delivery'}
    id: Mapped[str] = mapped_column(Uuid(as_uuid=False), primary_key=True)
    version: Mapped[str] = mapped_column(String(80), nullable=False)
    target_platform: Mapped[str] = mapped_column(String(40), nullable=False)
    bundle_sha256: Mapped[str] = mapped_column(String(64), nullable=False, unique=True)
    bundle_size: Mapped[int] = mapped_column(BigInteger, nullable=False)
    manifest_sha256: Mapped[str] = mapped_column(String(64), nullable=False)
    manifest_json: Mapped[dict] = mapped_column(JsonType, nullable=False)
    signature_json: Mapped[dict] = mapped_column(JsonType, nullable=False)
    imported_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, default=now_utc)

class DeliveryTarget(Base):
    __tablename__ = 'station_targets'
    __table_args__ = {'schema': 'delivery'}
    station_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('infra.stations.id'), primary_key=True)
    release_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('delivery.releases.id'), nullable=False)
    revision: Mapped[int] = mapped_column(Integer, nullable=False)
    channel: Mapped[str] = mapped_column(String(16), nullable=False)
    assigned_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, default=now_utc)

class DeliveryReport(Base):
    __tablename__ = 'station_reports'
    __table_args__ = (UniqueConstraint('station_id','event_id'), {'schema': 'delivery'})
    id: Mapped[str] = mapped_column(Uuid(as_uuid=False), primary_key=True, default=lambda: str(uuid4()))
    station_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('infra.stations.id'), nullable=False)
    event_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), nullable=False)
    release_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('delivery.releases.id'), nullable=False)
    target_revision: Mapped[int] = mapped_column(Integer, nullable=False)
    state: Mapped[str] = mapped_column(String(24), nullable=False)
    detail: Mapped[str | None] = mapped_column(String(500))
    reported_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, default=now_utc)

class DeliveryTargetHistory(Base):
    __tablename__ = 'target_history'
    __table_args__ = (UniqueConstraint('station_id', 'revision'), {'schema': 'delivery'})
    id: Mapped[str] = mapped_column(Uuid(as_uuid=False), primary_key=True, default=lambda: str(uuid4()))
    station_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('infra.stations.id'), nullable=False)
    release_id: Mapped[str] = mapped_column(Uuid(as_uuid=False), ForeignKey('delivery.releases.id'), nullable=False)
    revision: Mapped[int] = mapped_column(Integer, nullable=False)
    channel: Mapped[str] = mapped_column(String(16), nullable=False)
    assigned_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, default=now_utc)
