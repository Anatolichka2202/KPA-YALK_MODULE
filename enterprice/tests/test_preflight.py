from pathlib import Path
import pytest
import yaml
from pydantic import ValidationError
from miltech_edge.preflight import load_profile,validate_environment
from miltech_server.contracts import Profile

FILE=Path(__file__).resolve().parents[1]/'examples'/'station-01.example.yaml'

def test_profile_valid_and_missing_local_ip():
    p=load_profile(str(FILE))
    assert len(p.interfaces)==3
    result=validate_environment(p,{'platform':'test','interfaces':{'Ethernet - MGMT VLAN 130':['10.30.30.11'],'Ethernet - STAND VLAN 2201':['192.168.10.10']},'sockets':[]})
    assert result['ready']
    bad=validate_environment(p,{'platform':'test','interfaces':{},'sockets':[]})
    assert not bad['ready']

def test_ot_gateway_forbidden():
    raw=yaml.safe_load(FILE.read_text(encoding='utf-8'))
    raw['interfaces'][1]['gateway']='192.168.10.1'
    with pytest.raises(ValidationError,match='OT interface'):Profile.model_validate(raw)

def test_binding_overlap_forbidden():
    raw=yaml.safe_load(FILE.read_text(encoding='utf-8'))
    raw['sockets']=[{'name':n,'protocol':'udp','direction':'listen','address':'192.168.10.10','port':1234,'interface_role':'ot','owner':'test'} for n in ['a','b']]
    with pytest.raises(ValidationError,match='conflicting port'):Profile.model_validate(raw)

def test_port_occupied_warns():
    raw=yaml.safe_load(FILE.read_text(encoding='utf-8'))
    raw['sockets']=[{'name':'test','protocol':'udp','direction':'listen','address':'192.168.10.10','port':4567,'interface_role':'ot','owner':'plugin'}]
    p=Profile.model_validate(raw)
    host={'platform':'test','interfaces':{'Ethernet - MGMT VLAN 130':['10.30.30.11'],'Ethernet - STAND VLAN 2201':['192.168.10.10']},'sockets':[{'protocol':'udp','ip':'0.0.0.0','port':4567,'status':'NONE'}]}
    r=validate_environment(p,host)
    assert any(x['code']=='PORT_IN_USE' for x in r['issues'])


def test_prefix_mismatch_rejected():
    p=load_profile(str(FILE))
    obs={'platform':'test','interfaces':{'Ethernet - MGMT VLAN 130':['10.30.30.11'],'Ethernet - STAND VLAN 2201':['192.168.10.10']},'prefixes':{'10.30.30.11':24},'sockets':[]}
    issues=validate_environment(p,obs)
    assert not issues['ready']
    assert any(i['code']=='PREFIX_MISMATCH' for i in issues['issues'])
