"""Authenticated pull of assigned release. Bundle signature is checked locally.

No server-provided URL/command is executed. Only a fixed same-origin download path
is accepted. Offline USB/air-gap operation uses installer.stage(path) directly.
"""
from __future__ import annotations

import hashlib
import os
import tempfile
from pathlib import Path
from urllib.parse import urlsplit
from uuid import UUID

import httpx

from miltech_delivery.package import BundleError, verify_bundle, sha256_file
from miltech_delivery.server_api import MAX_UPLOAD


class DeliveryNetworkError(RuntimeError):
    pass


def validate_server_url(base_url: str) -> str:
    url = urlsplit(base_url)
    if (url.username or url.password or url.query or url.fragment or
        url.scheme not in ('https', 'http') or not url.hostname):
        raise ValueError('Invalid Enterprise URL')
    if url.scheme == 'http' and url.hostname not in ('127.0.0.1', 'localhost', '::1'):
        raise ValueError('Remote Enterprise requires HTTPS')
    if url.path not in ('', '/'):
        raise ValueError('Enterprise base URL must not contain a path')
    return base_url.rstrip('/')


class DeliveryClient:
    def __init__(self, url: str, token: str, trust_keys: dict[str, bytes],
                 cache_dir: str | Path, transport: httpx.BaseTransport | None = None):
        self.url = validate_server_url(url)
        if not token:
            raise ValueError('Station bearer token required')
        self.token = token
        self.trust = trust_keys
        self.cache = Path(cache_dir)
        self.transport = transport

    def pull(self) -> dict:
        self.cache.mkdir(parents=True, exist_ok=True)
        headers = {'Authorization': 'Bearer ' + self.token}
        try:
            with httpx.Client(transport=self.transport, timeout=60, follow_redirects=False) as client:
                desired = client.get(self.url + '/api/v1/stations/me/delivery/desired', headers=headers)
                desired.raise_for_status()
                data = desired.json()
                if data['status'] == 'unassigned':
                    return {'status': 'unassigned'}
                rid = str(UUID(data['release_id']))
                if rid != data['release_id']:
                    raise DeliveryNetworkError('Non-canonical release ID')
                expected_path = f'/api/v1/stations/me/delivery/bundles/{rid}'
                if data['download_path'] != expected_path:
                    raise DeliveryNetworkError('Server supplied unexpected bundle path')
                digest = data['bundle_sha256']
                size = data['bundle_size']
                if (not isinstance(digest, str) or len(digest) != 64 or
                    any(c not in '0123456789abcdef' for c in digest) or
                    not isinstance(size, int) or size <= 0 or size > MAX_UPLOAD):
                    raise DeliveryNetworkError('Invalid bundle metadata')
                destination = self.cache / (digest + '.mtbundle')
                if destination.exists():
                    if destination.stat().st_size == size and sha256_file(destination) == digest:
                        vb = verify_bundle(destination, self.trust)
                        if vb.manifest['release_id'] != rid or vb.manifest_sha256 != data['manifest_sha256']:
                            raise DeliveryNetworkError('Cached bundle differs from target')
                        return {'status': 'cached', 'path': str(destination),
                                'release_id': rid, 'target_revision': data['target_revision']}
                    destination.unlink()
                fd, name = tempfile.mkstemp(prefix='download-', suffix='.part', dir=self.cache)
                temp = Path(name)
                try:
                    count = 0
                    h = hashlib.sha256()
                    with os.fdopen(fd, 'wb') as output:
                        with client.stream('GET', self.url + expected_path, headers=headers) as response:
                            response.raise_for_status()
                            if response.status_code != 200:
                                raise DeliveryNetworkError('Unexpected download response')
                            for chunk in response.iter_bytes(1024 * 1024):
                                count += len(chunk)
                                if count > size:
                                    raise DeliveryNetworkError('Download exceeded signed size')
                                output.write(chunk)
                                h.update(chunk)
                        output.flush(); os.fsync(output.fileno())
                    if count != size or h.hexdigest() != digest:
                        raise DeliveryNetworkError('Downloaded bundle size/hash mismatch')
                    vb = verify_bundle(temp, self.trust)
                    if vb.manifest['release_id'] != rid or vb.manifest_sha256 != data['manifest_sha256']:
                        raise DeliveryNetworkError('Downloaded release identity mismatch')
                    os.replace(temp, destination)
                finally:
                    temp.unlink(missing_ok=True)
                return {'status': 'downloaded', 'path': str(destination),
                        'release_id': rid, 'target_revision': data['target_revision']}
        except (httpx.HTTPError, OSError, KeyError, TypeError, ValueError, BundleError) as e:
            raise DeliveryNetworkError('Release pull failed: ' + str(e)) from e
