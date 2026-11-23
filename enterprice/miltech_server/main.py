"""MilTech Enterprise station registry and lossless offline run intake (pilot)."""
import hashlib
import hmac
import json
import os
import secrets
from contextlib import asynccontextmanager
from uuid import UUID

from fastapi import FastAPI, Header, HTTPException, Depends, Request
from fastapi.responses import JSONResponse
from sqlalchemy import select, text, func
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from miltech_server.contracts import SiteCreate, StationCreate, Profile, Heartbeat, SyncEvent
from miltech_server.db import build_engine, create_session_factory
from miltech_server.models import (
    Site, Station, StationCredential, StationProfile, StationSyncState,
    SyncInbox, ImportedRun, StationObservation, now_utc
)


def canonical_bytes(obj:dict)->bytes:
    return json.dumps(obj, sort_keys=True, separators=(',',':'), ensure_ascii=False).encode('utf-8')

def digest(obj:dict)->str:
    return hashlib.sha256(canonical_bytes(obj)).hexdigest()


def create_app(database_url:str|None=None, admin_key:str|None=None, engine=None,
               delivery_storage_root:str|None=None, delivery_trust_keys:dict[str,bytes]|None=None)->FastAPI:
    database_url = database_url or os.environ.get('MILTECH_DATABASE_URL','sqlite:///./miltech-dev.sqlite3')
    if admin_key is None:
        admin_key = os.environ.get('MILTECH_ADMIN_KEY')
    engine = engine or build_engine(database_url)
    sessions = create_session_factory(engine)
    app = FastAPI(title='MilTech Enterprise Edge Intake + Software Delivery',version='0.3.0',
                  description='Pilot: station registry, offline evidence intake, and signed software release delivery. No equipment control.')
    app.state.engine = engine
    app.state.sessions = sessions
    app.state.admin_key = admin_key
    if delivery_storage_root is None:
        delivery_storage_root = os.environ.get('MILTECH_DELIVERY_STORAGE')
    if delivery_trust_keys is None:
        trust_path = os.environ.get('MILTECH_DELIVERY_TRUST_FILE')
        if trust_path:
            from miltech_delivery.package import read_trust_file
            delivery_trust_keys = read_trust_file(trust_path)

    @app.middleware('http')
    async def limit_body(request:Request,call_next):
        if request.method in {'POST','PUT','PATCH'} and request.url.path != '/api/v1/admin/delivery/releases':
            size = 1024*1024
            if int(request.headers.get('content-length','0')) > size:
                return JSONResponse(status_code=413,content={'detail':'request body too large (1 MiB max)'})
            body = await request.body()
            if len(body)>size:
                return JSONResponse(status_code=413,content={'detail':'request body too large (1 MiB max)'})
        return await call_next(request)

    def db():
        with sessions() as session:
            yield session

    def admin(x_admin_key:str|None=Header(default=None)):
        secret=app.state.admin_key
        if not secret or not x_admin_key or not hmac.compare_digest(x_admin_key,secret):
            raise HTTPException(403,'Admin enrollment is disabled or invalid')

    def station_auth(authorization:str|None=Header(default=None), session:Session=Depends(db)) -> Station:
        if not authorization or not authorization.startswith('Bearer '):
            raise HTTPException(401,'Missing station bearer token')
        token=authorization[7:]
        parts=token.split('_',2)
        if len(parts)!=3 or parts[0]!='mts' or len(parts[1])!=32:
            raise HTTPException(401,'Invalid station credential')
        try:station_id=str(UUID(hex=parts[1]))
        except ValueError:raise HTTPException(401,'Invalid station identifier')
        row=session.get(StationCredential,station_id)
        station=session.get(Station,station_id)
        if not row or not station or not station.enabled or row.revoked_at is not None:
            raise HTTPException(401,'Station credential revoked or unknown')
        try:token_hash=hashlib.sha256(token.encode('ascii')).hexdigest()
        except UnicodeEncodeError:raise HTTPException(401,'Invalid credential encoding')
        if not hmac.compare_digest(token_hash,row.token_sha256):
            raise HTTPException(401,'Invalid station credential')
        return station

    @app.get('/health/live')
    def live():return {'status':'alive'}

    @app.get('/health/ready')
    def ready(session:Session=Depends(db)):
        try:
            session.execute(select(Site.id).limit(1))
            session.execute(select(StationSyncState.station_id).limit(1))
        except Exception:raise HTTPException(503,'Database unavailable')
        return {'status':'ready'}

    @app.post('/api/v1/admin/sites', status_code=201, dependencies=[Depends(admin)])
    def add_site(req:SiteCreate,session:Session=Depends(db)):
        if session.scalar(select(Site).where(Site.code==req.code)):
            raise HTTPException(409,'Site code already exists')
        row=Site(code=req.code,title=req.title)
        session.add(row);session.commit()
        return {'site_id':row.id,'code':row.code}

    @app.post('/api/v1/admin/stations',status_code=201,dependencies=[Depends(admin)])
    def add_station(req:StationCreate,session:Session=Depends(db)):
        site=session.scalar(select(Site).where(Site.code==req.site_code,Site.active.is_(True)))
        if not site:raise HTTPException(404,'Site not registered or inactive')
        exists=session.scalar(select(Station).where(Station.site_id==site.id, Station.station_code==req.station_code))
        if exists:raise HTTPException(409,'Station code already exists within site')
        row=Station(site_id=site.id,station_code=req.station_code,title=req.title)
        session.add(row);session.flush()
        token=f'mts_{UUID(row.id).hex}_{secrets.token_urlsafe(32)}'
        session.add(StationCredential(station_id=row.id, token_sha256=hashlib.sha256(token.encode()).hexdigest()))
        session.add(StationSyncState(station_id=row.id,last_applied_sequence=0))
        session.commit()
        return {'station_id':row.id,'station_token':token,'note':'Save the token securely: it is displayed only once.'}

    @app.post('/api/v1/admin/stations/{station_id}/rotate-token',dependencies=[Depends(admin)])
    def rotate_token(station_id:UUID,session:Session=Depends(db)):
        row=session.get(StationCredential,str(station_id))
        if not row:raise HTTPException(404,'Unknown station')
        token=f'mts_{station_id.hex}_{secrets.token_urlsafe(32)}'
        row.token_sha256=hashlib.sha256(token.encode('ascii')).hexdigest()
        row.issued_at=now_utc();row.revoked_at=None
        session.commit()
        return {'station_id':str(station_id),'station_token':token,'note':'Old token is invalid immediately'}

    @app.post('/api/v1/admin/stations/{station_id}/disable',dependencies=[Depends(admin)])
    def disable_station(station_id:UUID,session:Session=Depends(db)):
        row=session.get(Station,str(station_id))
        if not row:raise HTTPException(404,'Unknown station')
        row.enabled=False;session.commit()
        return {'station_id':str(station_id),'enabled':False}

    @app.put('/api/v1/admin/stations/{station_id}/profile',dependencies=[Depends(admin)])
    def publish_profile(station_id:UUID,profile:Profile,session:Session=Depends(db)):
        station=session.get(Station,str(station_id))
        if not station:raise HTTPException(404,'Unknown station')
        if profile.station_code != station.station_code:raise HTTPException(422,'Profile station_code mismatch')
        data=profile.model_dump(mode='json')
        last=session.scalar(select(StationProfile).where(StationProfile.station_id==station.id).order_by(StationProfile.revision.desc()).limit(1))
        hashcode=digest(data)
        if last and last.profile_sha256==hashcode:
            return {'revision':last.revision,'sha256':hashcode,'status':'unchanged'}
        revision=1 if last is None else last.revision+1
        row=StationProfile(station_id=station.id,revision=revision,profile_json=data,profile_sha256=hashcode)
        session.add(row);session.commit()
        return {'revision':revision,'sha256':hashcode,'status':'published'}

    @app.get('/api/v1/stations/me/profile')
    def current_profile(station:Station=Depends(station_auth),session:Session=Depends(db)):
        row=session.scalar(select(StationProfile).where(StationProfile.station_id==station.id).order_by(StationProfile.revision.desc()).limit(1))
        if not row:raise HTTPException(404,'Profile not assigned')
        return {'revision':row.revision,'sha256':row.profile_sha256,'profile':row.profile_json}

    @app.post('/api/v1/stations/me/heartbeat')
    def heartbeat(req:Heartbeat,station:Station=Depends(station_auth),session:Session=Depends(db)):
        if req.observed_at.tzinfo is None:raise HTTPException(422,'observed_at must be timezone-aware')
        received=now_utc()
        station.last_seen_at=received
        session.add(StationObservation(station_id=station.id,observed_at=req.observed_at,
                                       received_at=received,agent_version=req.agent_version,
                                       inventory=req.inventory,clock_status=req.clock_status))
        session.commit()
        return {'accepted':True,'server_time':received.isoformat()}

    @app.get('/api/v1/stations/me/sync-state')
    def sync_state(station:Station=Depends(station_auth),session:Session=Depends(db)):
        row=session.get(StationSyncState,station.id)
        if not row:raise HTTPException(500,'Sync state missing')
        return {'last_applied_sequence':row.last_applied_sequence}

    @app.post('/api/v1/sync/events')
    def accept_event(req:SyncEvent,station:Station=Depends(station_auth),session:Session=Depends(db)):
        event_id=str(req.event_id)
        payload=req.payload.model_dump(mode='json')
        payload_hash=digest(payload)
        existing=session.get(SyncInbox,(station.id,event_id))
        if existing:
            if (existing.payload_sha256 != payload_hash or existing.stream_sequence != req.stream_sequence
                    or existing.event_type != req.event_type):
                raise HTTPException(409,{'code':'EVENT_ID_CONFLICT','message':'Event id reused with different content'})
            if existing.apply_state=='APPLIED':
                return {'status':'duplicate','event_id':event_id,'sequence':req.stream_sequence,
                        'payload_sha256':payload_hash}
            raise HTTPException(409,{'code':'EVENT_NOT_APPLIED','message':existing.apply_state})

        state=session.scalar(select(StationSyncState).where(StationSyncState.station_id==station.id).with_for_update())
        if not state:raise HTTPException(500,'Sync state missing')
        if req.stream_sequence != state.last_applied_sequence+1:
            raise HTTPException(409,{'code':'SEQUENCE_GAP_OR_REUSE','expected':state.last_applied_sequence+1,
                                     'got':req.stream_sequence})
        run_id=str(req.payload.run_id)
        run=session.get(ImportedRun,run_id)
        if run:
            raise HTTPException(409,{'code':'RUN_ID_CONFLICT','message':'Run identifier already imported'})
        previous=session.scalar(select(ImportedRun).where(
            ImportedRun.station_id==station.id,ImportedRun.source_run_id==req.payload.source_run_id))
        if previous:raise HTTPException(409,{'code':'SOURCE_RUN_CONFLICT','message':'Source run already imported'})
        instant=now_utc()
        session.add(ImportedRun(run_id=run_id,station_id=station.id,source_run_id=req.payload.source_run_id,
             unit_serial=req.payload.unit_serial,unit_model=req.payload.unit_model,
             started_at=req.payload.started_at,finished_at=req.payload.finished_at,
             verdict=req.payload.verdict,payload_json=payload,payload_sha256=payload_hash))
        session.add(SyncInbox(station_id=station.id,event_id=event_id,stream_sequence=req.stream_sequence,
                              event_type=req.event_type,payload_sha256=payload_hash,
                              apply_state='APPLIED',received_at=instant,applied_at=instant))
        state.last_applied_sequence=req.stream_sequence
        try:session.commit()
        except IntegrityError:
            session.rollback()
            raise HTTPException(409,{'code':'CONCURRENT_CONFLICT','message':'Retry after refreshing sync-state'})
        return {'status':'applied','event_id':event_id,'sequence':req.stream_sequence,
                'payload_sha256':payload_hash}

    @app.get('/api/v1/admin/stations/{station_id}/runs',dependencies=[Depends(admin)])
    def station_runs(station_id:UUID,session:Session=Depends(db)):
        rows=session.scalars(select(ImportedRun).where(ImportedRun.station_id==str(station_id)).order_by(ImportedRun.received_at.desc()).limit(100)).all()
        return [{'run_id':r.run_id,'source_run_id':r.source_run_id,'unit_serial':r.unit_serial,
                 'unit_model':r.unit_model,'verdict':r.verdict,'payload_sha256':r.payload_sha256} for r in rows]

    from miltech_delivery.server_api import delivery_router
    app.include_router(delivery_router(db, admin, station_auth,
                                       delivery_storage_root, delivery_trust_keys))
    return app

app = create_app()
