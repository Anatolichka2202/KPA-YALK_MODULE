import json
from copy import deepcopy
from uuid import uuid4
from pathlib import Path
from miltech_edge.outbox import Outbox
from miltech_server.main import digest

EXAMPLES=Path(__file__).resolve().parents[1]/'examples'
def run():
    data=json.loads((EXAMPLES/'run-completed.example.json').read_text(encoding='utf-8'))
    data['run_id']=str(uuid4())
    data['source_run_id']=str(uuid4())
    return data

def event(seq=1):return {'event_id':str(uuid4()),'stream_sequence':seq,'event_type':'RUN_COMPLETED','payload':run()}

def auth(reg):return {'Authorization':'Bearer '+reg['station_token']}

def test_register_profile_and_heartbeat(sample):
    client,admin,reg=sample
    profile=__import__('yaml').safe_load((EXAMPLES/'station-01.example.yaml').read_text(encoding='utf-8'))
    r=client.put(f"/api/v1/admin/stations/{reg['station_id']}/profile",headers=admin,json=profile)
    assert r.status_code==200,r.text
    assert r.json()['revision']==1
    assert client.put(f"/api/v1/admin/stations/{reg['station_id']}/profile",headers=admin,json=profile).json()['status']=='unchanged'
    p=client.get('/api/v1/stations/me/profile',headers=auth(reg))
    assert p.status_code==200 and p.json()['profile']['station_code']=='STAND-01'
    hb=client.post('/api/v1/stations/me/heartbeat',headers=auth(reg),json={'observed_at':'2026-11-23T10:00:00Z','agent_version':'0.1','inventory':{'adapters':2},'clock_status':'SYNCED'})
    assert hb.status_code==200,hb.text

def test_auth_closed(sample):
    client,admin,reg=sample
    assert client.post('/api/v1/admin/sites',json={'code':'BAD','title':'x'}).status_code==403
    assert client.get('/api/v1/stations/me/sync-state').status_code==401
    assert client.get('/api/v1/stations/me/sync-state',headers={'Authorization':'Bearer dummy'}).status_code==401

def test_idempotent_ingest(sample):
    client,admin,reg=sample
    e=event(1)
    first=client.post('/api/v1/sync/events',headers=auth(reg),json=e)
    assert first.status_code==200,first.text
    assert first.json()['status']=='applied'
    assert first.json()['payload_sha256']==digest(e['payload'])
    duplicate=client.post('/api/v1/sync/events',headers=auth(reg),json=e)
    assert duplicate.status_code==200 and duplicate.json()['status']=='duplicate'
    assert client.get('/api/v1/stations/me/sync-state',headers=auth(reg)).json()['last_applied_sequence']==1
    runs=client.get(f"/api/v1/admin/stations/{reg['station_id']}/runs",headers=admin)
    assert runs.status_code==200 and len(runs.json())==1

def test_replay_mutation_blocked(sample):
    client,admin,reg=sample
    e=event();assert client.post('/api/v1/sync/events',headers=auth(reg),json=e).status_code==200
    e['payload']['verdict']='NORM'
    r=client.post('/api/v1/sync/events',headers=auth(reg),json=e)
    assert r.status_code==409 and r.json()['detail']['code']=='EVENT_ID_CONFLICT'

def test_out_of_order_and_source_collision(sample):
    client,admin,reg=sample
    e=event(2)
    r=client.post('/api/v1/sync/events',headers=auth(reg),json=e)
    assert r.status_code==409 and r.json()['detail']['expected']==1
    e['stream_sequence']=1
    assert client.post('/api/v1/sync/events',headers=auth(reg),json=e).status_code==200
    e2=event(2)
    e2['payload']['source_run_id']=e['payload']['source_run_id']
    assert client.post('/api/v1/sync/events',headers=auth(reg),json=e2).status_code==409
    assert client.get('/api/v1/stations/me/sync-state',headers=auth(reg)).json()['last_applied_sequence']==1

def test_invalid_run_rejected(sample):
    client,admin,reg=sample
    e=event()
    e['payload']['started_at']='2026-11-23T13:00:00Z'
    e['payload']['finished_at']='2026-11-23T12:00:00Z'
    assert client.post('/api/v1/sync/events',headers=auth(reg),json=e).status_code==422
    e=event();e['payload']['started_at']='2026-11-23T13:00:00'
    assert client.post('/api/v1/sync/events',headers=auth(reg),json=e).status_code==422

def test_outbox_offline_retry_and_replay(sample,tmp_path):
    client,admin,reg=sample
    outbox=Outbox(str(tmp_path/'edge.sqlite'))
    try:
        content=run()
        queued=outbox.enqueue(content)
        assert queued['status']=='queued'
        assert outbox.enqueue(content)['status']=='existing'
        assert len(outbox.pending())==1
        # In-process HTTP transport simulates connection restored; no Internet required.
        import httpx
        def mock_server(request):
            reply=client.post(request.url.path,headers=dict(request.headers),content=request.content)
            return httpx.Response(status_code=reply.status_code,json=reply.json())
        result=outbox.sync('http://localhost',reg['station_token'],httpx.MockTransport(mock_server))
        assert result['sent']==1
        assert outbox.pending()==[]
    finally:outbox.close()

def test_credentials_rotate_disable(sample):
    client,admin,reg=sample
    station=reg['station_id']
    newer=client.post(f'/api/v1/admin/stations/{station}/rotate-token',headers=admin)
    assert newer.status_code==200
    assert client.get('/api/v1/stations/me/sync-state',headers=auth(reg)).status_code==401
    newreg={'station_token':newer.json()['station_token']}
    assert client.get('/api/v1/stations/me/sync-state',headers=auth(newreg)).status_code==200
    assert client.post(f'/api/v1/admin/stations/{station}/disable',headers=admin).status_code==200
    assert client.get('/api/v1/stations/me/sync-state',headers=auth(newreg)).status_code==401

def test_outbox_network_failure_preserves_data(tmp_path):
    import httpx
    outbox=Outbox(str(tmp_path/'offline.sqlite'))
    try:
        outbox.enqueue(run())
        def failing(_request):raise httpx.ConnectError('network down')
        res=outbox.sync('https://server.example','token',httpx.MockTransport(failing))
        assert res['stopped_by']=='network_unavailable'
        assert len(outbox.pending())==1
    finally:outbox.close()


def test_cleartext_remote_refused(tmp_path):
    import pytest
    outbox=Outbox(str(tmp_path/'edge.sqlite'))
    try:
        with pytest.raises(ValueError,match='HTTPS'):
            outbox.sync('http://10.30.50.10:8000','secret')
    finally:outbox.close()
