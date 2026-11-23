"""Read-only station environment validation. NO IP/route/firewall mutations."""
import ipaddress
import json
import os
import platform
from pathlib import Path
from typing import Any
import psutil
import yaml
from miltech_server.contracts import Profile


def load_profile(path:str)->Profile:
    return Profile.model_validate(yaml.safe_load(Path(path).read_text(encoding='utf-8')))


def inspect_host()->dict:
    interfaces={}
    prefixes={}
    for name,items in psutil.net_if_addrs().items():
        interfaces[name]=[x.address.split('%')[0] for x in items if x.family.name in ('AF_INET','AF_INET6')]
        for item in items:
            if item.family.name == 'AF_INET' and item.netmask:
                try:
                    prefixes[item.address]=ipaddress.IPv4Interface(f'{item.address}/{item.netmask}').network.prefixlen
                except ValueError:pass
    conn=[]
    # Requires no administrator privileges on common Windows configurations for local sockets.
    for c in psutil.net_connections(kind='inet'):
        if c.laddr:
            try:
                port=c.laddr.port; address=c.laddr.ip
            except AttributeError:
                address,port=c.laddr[:2]
            conn.append({'protocol':'tcp' if c.type==1 else 'udp', 'ip':address,'port':port,'status':c.status})
    return {'platform':platform.system(),'interfaces':interfaces,'prefixes':prefixes,'sockets':conn}


def validate_environment(profile:Profile,observed:dict|None=None)->dict:
    observed=observed if observed is not None else inspect_host()
    issues=[]
    seen_ips={ip for ips in observed.get('interfaces',{}).values() for ip in ips}
    for i in profile.interfaces:
        ip=str(i.ipv4.ip)
        if i.role=='loopback':continue
        if ip not in seen_ips:
            issues.append({'level':'ERROR','code':'IP_NOT_ASSIGNED','detail':f'{i.role}: {ip} not found on host'})
        # Alias check: VLAN interfaces may have OS/vendor names, so only warn.
        actual_prefix=observed.get('prefixes',{}).get(ip)
        if actual_prefix is not None and actual_prefix != i.ipv4.network.prefixlen:
            issues.append({'level':'ERROR','code':'PREFIX_MISMATCH','detail':f'{ip}: expected /{i.ipv4.network.prefixlen}, got /{actual_prefix}'})
        if i.alias not in observed.get('interfaces',{}):
            issues.append({'level':'WARNING','code':'NIC_ALIAS_NOT_FOUND','detail':i.alias})
        elif ip not in observed['interfaces'][i.alias]:
            issues.append({'level':'ERROR','code':'IP_ON_DIFFERENT_NIC','detail':f'{ip} expected on {i.alias}'})
    for sock in profile.sockets:
        if sock.direction != 'listen':continue
        for actual in observed.get('sockets',[]):
            if actual['protocol']!=sock.protocol or actual['port']!=sock.port:continue
            if actual['ip'] in (str(sock.address),'0.0.0.0','::'):
                issues.append({'level':'WARNING','code':'PORT_IN_USE','detail':f'{sock.protocol}/{sock.port} on {actual["ip"]}: inspect owner; may be expected service'})
    return {'ready':not any(i['level']=='ERROR' for i in issues),
            'issues':issues, 'platform':observed.get('platform'),
            'note':'Read-only snapshot; cannot prove port availability, VLAN isolation, routing or firewall policy.'}
