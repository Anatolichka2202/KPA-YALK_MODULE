"""Integration contract: signed release -> server assignment -> verified edge -> rollback."""
from __future__ import annotations

import hashlib
import json
import os
import zipfile
from pathlib import Path
from uuid import uuid4

import httpx
import pytest
from fastapi.testclient import TestClient

from miltech_delivery.package import (BundleError, build_bundle, generate_publisher_key,
                                      read_trust_file, verify_bundle, validate_relpath)
from miltech_delivery.installer import InstallError, ReleaseInstaller
from miltech_delivery.edge_client import DeliveryClient, DeliveryNetworkError, validate_server_url
from miltech_server.db import build_engine, init_dev_db
from miltech_server.main import create_app


@pytest.fixture
def publisher(tmp_path):
    key = tmp_path / 'ci-private.pem'
    trust = tmp_path / 'ci-public.json'
    generate_publisher_key(key, trust, 'lab-ci')
    return key, read_trust_file(trust)


def make_bundle(tmp_path, publisher, version='1.0.0', large=False):
    private, trust = publisher
    rid = str(uuid4())
    src = tmp_path / ('source-' + rid)
    for name, data in {
        'application/Station.exe': os.urandom(1100000) if large else b'EXE-TEST-ONLY',
        'plugins/KTMA.dll': b'PLUGIN-VERSION-' + version.encode(),
        'tu/TEST_EXE/TuCheck.exe': b'TU-EXE',
        'scenarios/KTMA-01.json': b'{"version":1}',
        'catalog/parameters.json': b'{"word":"orbita"}',
    }.items():
        output = src / name
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(data)
    spec = {'release_id': rid, 'version': version, 'target_platform': 'windows-x64',
            'publisher_key_id': 'lab-ci', 'required_capabilities': ['qt-core'],
            'created_at': '2026-11-23T11:30:00Z',
            'components': [
                {'id': 'org.miltech.station.core', 'kind': 'core', 'version': version,
                 'paths': ['application/Station.exe']},
                {'id': 'org.miltech.station.ktma', 'kind': 'plugin', 'version': version,
                 'paths': ['plugins/KTMA.dll']},
                {'id': 'org.miltech.station.tu', 'kind': 'tu', 'version': version,
                 'paths': ['tu/TEST_EXE/TuCheck.exe']},
                {'id': 'org.miltech.station.scenarios', 'kind': 'scenario', 'version': version,
                 'paths': ['scenarios/KTMA-01.json']},
                {'id': 'org.miltech.station.catalog', 'kind': 'catalog', 'version': version,
                 'paths': ['catalog/parameters.json']},
            ]}
    bundle_path = tmp_path / ('release-' + rid + '.mtbundle')
    verified = build_bundle(spec, src, private, bundle_path)
    assert verified.bundle_sha256 == hashlib.sha256(bundle_path.read_bytes()).hexdigest()
    return rid, bundle_path, verified


def make_installer(tmp_path, trust):
    return ReleaseInstaller(tmp_path / 'ProgramFiles' / 'Station', tmp_path / 'ProgramData' / 'Station',
                            trust, 'windows-x64', {'qt-core'})


def test_signed_bundle_staging_does_not_touch_station_data(tmp_path, publisher):
    rid, bundle, _ = make_bundle(tmp_path, publisher)
    inst = make_installer(tmp_path, publisher[1])
    db = inst.data_root / 'db' / 'runs.db'
    db.parent.mkdir(parents=True)
    db.write_bytes(b'EXISTING_RUNS_DB_MUST_SURVIVE')
    assert inst.stage(bundle)['status'] == 'staged'
    assert inst.stage(bundle)['status'] == 'already_staged'
    assert (inst.release_root / rid / 'plugins' / 'KTMA.dll').read_bytes() == b'PLUGIN-VERSION-1.0.0'
    assert db.read_bytes() == b'EXISTING_RUNS_DB_MUST_SURVIVE'
    assert inst.status()['active'] is None


def test_boot_confirmation_and_automatic_fallback(tmp_path, publisher):
    rid1, bundle1, _ = make_bundle(tmp_path, publisher, '1.0.0')
    rid2, bundle2, _ = make_bundle(tmp_path, publisher, '1.1.0')
    inst = make_installer(tmp_path, publisher[1])
    inst.stage(bundle1); inst.stage(bundle2)
    with pytest.raises(InstallError, match='Maintenance'):
        inst.request_activation(rid1)
    inst.request_activation(rid1, maintenance_confirmed=True)
    assert inst.resolve_boot()['release_id'] == rid1
    assert inst.confirm_boot(rid1)['active'] == rid1
    inst.request_activation(rid2, maintenance_confirmed=True)
    assert inst.status()['active'] == rid1  # Not until readiness confirmation
    assert inst.resolve_boot() == {'release_id': rid2, 'release_root': str(inst.release_root / rid2), 'pending': True}
    # New process crashed before reporting readiness. Bootstrap next start returns known-good.
    assert inst.resolve_boot()['release_id'] == rid1
    assert inst.status()['pending'] is None
    inst.request_activation(rid2, maintenance_confirmed=True)
    assert inst.resolve_boot()['release_id'] == rid2
    assert inst.confirm_boot(rid2)['active'] == rid2
    assert inst.status()['previous'] == rid1
    inst.request_rollback(maintenance_confirmed=True)
    assert inst.resolve_boot()['release_id'] == rid1
    assert inst.confirm_boot(rid1)['active'] == rid1


def test_tampered_bundle_and_untrusted_key_fail_closed(tmp_path, publisher):
    rid, bundle, _ = make_bundle(tmp_path, publisher)
    assert verify_bundle(bundle, publisher[1]).manifest['release_id'] == rid
    with pytest.raises(BundleError, match='not trusted'):
        verify_bundle(bundle, {})
    with zipfile.ZipFile(bundle) as z:
        entries = {name: z.read(name) for name in z.namelist()}
    entries['payload/application/Station.exe'] = b'REPLACED-EXE'
    modified = tmp_path / 'tampered.mtbundle'
    with zipfile.ZipFile(modified, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, content in entries.items():
            z.writestr(name, content)
    with pytest.raises(BundleError, match='size mismatch|checksum'):
        verify_bundle(modified, publisher[1])
    modified_manifest = json.loads(entries['manifest.json'])
    modified_manifest['version'] = '9.9.9'
    entries['manifest.json'] = json.dumps(modified_manifest).encode()
    with zipfile.ZipFile(modified, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, content in entries.items():
            z.writestr(name, content)
    with pytest.raises(BundleError, match='signature'):
        verify_bundle(modified, publisher[1])


def test_forbidden_paths_and_extra_entries(tmp_path, publisher):
    for path in ('../x', '/absolute', 'config/site.json', 'application/../../evil',
                 'application/C:\\Windows', 'plugins/CON.txt', 'tu/a//b', 'plugins/x.', 'plugins\\evil'):
        with pytest.raises(BundleError):
            validate_relpath(path)
    rid, bundle, _ = make_bundle(tmp_path, publisher)
    with zipfile.ZipFile(bundle, 'a') as z:
        z.writestr('payload/../../escape.txt', b'evil')
    with pytest.raises(BundleError):
        verify_bundle(bundle, publisher[1])


def test_mismatched_capability_platform_and_tampered_install(tmp_path, publisher):
    rid, bundle, _ = make_bundle(tmp_path, publisher)
    wrong = ReleaseInstaller(tmp_path / 'other', tmp_path / 'otherdata', publisher[1], 'linux-x64')
    with pytest.raises(InstallError, match='platform'):
        wrong.stage(bundle)
    wrong_cap = ReleaseInstaller(tmp_path / 'other2', tmp_path / 'otherdata2', publisher[1], 'windows-x64')
    with pytest.raises(InstallError, match='capabilities'):
        wrong_cap.stage(bundle)
    inst = make_installer(tmp_path, publisher[1]); inst.stage(bundle)
    (inst.release_root / rid / 'application' / 'Station.exe').write_bytes(b'MODIFIED')
    with pytest.raises(InstallError, match='modified|hash'):
        inst.request_activation(rid, maintenance_confirmed=True)


def test_delivery_registration_assignment_download_and_reports(tmp_path, publisher):
    rid, bundle, verified = make_bundle(tmp_path, publisher, large=True)
    engine = build_engine('sqlite:///' + str(tmp_path / 'enterprise.sqlite'))
    init_dev_db(engine)
    app = create_app(engine=engine, admin_key='dev-admin',
                     delivery_storage_root=str(tmp_path / 'artifact-storage'),
                     delivery_trust_keys=publisher[1])
    with TestClient(app) as client:
        admin = {'X-Admin-Key': 'dev-admin'}
        assert client.post('/api/v1/admin/delivery/releases', content=b'bad', headers=admin).status_code == 415
        no_key = client.post('/api/v1/admin/delivery/releases', content=bundle.read_bytes(),
                             headers={'Content-Type':'application/octet-stream'})
        assert no_key.status_code == 403
        upload_headers = {**admin, 'Content-Type':'application/octet-stream'}
        upload = client.post('/api/v1/admin/delivery/releases', headers=upload_headers,
                             content=bundle.read_bytes())
        assert upload.status_code == 201, upload.text
        assert upload.json()['status'] == 'imported'
        assert client.post('/api/v1/admin/delivery/releases', headers=upload_headers,
                           content=bundle.read_bytes()).json()['status'] == 'already_imported'
        assert client.post('/api/v1/admin/sites', headers=admin,
                           json={'code':'PLANT','title':'Factory'}).status_code == 201
        registration = client.post('/api/v1/admin/stations', headers=admin,
                                   json={'site_code':'PLANT', 'station_code':'STAND01','title':'Station'}).json()
        auth = {'Authorization':'Bearer '+registration['station_token']}
        station_id = registration['station_id']
        url = f'/api/v1/admin/delivery/stations/{station_id}/target'
        assert client.get('/api/v1/stations/me/delivery/desired', headers=auth).json()['status']=='unassigned'
        assert client.get(f'/api/v1/stations/me/delivery/bundles/{rid}', headers=auth).status_code == 404
        assignment = client.put(url, headers=admin, json={'release_id':rid,'channel':'pilot'})
        assert assignment.status_code == 200, assignment.text
        assert assignment.json()['revision'] == 1
        assert client.put(url, headers=admin, json={'release_id':rid,'channel':'pilot'}).json()['status']=='unchanged'
        desired = client.get('/api/v1/stations/me/delivery/desired', headers=auth)
        assert desired.status_code == 200
        assert desired.json()['bundle_sha256'] == verified.bundle_sha256
        download = client.get(f'/api/v1/stations/me/delivery/bundles/{rid}', headers=auth)
        assert download.status_code == 200 and download.content == bundle.read_bytes()
        event_id = str(uuid4())
        report = {'event_id':event_id,'release_id':rid,'target_revision':1,'state':'STAGED'}
        assert client.post('/api/v1/stations/me/delivery/reports',headers=auth,json=report).json()['status']=='recorded'
        assert client.post('/api/v1/stations/me/delivery/reports',headers=auth,json=report).json()['status']=='duplicate'
        report['state'] = 'ACTIVE'
        assert client.post('/api/v1/stations/me/delivery/reports',headers=auth,json=report).status_code == 409
        assert client.get(f'/api/v1/admin/delivery/stations/{station_id}/reports',headers=admin).json()[0]['state']=='STAGED'

        def relay(request: httpx.Request) -> httpx.Response:
            response = client.request(request.method, request.url.path,
                                      headers=dict(request.headers), content=request.content)
            return httpx.Response(response.status_code, headers=dict(response.headers), content=response.content)
        downloaded = DeliveryClient('https://enterprise.example', registration['station_token'],
                                    publisher[1], tmp_path / 'edge-cache', httpx.MockTransport(relay))
        got = downloaded.pull()
        assert got['status'] == 'downloaded'
        assert got['release_id'] == rid
        assert downloaded.pull()['status'] == 'cached'
        inst = make_installer(tmp_path, publisher[1])
        assert inst.stage(got['path'])['status'] == 'staged'
    engine.dispose()


def test_assignment_revision_history_and_disable(tmp_path, publisher):
    rid1, b1, _ = make_bundle(tmp_path, publisher, '1.0.0')
    rid2, b2, _ = make_bundle(tmp_path, publisher, '1.1.0')
    engine = build_engine('sqlite:///' + str(tmp_path / 'db.sqlite'))
    init_dev_db(engine)
    app = create_app(engine=engine, admin_key='dev-admin', delivery_storage_root=str(tmp_path / 'objects'),
                     delivery_trust_keys=publisher[1])
    with TestClient(app) as client:
        adm={'X-Admin-Key':'dev-admin'}
        upload_headers={**adm,'Content-Type':'application/octet-stream'}
        for b in (b1,b2):
            assert client.post('/api/v1/admin/delivery/releases',headers=upload_headers,content=b.read_bytes()).status_code==201
        assert client.post('/api/v1/admin/sites',headers=adm,json={'code':'PLANT','title':'Site'}).status_code==201
        reg=client.post('/api/v1/admin/stations',headers=adm,json={
            'site_code':'PLANT','station_code':'ST-01','title':'Bench'}).json()
        sid=reg['station_id'];tok={'Authorization':'Bearer '+reg['station_token']}
        route=f'/api/v1/admin/delivery/stations/{sid}/target'
        assert client.put(route,headers=adm,json={'release_id':rid1}).json()['revision']==1
        assert client.put(route,headers=adm,json={'release_id':rid2}).json()['revision']==2
        assert client.get(f'/api/v1/stations/me/delivery/bundles/{rid1}',headers=tok).status_code==404
        report={'event_id':str(uuid4()),'release_id':rid1,'target_revision':1,'state':'ACTIVE'}
        assert client.post('/api/v1/stations/me/delivery/reports',headers=tok,json=report).status_code==200
        report['event_id']=str(uuid4());report['target_revision']=2
        assert client.post('/api/v1/stations/me/delivery/reports',headers=tok,json=report).status_code==409
        assert client.post(f'/api/v1/admin/stations/{sid}/disable',headers=adm).status_code==200
        assert client.get('/api/v1/stations/me/delivery/desired',headers=tok).status_code==401
    engine.dispose()


def test_url_requires_https_and_failure_is_non_destructive(tmp_path, publisher):
    for uri in ('http://172.16.10.1','http://enterprise.example', 'https://user:pass@enterprise.example',
                'https://enterprise.example/path', 'ftp://enterprise.example'):
        with pytest.raises(ValueError):
            validate_server_url(uri)
    assert validate_server_url('http://localhost:8000') == 'http://localhost:8000'
    def unavailable(req):
        raise httpx.ConnectError('link down')
    c = DeliveryClient('https://enterprise.example','station-token',publisher[1],tmp_path/'offline',
                       httpx.MockTransport(unavailable))
    with pytest.raises(DeliveryNetworkError):
        c.pull()
    assert not any((tmp_path/'offline').glob('*.mtbundle'))


def test_report_outbox_retries_without_duplicate(tmp_path, publisher):
    from miltech_delivery.reports import DeliveryReportOutbox
    rid, _, _ = make_bundle(tmp_path, publisher)
    path = tmp_path / 'report-outbox.sqlite3'
    out = DeliveryReportOutbox(path)
    try:
        first = out.enqueue(rid,1,'STAGED')
        assert out.enqueue(rid,1,'STAGED',event_id=first['event_id'])['status']=='existing'
        with pytest.raises(ValueError):
            out.enqueue(rid,1,'ACTIVE',event_id=first['event_id'])
        assert len(out.pending()) == 1
        result = out.sync('https://server.example','station-token',httpx.MockTransport(
            lambda req: (_ for _ in ()).throw(httpx.ConnectError('offline'))))
        assert result['remaining']==1
    finally:
        out.close()
    reopened = DeliveryReportOutbox(path)
    assert len(reopened.pending())==1
    def accepted(req):
        return httpx.Response(200, json={'status':'recorded','event_id':first['event_id']})
    assert reopened.sync('https://server.example','station-token',httpx.MockTransport(accepted))['sent']==1
    assert reopened.pending()==[]
    assert reopened.sync('https://server.example','station-token',httpx.MockTransport(accepted))['sent']==0
    reopened.close()


def test_delivery_disabled_without_trust_or_storage(tmp_path):
    engine = build_engine('sqlite:///' + str(tmp_path/'disabled.sqlite'))
    init_dev_db(engine)
    app=create_app(engine=engine,admin_key='dev-admin',delivery_storage_root=None,delivery_trust_keys={})
    with TestClient(app) as c:
        r=c.post('/api/v1/admin/delivery/releases',headers={
            'X-Admin-Key':'dev-admin','Content-Type':'application/octet-stream'},content=b'unknown')
        assert r.status_code==503
    engine.dispose()


def test_installed_release_extra_file_is_rejected(tmp_path, publisher):
    rid, bundle, _ = make_bundle(tmp_path, publisher)
    inst=make_installer(tmp_path, publisher[1]);inst.stage(bundle)
    (inst.release_root/rid/'plugins'/'extra.dll').write_bytes(b'unlisted')
    with pytest.raises(InstallError, match='Unexpected'):
        inst.request_activation(rid,maintenance_confirmed=True)
