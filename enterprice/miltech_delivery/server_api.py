"""Delivery API: trusted release ingest, per-station rollout, authenticated blob download.

The server distributes signed bytes; the station independently verifies the
publisher signature with a pinned trust anchor before installing anything.
"""
from __future__ import annotations

import hashlib
import os
import tempfile
from pathlib import Path
from typing import Callable, Literal
from uuid import UUID, uuid4

from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field
from sqlalchemy import select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from miltech_delivery.package import BundleError, sha256_file, verify_bundle
from miltech_server.models import (Station, DeliveryRelease, DeliveryTarget, DeliveryTargetHistory,
                                   DeliveryReport, now_utc)

MAX_UPLOAD = 512 * 1024 * 1024  # pilot. Large software images require a separate artifact service.


class TargetRequest(BaseModel):
    release_id: UUID
    channel: Literal['pilot', 'stable'] = 'pilot'


class StationReportRequest(BaseModel):
    event_id: UUID
    release_id: UUID
    target_revision: int = Field(ge=1)
    state: Literal['STAGED', 'PENDING_REBOOT', 'ACTIVE', 'FAILED', 'ROLLED_BACK']
    detail: str | None = Field(default=None, max_length=500)


def delivery_router(db: Callable, admin: Callable, station_auth: Callable,
                    storage_root: str | Path | None, trust_keys: dict[str, bytes] | None) -> APIRouter:
    router = APIRouter(prefix='/api/v1')
    root = Path(storage_root).resolve() if storage_root is not None else None
    trust_keys = trust_keys or {}

    def require_delivery() -> Path:
        if root is None or not trust_keys:
            raise HTTPException(503, 'Delivery is not configured: storage and trusted publishers required')
        root.mkdir(parents=True, exist_ok=True)
        return root

    def blob_path(release: DeliveryRelease) -> Path:
        return require_delivery() / 'blobs' / release.bundle_sha256[:2] / (release.bundle_sha256 + '.mtbundle')

    @router.post('/admin/delivery/releases', status_code=201, dependencies=[Depends(admin)])
    async def upload_release(request: Request, session: Session = Depends(db)):
        base = require_delivery()
        if request.headers.get('content-type', '').split(';')[0] != 'application/octet-stream':
            raise HTTPException(415, 'Upload signed bundle as application/octet-stream')
        if int(request.headers.get('content-length') or 0) > MAX_UPLOAD:
            raise HTTPException(413, 'Release bundle exceeds 512 MiB pilot cap')
        incoming = base / 'incoming'
        incoming.mkdir(parents=True, exist_ok=True)
        fd, temp_name = tempfile.mkstemp(prefix='upload-', suffix='.tmp', dir=incoming)
        temp = Path(temp_name)
        try:
            size = 0
            with os.fdopen(fd, 'wb') as out:
                async for chunk in request.stream():
                    size += len(chunk)
                    if size > MAX_UPLOAD:
                        raise HTTPException(413, 'Bundle exceeds size limit')
                    out.write(chunk)
                out.flush(); os.fsync(out.fileno())
            try:
                verified = verify_bundle(temp, trust_keys)
            except BundleError as e:
                raise HTTPException(422, str(e)) from e
            release_id = verified.manifest['release_id']
            existing = session.get(DeliveryRelease, release_id)
            if existing:
                if existing.bundle_sha256 != verified.bundle_sha256:
                    raise HTTPException(409, 'Release ID already registered with another bundle')
                return {'release_id': release_id, 'bundle_sha256': existing.bundle_sha256,
                        'manifest_sha256': existing.manifest_sha256, 'status': 'already_imported'}
            row = DeliveryRelease(id=release_id, version=verified.manifest['version'],
                                  target_platform=verified.manifest['target_platform'],
                                  bundle_sha256=verified.bundle_sha256,
                                  bundle_size=verified.bundle_size,
                                  manifest_sha256=verified.manifest_sha256,
                                  manifest_json=verified.manifest,
                                  signature_json=verified.signature)
            target = blob_path(row)
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                if sha256_file(target) != verified.bundle_sha256:
                    raise HTTPException(500, 'Existing content-addressed blob is damaged')
            else:
                os.replace(temp, target)
            session.add(row)
            try:
                session.commit()
            except IntegrityError as e:
                session.rollback()
                raise HTTPException(409, 'Concurrent release registration conflict') from e
            return {'release_id': release_id, 'bundle_sha256': row.bundle_sha256,
                    'manifest_sha256': row.manifest_sha256, 'status': 'imported'}
        finally:
            temp.unlink(missing_ok=True)

    @router.put('/admin/delivery/stations/{station_id}/target', dependencies=[Depends(admin)])
    def set_target(station_id: UUID, request: TargetRequest, session: Session = Depends(db)):
        require_delivery()
        station = session.get(Station, str(station_id))
        if not station or not station.enabled:
            raise HTTPException(404, 'Station not registered/enabled')
        release = session.get(DeliveryRelease, str(request.release_id))
        if not release:
            raise HTTPException(404, 'Unknown release')
        target = session.scalar(select(DeliveryTarget)
                                .where(DeliveryTarget.station_id == station.id).with_for_update())
        if target and target.release_id == release.id and target.channel == request.channel:
            return {'station_id': station.id, 'revision': target.revision, 'status': 'unchanged'}
        revision = 1 if target is None else target.revision + 1
        at = now_utc()
        if target is None:
            target = DeliveryTarget(station_id=station.id, release_id=release.id,
                                    revision=revision, channel=request.channel, assigned_at=at)
            session.add(target)
        else:
            target.release_id, target.channel, target.revision, target.assigned_at = (
                release.id, request.channel, revision, at)
        session.add(DeliveryTargetHistory(station_id=station.id, release_id=release.id,
                                          revision=revision, channel=request.channel, assigned_at=at))
        try:
            session.commit()
        except IntegrityError as e:
            session.rollback()
            raise HTTPException(409, 'Concurrent assignment conflict') from e
        return {'station_id': station.id, 'revision': revision, 'status': 'assigned'}

    @router.get('/stations/me/delivery/desired')
    def desired(station: Station = Depends(station_auth), session: Session = Depends(db)):
        require_delivery()
        target = session.get(DeliveryTarget, station.id)
        if target is None:
            return {'status': 'unassigned'}
        release = session.get(DeliveryRelease, target.release_id)
        return {'status': 'assigned', 'target_revision': target.revision,
                'channel': target.channel, 'release_id': release.id, 'version': release.version,
                'target_platform': release.target_platform, 'bundle_sha256': release.bundle_sha256,
                'bundle_size': release.bundle_size, 'manifest_sha256': release.manifest_sha256,
                'manifest': release.manifest_json, 'signature': release.signature_json,
                'download_path': f'/api/v1/stations/me/delivery/bundles/{release.id}'}

    @router.get('/stations/me/delivery/bundles/{release_id}')
    def download(release_id: UUID, station: Station = Depends(station_auth),
                 session: Session = Depends(db)):
        target = session.get(DeliveryTarget, station.id)
        if not target or target.release_id != str(release_id):
            raise HTTPException(404, 'No active assignment for this release')
        release = session.get(DeliveryRelease, target.release_id)
        path = blob_path(release)
        if not path.is_file() or path.stat().st_size != release.bundle_size:
            raise HTTPException(503, 'Release blob absent or wrong size')
        if sha256_file(path) != release.bundle_sha256:
            raise HTTPException(503, 'Release blob checksum mismatch')
        return FileResponse(path, media_type='application/octet-stream', filename=f'{release.id}.mtbundle',
                            headers={'X-Bundle-SHA256': release.bundle_sha256,
                                     'Cache-Control': 'private, no-store'})

    @router.post('/stations/me/delivery/reports')
    def station_report(report: StationReportRequest, station: Station = Depends(station_auth),
                       session: Session = Depends(db)):
        historical = session.scalar(select(DeliveryTargetHistory).where(
            DeliveryTargetHistory.station_id == station.id,
            DeliveryTargetHistory.revision == report.target_revision))
        if historical is None or historical.release_id != str(report.release_id):
            raise HTTPException(409, 'No matching assignment history')
        old = session.scalar(select(DeliveryReport).where(
            DeliveryReport.station_id == station.id, DeliveryReport.event_id == str(report.event_id)))
        if old:
            if (old.release_id, old.target_revision, old.state, old.detail) != (
                str(report.release_id), report.target_revision, report.state, report.detail):
                raise HTTPException(409, 'Delivery event UUID reused with modified data')
            return {'status': 'duplicate', 'event_id': str(report.event_id)}
        session.add(DeliveryReport(station_id=station.id, event_id=str(report.event_id),
                                   release_id=str(report.release_id), target_revision=report.target_revision,
                                   state=report.state, detail=report.detail))
        try:session.commit()
        except IntegrityError as e:
            session.rollback()
            raise HTTPException(409, 'Concurrent report conflict') from e
        return {'status': 'recorded', 'event_id': str(report.event_id)}

    @router.get('/admin/delivery/stations/{station_id}/reports', dependencies=[Depends(admin)])
    def list_reports(station_id: UUID, session: Session = Depends(db)):
        rows = session.scalars(select(DeliveryReport).where(DeliveryReport.station_id == str(station_id))
                               .order_by(DeliveryReport.reported_at.desc()).limit(100)).all()
        return [{'event_id': r.event_id, 'release_id': r.release_id, 'target_revision': r.target_revision,
                 'state': r.state, 'detail': r.detail, 'reported_at': r.reported_at.isoformat()}
                for r in rows]

    return router
