"""Signed, immutable MilTech Station release bundles.

No code execution, package script hooks, URL fetching, or OS configuration takes
place here. The signing root is configured OUTSIDE the downloaded bundle.
"""
from __future__ import annotations

import base64
import hashlib
import json
import os
import re
import stat
import tempfile
import zipfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from uuid import UUID

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

FORMAT = 'miltech.release.v1'
MAX_FILES = 4000
MAX_TOTAL_BYTES = 2 * 1024 * 1024 * 1024
MAX_MANIFEST_BYTES = 2 * 1024 * 1024
MAX_COMPRESSION_RATIO = 250
ALLOWED_ROOTS = ('application/', 'plugins/', 'scenarios/', 'catalog/',
                 'profiles/defaults/', 'runtime/', 'tu/')
KINDS = frozenset({'core', 'plugin', 'tu', 'scenario', 'catalog', 'profile', 'runtime'})
NAME_RE = re.compile(r'^[a-z0-9][a-z0-9._-]{1,100}$')
VERSION_RE = re.compile(r'^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9.-]+)?$')
HEX_RE = re.compile(r'^[0-9a-f]{64}$')
WINDOWS_RESERVED = {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)),
                    *(f'LPT{i}' for i in range(1, 10))}


class BundleError(ValueError):
    pass


def canonical_json(value: dict) -> bytes:
    return json.dumps(value, sort_keys=True, ensure_ascii=False,
                      separators=(',', ':'), allow_nan=False).encode('utf-8')


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as handle:
        while part := handle.read(1024 * 1024):
            h.update(part)
    return h.hexdigest()


def validate_relpath(path: str) -> str:
    if not isinstance(path, str) or not path or len(path) > 230 or '\\' in path or '\x00' in path:
        raise BundleError('Invalid release-relative file path')
    if path.startswith('/') or ':' in path or any(ord(ch) < 32 for ch in path):
        raise BundleError(f'Absolute/drive/control path forbidden: {path!r}')
    parts = path.split('/')
    if any(x in ('', '.', '..') or x.endswith((' ', '.')) for x in parts):
        raise BundleError(f'Unsafe path segment: {path!r}')
    if any(x.split('.')[0].upper() in WINDOWS_RESERVED for x in parts):
        raise BundleError(f'Windows reserved filename: {path!r}')
    if not any(path.startswith(prefix) for prefix in ALLOWED_ROOTS):
        raise BundleError(f'Forbidden release content root: {path!r}')
    return path


def check_manifest(manifest: dict) -> list[dict]:
    if not isinstance(manifest, dict) or manifest.get('format') != FORMAT:
        raise BundleError('Unsupported release format')
    try:
        rid = str(UUID(manifest['release_id']))
    except (ValueError, KeyError, TypeError) as e:
        raise BundleError('release_id must be UUID') from e
    if rid != manifest['release_id']:
        raise BundleError('Canonical release_id required')
    if not isinstance(manifest.get('version'), str) or not VERSION_RE.fullmatch(manifest['version']):
        raise BundleError('Invalid semver')
    if manifest.get('target_platform') not in ('windows-x64', 'windows-arm64', 'linux-x64'):
        raise BundleError('Unsupported target_platform')
    if not isinstance(manifest.get('publisher_key_id'), str) or not NAME_RE.fullmatch(manifest['publisher_key_id']):
        raise BundleError('Invalid publisher_key_id')
    required_caps = manifest.get('required_capabilities', [])
    if (not isinstance(required_caps, list) or len(required_caps) > 100 or
        any(not isinstance(c, str) or not NAME_RE.fullmatch(c) for c in required_caps) or
        len(set(required_caps)) != len(required_caps)):
        raise BundleError('Invalid required_capabilities')
    components = manifest.get('components')
    if not isinstance(components, list) or not components or len(components) > 100:
        raise BundleError('At least one component is required')
    files = []
    component_ids = set()
    paths = set()
    for component in components:
        if not isinstance(component, dict) or not NAME_RE.fullmatch(str(component.get('id', ''))):
            raise BundleError('Invalid component ID')
        if component['id'] in component_ids:
            raise BundleError('Duplicate component ID')
        component_ids.add(component['id'])
        if component.get('kind') not in KINDS or not VERSION_RE.fullmatch(str(component.get('version', ''))):
            raise BundleError('Invalid component kind/version')
        items = component.get('files')
        if not isinstance(items, list) or not items:
            raise BundleError('Component must contain files')
        for entry in items:
            if not isinstance(entry, dict):
                raise BundleError('Invalid file descriptor')
            path = validate_relpath(entry.get('path'))
            if path.casefold() in paths:
                raise BundleError('Duplicate or case-colliding file path')
            paths.add(path.casefold())
            if (not isinstance(entry.get('size'), int) or isinstance(entry.get('size'), bool) or
                entry['size'] < 0 or entry['size'] > MAX_TOTAL_BYTES or
                not isinstance(entry.get('sha256'), str) or not HEX_RE.fullmatch(entry['sha256'])):
                raise BundleError('Invalid file size/hash')
            files.append(entry)
    if len(files) > MAX_FILES or sum(f['size'] for f in files) > MAX_TOTAL_BYTES:
        raise BundleError('Release exceeds file/size limits')
    return files


def read_trust_file(path: str | Path) -> dict[str, bytes]:
    """External file: {"publisher-key-id": "base64-encoded 32-byte Ed25519 public key"}."""
    root = json.loads(Path(path).read_text(encoding='utf-8'))
    if not isinstance(root, dict):
        raise BundleError('Trust store must be an object')
    result = {}
    for key_id, encoded in root.items():
        if not isinstance(key_id, str) or not NAME_RE.fullmatch(key_id) or not isinstance(encoded, str):
            raise BundleError('Invalid trust store entry')
        try:
            raw = base64.b64decode(encoded, validate=True)
        except Exception as e:
            raise BundleError('Invalid public key encoding') from e
        if len(raw) != 32:
            raise BundleError('Ed25519 public key must have 32 bytes')
        result[key_id] = raw
    return result


@dataclass(frozen=True)
class VerifiedBundle:
    manifest: dict
    signature: dict
    manifest_sha256: str
    bundle_sha256: str
    bundle_size: int


def verify_bundle(path: str | Path, trust_keys: dict[str, bytes]) -> VerifiedBundle:
    path = Path(path)
    if not path.is_file():
        raise BundleError('Bundle not found')
    if path.stat().st_size > MAX_TOTAL_BYTES:
        raise BundleError('Bundle too large')
    try:
        with zipfile.ZipFile(path, 'r') as z:
            infos = z.infolist()
            if len(infos) < 3 or len(infos) > MAX_FILES + 2:
                raise BundleError('Unexpected ZIP entry count')
            names = set()
            for info in infos:
                if info.is_dir() or info.flag_bits & 1:
                    raise BundleError('ZIP directories/encryption forbidden')
                name = info.filename
                if name in ('manifest.json', 'signature.json'):
                    pass
                elif name.startswith('payload/'):
                    validate_relpath(name[len('payload/'):])
                else:
                    raise BundleError('Unexpected ZIP entry: ' + name)
                if name.casefold() in names:
                    raise BundleError('Duplicate/case-colliding ZIP name')
                names.add(name.casefold())
                mode = (info.external_attr >> 16) & 0xFFFF
                if mode and stat.S_IFMT(mode) not in (0, stat.S_IFREG):
                    raise BundleError('ZIP links/device/special files forbidden')
                if info.file_size > MAX_TOTAL_BYTES:
                    raise BundleError('Individual ZIP item too large')
                if info.file_size > 0 and info.file_size > max(1, info.compress_size) * MAX_COMPRESSION_RATIO:
                    raise BundleError('Compression ratio limit exceeded')
            if not {'manifest.json', 'signature.json'}.issubset(set(z.namelist())):
                raise BundleError('Manifest/signature missing')
            if z.getinfo('manifest.json').file_size > MAX_MANIFEST_BYTES or z.getinfo('signature.json').file_size > 8192:
                raise BundleError('Metadata too large')
            manifest = json.loads(z.read('manifest.json'))
            signature = json.loads(z.read('signature.json'))
            files = check_manifest(manifest)
            expected = {'manifest.json', 'signature.json', *(f'payload/{f["path"]}' for f in files)}
            if expected != set(z.namelist()):
                raise BundleError('Payload does not match manifest')
            if (not isinstance(signature, dict) or signature.get('algorithm') != 'Ed25519' or
                signature.get('key_id') != manifest['publisher_key_id']):
                raise BundleError('Invalid signature envelope')
            pub_raw = trust_keys.get(signature['key_id'])
            if not pub_raw or len(pub_raw) != 32:
                raise BundleError('Publisher key not trusted')
            try:
                sig = base64.b64decode(signature['signature_b64'], validate=True)
                Ed25519PublicKey.from_public_bytes(pub_raw).verify(sig, canonical_json(manifest))
            except (InvalidSignature, ValueError, KeyError, TypeError) as e:
                raise BundleError('Release signature verification failed') from e
            for entry in files:
                archive_path = 'payload/' + entry['path']
                info = z.getinfo(archive_path)
                if info.file_size != entry['size']:
                    raise BundleError('Declared file size mismatch')
                h = hashlib.sha256()
                read_size = 0
                with z.open(info) as stream:
                    while chunk := stream.read(1024 * 1024):
                        read_size += len(chunk)
                        if read_size > entry['size']:
                            raise BundleError('File expands past declared size')
                        h.update(chunk)
                if read_size != entry['size'] or h.hexdigest() != entry['sha256']:
                    raise BundleError('Payload checksum failed: ' + entry['path'])
            return VerifiedBundle(manifest, signature, hashlib.sha256(canonical_json(manifest)).hexdigest(),
                                  sha256_file(path), path.stat().st_size)
    except (zipfile.BadZipFile, zipfile.LargeZipFile, UnicodeDecodeError, json.JSONDecodeError) as e:
        raise BundleError('Malformed release bundle') from e


def _load_signer(path: str | Path) -> Ed25519PrivateKey:
    key = serialization.load_pem_private_key(Path(path).read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise BundleError('Expected Ed25519 private key')
    return key


def generate_publisher_key(private_path: str | Path, public_path: str | Path, key_id: str) -> None:
    """Development helper. Production keys should be managed by secure CI/HSM."""
    if not NAME_RE.fullmatch(key_id):
        raise BundleError('Invalid key identifier')
    private_path = Path(private_path)
    public_path = Path(public_path)
    if private_path.exists() or public_path.exists():
        raise BundleError('Refusing to overwrite an existing key/trust file')
    key = Ed25519PrivateKey.generate()
    private_path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(private_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(key.private_bytes(serialization.Encoding.PEM,
                                           serialization.PrivateFormat.PKCS8,
                                           serialization.NoEncryption()))
            output.flush(); os.fsync(output.fileno())
    except BaseException:
        private_path.unlink(missing_ok=True)
        raise
    public_path.write_text(json.dumps({key_id: base64.b64encode(
        key.public_key().public_bytes(serialization.Encoding.Raw,
                                      serialization.PublicFormat.Raw)).decode('ascii')}, indent=2) + '\n', encoding='utf-8')


def build_bundle(spec: dict, release_source: str | Path, private_key: str | Path,
                 output_path: str | Path) -> VerifiedBundle:
    """Spec components use `paths`, the signed manifest uses hashed `files`."""
    source = Path(release_source).resolve()
    manifest = {key: spec[key] for key in ('release_id', 'version', 'target_platform', 'publisher_key_id')}
    manifest['format'] = FORMAT
    manifest['required_capabilities'] = spec.get('required_capabilities', [])
    manifest['created_at'] = spec.get('created_at', datetime.now(timezone.utc).isoformat())
    manifest['components'] = []
    all_files = []
    for item in spec.get('components', []):
        component = {k: item[k] for k in ('id', 'kind', 'version')}
        component['files'] = []
        for rel in item.get('paths', []):
            validate_relpath(rel)
            file_path = source.joinpath(*rel.split('/'))
            if (file_path.is_symlink() or not file_path.is_file() or
                not file_path.resolve().is_relative_to(source)):
                raise BundleError('Source file missing or symlink: ' + rel)
            entry = {'path': rel, 'sha256': sha256_file(file_path), 'size': file_path.stat().st_size}
            component['files'].append(entry)
            all_files.append(entry)
        manifest['components'].append(component)
    check_manifest(manifest)
    key = _load_signer(private_key)
    sign = {'algorithm': 'Ed25519', 'key_id': manifest['publisher_key_id'],
            'signature_b64': base64.b64encode(key.sign(canonical_json(manifest))).decode('ascii')}
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists():
        raise BundleError('Refusing to overwrite a bundle')
    def make_info(name: str):
        entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
        entry.create_system = 3
        entry.external_attr = (stat.S_IFREG | 0o644) << 16
        entry.compress_type = zipfile.ZIP_DEFLATED
        return entry
    try:
        with zipfile.ZipFile(output_path, mode='x', allowZip64=True) as z:
            z.writestr(make_info('manifest.json'), canonical_json(manifest))
            z.writestr(make_info('signature.json'), canonical_json(sign))
            for item in sorted(all_files, key=lambda x: x['path']):
                with (source / item['path']).open('rb') as src, z.open(make_info('payload/' + item['path']), mode='w', force_zip64=True) as target:
                    while chunk := src.read(1024 * 1024):
                        target.write(chunk)
        pub = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        return verify_bundle(output_path, {manifest['publisher_key_id']: pub})
    except BaseException:
        output_path.unlink(missing_ok=True)
        raise
