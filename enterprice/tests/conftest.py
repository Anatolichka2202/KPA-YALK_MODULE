import pytest
from pathlib import Path
from fastapi.testclient import TestClient
from miltech_server.db import build_engine,init_dev_db
from miltech_server.main import create_app

@pytest.fixture
def sample(tmp_path):
    engine=build_engine('sqlite:///'+str(tmp_path/'server.sqlite'))
    init_dev_db(engine)
    app=create_app(engine=engine,admin_key='test-only-secret')
    client=TestClient(app)
    headers={'X-Admin-Key':'test-only-secret'}
    site=client.post('/api/v1/admin/sites',headers=headers,json={'code':'PLANT','title':'Pilot plant'})
    assert site.status_code==201,site.text
    reg=client.post('/api/v1/admin/stations',headers=headers,json={'site_code':'PLANT','station_code':'STAND-01','title':'Test station'})
    assert reg.status_code==201,reg.text
    yield client,headers,reg.json()
    client.close();engine.dispose()
