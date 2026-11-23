import socket
from pathlib import Path
import pytest
from miltech_edge.resources import FileLease,bind_exclusive
from miltech_server.contracts import SocketSpec

def test_listen_socket_owned_until_close():
    tmp=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    tmp.bind(('127.0.0.1',0));port=tmp.getsockname()[1];tmp.close()
    spec=SocketSpec(name='plugin',protocol='tcp',direction='listen',address='127.0.0.1',
                    port=port,interface_role='loopback',owner='plugin-A')
    first=bind_exclusive(spec)
    try:
        with pytest.raises(OSError):bind_exclusive(spec)
    finally:first.close()
    second=bind_exclusive(spec);second.close()

def test_file_lease_acquire_and_release(tmp_path):
    with FileLease(str(tmp_path),'COM3') as hold:
        assert hold.path.exists()
        with pytest.raises(BlockingIOError):
            with FileLease(str(tmp_path),'com3'):pass
    with FileLease(str(tmp_path),'Com3'):pass
