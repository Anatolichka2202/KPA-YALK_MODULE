"""Station profile/OT contract. All network configuration is DECLARATIVE.
Applying IP/VLAN/firewall changes is intentionally outside the service.
"""
from datetime import datetime
from enum import Enum
from ipaddress import IPv4Address, IPv4Interface
from typing import Literal
from uuid import UUID
from pydantic import BaseModel, Field, ConfigDict, model_validator

class Strict(BaseModel):
    model_config = ConfigDict(extra='forbid')

class InterfaceSpec(Strict):
    role: Literal['enterprise','ot','service','loopback']
    alias: str = Field(min_length=1,max_length=150)
    vlan_id: int | None = Field(default=None,ge=1,le=4094)
    ipv4: IPv4Interface
    gateway: IPv4Address | None = None
    default_route: bool = False
    skip_as_source: bool = False

class SocketSpec(Strict):
    name: str = Field(min_length=1, max_length=120)
    protocol: Literal['tcp','udp']
    direction: Literal['listen','connect']
    address: IPv4Address
    port: int = Field(ge=1, le=65535)
    interface_role: Literal['enterprise','ot','service','loopback']
    owner: str = Field(min_length=1,max_length=160)

class ResourceSpec(Strict):
    id: str = Field(min_length=1,max_length=200)
    kind: Literal['rs485','serial','usb','visa','can','ethernet','gpio','other']
    owner: str = Field(min_length=1,max_length=160)
    exclusive: bool = True

class Profile(Strict):
    schema_version: Literal[1] = 1
    station_code: str = Field(min_length=1,max_length=120)
    interfaces: list[InterfaceSpec] = Field(min_length=1,max_length=24)
    sockets: list[SocketSpec] = Field(default_factory=list,max_length=200)
    resources: list[ResourceSpec] = Field(default_factory=list,max_length=250)
    required_capabilities: list[str] = Field(default_factory=list,max_length=200)

    @model_validator(mode='after')
    def check_network_safety(self):
        if sum(i.default_route for i in self.interfaces)>1:
            raise ValueError('more than one default route is not supported by station profile')
        identities=set()
        roles={i.role for i in self.interfaces}
        address_roles={}
        for iface in self.interfaces:
            k=(iface.alias,iface.vlan_id, str(iface.ipv4.ip))
            if k in identities:
                raise ValueError(f'duplicate interface assignment: {k}')
            identities.add(k)
            address_roles.setdefault((str(iface.ipv4.ip),iface.role),True)
            if iface.role == 'ot' and (iface.default_route or iface.gateway is not None):
                raise ValueError('OT interface must not use default_route or gateway')
            if iface.role == 'loopback' and not iface.ipv4.ip.is_loopback:
                raise ValueError('loopback interface must use 127.0.0.0/8')
            if iface.role != 'loopback' and iface.ipv4.ip.is_loopback:
                raise ValueError('loopback address on physical interface')
            if iface.default_route and iface.role != 'enterprise':
                raise ValueError('default route only allowed via enterprise interface')
            if iface.gateway is not None and iface.gateway not in iface.ipv4.network:
                raise ValueError('gateway outside interface subnet')
        socket_names=set(); binds=[]
        for s in self.sockets:
            if s.name in socket_names:raise ValueError('duplicate socket name')
            socket_names.add(s.name)
            if s.interface_role not in roles:raise ValueError('socket interface role missing')
            if s.direction == 'listen':
                if s.address.is_unspecified:raise ValueError('wildcard bind prohibited: use explicit local IP')
                if (str(s.address),s.interface_role) not in address_roles:
                    raise ValueError('listen address not assigned to interface role')
                for previous in binds:
                    # Same port/protocol on the same address; wildcard is already forbidden.
                    if previous.protocol == s.protocol and previous.port == s.port and previous.address == s.address:
                        raise ValueError('conflicting port binding')
                binds.append(s)
        names=set()
        for r in self.resources:
            if r.exclusive and r.id.casefold() in names:
                raise ValueError('duplicate exclusive hardware resource')
            if r.exclusive:names.add(r.id.casefold())
        return self

class SiteCreate(Strict):
    code: str = Field(min_length=1,max_length=100)
    title: str = Field(min_length=1,max_length=250)

class StationCreate(Strict):
    site_code: str
    station_code: str = Field(min_length=1,max_length=120)
    title: str = Field(min_length=1,max_length=250)

class Heartbeat(Strict):
    observed_at: datetime
    agent_version: str = Field(min_length=1,max_length=120)
    inventory: dict = Field(default_factory=dict)
    clock_status: Literal['SYNCED','UNSYNCED','UNKNOWN'] = 'UNKNOWN'

class RunPayload(Strict):
    schema_version: Literal[1] = 1
    run_id: UUID
    source_run_id: str = Field(min_length=1,max_length=160)
    unit_serial: str = Field(min_length=1,max_length=200)
    unit_model: str = Field(min_length=1,max_length=200)
    scenario_id: str = Field(min_length=1,max_length=200)
    scenario_version: str = Field(min_length=1,max_length=120)
    catalog_version: str = Field(min_length=1,max_length=120)
    profile_version: str = Field(min_length=1,max_length=120)
    software_release: str = Field(min_length=1,max_length=120)
    started_at: datetime
    finished_at: datetime
    verdict: Literal['NORM','NOT_NORM','STAND_ERROR','INCOMPLETE','STOPPED','NOT_EXECUTED']
    composition_snapshot: list[dict] = Field(default_factory=list,max_length=200)
    artifacts: list[dict] = Field(default_factory=list,max_length=250)
    clock_status: Literal['SYNCED','UNSYNCED','UNKNOWN'] = 'UNKNOWN'

    @model_validator(mode='after')
    def check_run(self):
        if self.started_at.tzinfo is None or self.finished_at.tzinfo is None:
            raise ValueError('run timestamps must have timezone')
        if self.finished_at < self.started_at:
            raise ValueError('finished_at before started_at')
        return self

class SyncEvent(Strict):
    event_id: UUID
    stream_sequence: int = Field(ge=1)
    event_type: Literal['RUN_COMPLETED']
    payload: RunPayload
