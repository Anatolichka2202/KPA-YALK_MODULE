"""Process-level reference ResourceBroker; production C++ Core will own leases.

The broker returns the bound socket to its owner. It MUST NOT bind/release and
then tell a separate application to re-bind (TOCTOU gap).
"""
from __future__ import annotations
import hashlib
import os
import socket
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

from miltech_server.contracts import SocketSpec


def bind_exclusive(spec:SocketSpec) -> socket.socket:
    if spec.direction != 'listen':
        raise ValueError('Only local listening sockets can be reserved')
    mode=socket.SOCK_STREAM if spec.protocol=='tcp' else socket.SOCK_DGRAM
    s=socket.socket(socket.AF_INET,mode)
    try:
        if os.name=='nt' and hasattr(socket,'SO_EXCLUSIVEADDRUSE'):
            s.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
        # Do NOT enable SO_REUSEADDR / SO_REUSEPORT.
        s.bind((str(spec.address),spec.port))
        if spec.protocol=='tcp':s.listen(16)
        return s
    except BaseException:
        s.close();raise


class FileLease:
    """OS advisory lock for serial/VISA/USB resource ownership.
    Every cooperating MilTech process MUST use the same lock root and id.
    """
    def __init__(self,lock_root:str,resource_id:str):
        root=Path(lock_root);root.mkdir(parents=True,exist_ok=True)
        identifier=hashlib.sha256(resource_id.casefold().encode('utf-8')).hexdigest()
        self.path=root/(identifier+'.lock')
        self.fp=None

    def __enter__(self):
        self.fp=open(self.path,'a+b')
        try:
            if os.name=='nt':
                import msvcrt
                self.fp.seek(0);self.fp.write(b'1');self.fp.flush()
                self.fp.seek(0);msvcrt.locking(self.fp.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(self.fp.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BaseException:
            self.fp.close();self.fp=None;raise
        return self

    def __exit__(self,*_):
        if self.fp:
            if os.name=='nt':
                import msvcrt
                self.fp.seek(0);msvcrt.locking(self.fp.fileno(),msvcrt.LK_UNLCK,1)
            else:
                import fcntl
                fcntl.flock(self.fp.fileno(),fcntl.LOCK_UN)
            self.fp.close();self.fp=None
