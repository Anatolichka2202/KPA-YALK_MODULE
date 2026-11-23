"""Fail-closed offline installer and two-phase release activation for Windows bootstrap.

Install root contains immutable release directories ONLY. Mutable delivery state
lives separately in ProgramData. The actual reboot/relaunch and quiescence check
belong to the external bootstrap/Core; we never kill a running test process.
"""
from __future__ import annotations

import base64
import hashlib
import json
import os
import shutil
import tempfile
import zipfile
from pathlib import Path
from uuid import uuid4

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

from miltech_delivery.package import (BundleError, canonical_json, check_manifest,
                                      sha256_file, verify_bundle, validate_relpath)
from miltech_edge.resources import FileLease


class InstallError(RuntimeError):
    pass


class ReleaseInstaller:
    def __init__(self, install_root: str | Path, data_root: str | Path,
                 trust_keys: dict[str, bytes], platform: str,
                 capabilities: set[str] | None = None):
        self.install_root = Path(install_root).resolve()
        self.data_root = Path(data_root).resolve()
        if self.install_root == self.data_root or self.install_root in self.data_root.parents or self.data_root in self.install_root.parents:
            raise InstallError('Installation and persistent data roots must not overlap')
        self.trust_keys = trust_keys
        self.platform = platform
        self.capabilities = capabilities or set()
        self.release_root = self.install_root / 'releases'
        self.state_path = self.data_root / 'delivery' / 'state.json'
        self.lock_dir = self.data_root / 'delivery' / 'locks'
        self.release_root.mkdir(parents=True, exist_ok=True)
        self.state_path.parent.mkdir(parents=True, exist_ok=True)

    def _locked(self):
        return FileLease(str(self.lock_dir), 'station-release-state')

    def _load_state(self) -> dict:
        if not self.state_path.exists():
            return {'format': 1, 'active': None, 'previous': None,
                    'pending': None, 'pending_boot_attempted': False}
        try:
            state = json.loads(self.state_path.read_text(encoding='utf-8'))
        except (ValueError, OSError) as e:
            raise InstallError('Corrupted delivery state: manual recovery required') from e
        if (state.get('format') != 1 or
            not all(k in state for k in ('active', 'previous', 'pending', 'pending_boot_attempted')) or
            not isinstance(state['pending_boot_attempted'], bool)):
            raise InstallError('Unsupported delivery state format')
        return state

    def _persist(self, state: dict) -> None:
        parent = self.state_path.parent
        fd, temp_path = tempfile.mkstemp(prefix='state-', suffix='.tmp', dir=parent)
        try:
            with os.fdopen(fd, 'wb') as f:
                f.write(canonical_json(state) + b'\n')
                f.flush(); os.fsync(f.fileno())
            os.replace(temp_path, self.state_path)
            if os.name != 'nt':
                dir_fd = os.open(parent, os.O_RDONLY)
                try: os.fsync(dir_fd)
                finally: os.close(dir_fd)
        finally:
            Path(temp_path).unlink(missing_ok=True)

    def _verify_installed(self, release_id: str) -> dict:
        from uuid import UUID
        try:
            if str(UUID(release_id)) != release_id:
                raise ValueError('noncanonical')
        except (ValueError, TypeError) as e:
            raise InstallError('Invalid installed release ID') from e
        release_dir = self.release_root / release_id
        try:
            manifest = json.loads((release_dir / '.manifest.json').read_text(encoding='utf-8'))
            signature = json.loads((release_dir / '.signature.json').read_text(encoding='utf-8'))
        except (OSError, ValueError) as e:
            raise InstallError('Missing or corrupt installed metadata') from e
        try:
            files = check_manifest(manifest)
            expected_names = {f['path'].casefold() for f in files} | {'.manifest.json', '.signature.json'}
            observed_names = set()
            for item in release_dir.rglob('*'):
                if item.is_symlink():
                    raise InstallError('Symlink inside installed release forbidden')
                if item.is_file():
                    observed_names.add(item.relative_to(release_dir).as_posix().casefold())
            if observed_names != expected_names:
                raise InstallError('Unexpected/missing file inside immutable release')
            if manifest['release_id'] != release_id or manifest['target_platform'] != self.platform:
                raise InstallError('Installed release identity/platform mismatch')
            if set(manifest.get('required_capabilities', [])) - self.capabilities:
                raise InstallError('Required station capabilities are not present')
            if signature.get('algorithm') != 'Ed25519' or signature.get('key_id') != manifest['publisher_key_id']:
                raise InstallError('Invalid installed signature envelope')
            key = self.trust_keys.get(signature['key_id'])
            if not key:
                raise InstallError('Publisher key no longer trusted')
            Ed25519PublicKey.from_public_bytes(key).verify(
                base64.b64decode(signature['signature_b64'], validate=True), canonical_json(manifest))
            for f in files:
                path = release_dir.joinpath(*validate_relpath(f['path']).split('/'))
                if path.is_symlink() or not path.is_file() or path.stat().st_size != f['size']:
                    raise InstallError('Installed release file missing/modified: ' + f['path'])
                if sha256_file(path) != f['sha256']:
                    raise InstallError('Installed release file hash mismatch: ' + f['path'])
            return manifest
        except (InvalidSignature, ValueError, KeyError, TypeError, BundleError) as e:
            raise InstallError('Installed release signature/data invalid') from e

    def stage(self, bundle_path: str | Path) -> dict:
        verified = verify_bundle(bundle_path, self.trust_keys)
        manifest = verified.manifest
        if manifest['target_platform'] != self.platform:
            raise InstallError('Release platform does not match this station')
        missing = set(manifest.get('required_capabilities', [])) - self.capabilities
        if missing:
            raise InstallError('Station capabilities missing: ' + ', '.join(sorted(missing)))
        release_id = manifest['release_id']
        with self._locked():
            final_path = self.release_root / release_id
            if final_path.exists():
                other = self._verify_installed(release_id)
                if canonical_json(other) != canonical_json(manifest):
                    raise InstallError('Release ID already used for different content')
                return {'release_id': release_id, 'status': 'already_staged',
                        'manifest_sha256': verified.manifest_sha256}
            staged = self.release_root / ('.staging-' + str(uuid4()))
            staged.mkdir(mode=0o700)
            try:
                with zipfile.ZipFile(bundle_path, 'r') as z:
                    for component in manifest['components']:
                        for entry in component['files']:
                            rel = validate_relpath(entry['path'])
                            dest = staged.joinpath(*rel.split('/'))
                            dest.parent.mkdir(parents=True, exist_ok=True)
                            h = hashlib.sha256()
                            count = 0
                            with z.open('payload/' + rel) as src, dest.open('xb') as target:
                                while chunk := src.read(1024 * 1024):
                                    count += len(chunk)
                                    if count > entry['size']:
                                        raise InstallError('File exceeds signed size')
                                    target.write(chunk)
                                    h.update(chunk)
                                target.flush(); os.fsync(target.fileno())
                            if count != entry['size'] or h.hexdigest() != entry['sha256']:
                                raise InstallError('Staged file hash differs from manifest')
                (staged / '.manifest.json').write_bytes(canonical_json(manifest))
                (staged / '.signature.json').write_bytes(canonical_json(verified.signature))
                os.replace(staged, final_path)
            finally:
                if staged.exists():
                    shutil.rmtree(staged)
            return {'release_id': release_id, 'status': 'staged',
                    'manifest_sha256': verified.manifest_sha256}

    def status(self) -> dict:
        with self._locked():
            return self._load_state()

    def request_activation(self, release_id: str, *, maintenance_confirmed: bool = False) -> dict:
        """A human flag is for pilot. Core-enforced quiescence required in production."""
        if not maintenance_confirmed:
            raise InstallError('Maintenance/quiescence confirmation required')
        with self._locked():
            self._verify_installed(release_id)
            state = self._load_state()
            if state['pending'] and state['pending'] != release_id:
                raise InstallError('A different release is awaiting boot confirmation')
            if state['active'] == release_id:
                return state
            state['pending'] = release_id
            state['pending_boot_attempted'] = False
            self._persist(state)
            return state

    def resolve_boot(self) -> dict:
        """Called by external StationBootstrap before starting a process.

        First boot tries pending; next attempt without confirm reverts to last good.
        The chosen directory is verified against publisher signature and hashes.
        """
        with self._locked():
            state = self._load_state()
            if state['pending'] and not state['pending_boot_attempted']:
                chosen = state['pending']
                self._verify_installed(chosen)
                state['pending_boot_attempted'] = True
                self._persist(state)
                return {'release_id': chosen, 'release_root': str(self.release_root / chosen), 'pending': True}
            if state['pending'] and state['pending_boot_attempted']:
                state['pending'] = None
                state['pending_boot_attempted'] = False
                self._persist(state)
            chosen = state['active']
            if not chosen:
                raise InstallError('No known-good active release; manual provisioning required')
            self._verify_installed(chosen)
            return {'release_id': chosen, 'release_root': str(self.release_root / chosen), 'pending': False}

    def confirm_boot(self, release_id: str) -> dict:
        """Only Core/bootstrap after readiness checks may call this in production."""
        with self._locked():
            state = self._load_state()
            if state['pending'] != release_id or not state['pending_boot_attempted']:
                raise InstallError('Release was not booted as the pending candidate')
            self._verify_installed(release_id)
            state['previous'] = state['active']
            state['active'] = release_id
            state['pending'] = None
            state['pending_boot_attempted'] = False
            self._persist(state)
            return state

    def request_rollback(self, *, maintenance_confirmed: bool = False) -> dict:
        if not maintenance_confirmed:
            raise InstallError('Maintenance/quiescence confirmation required')
        with self._locked():
            state = self._load_state()
            if state['pending']:
                state['pending'] = None
                state['pending_boot_attempted'] = False
            elif state['previous']:
                self._verify_installed(state['previous'])
                state['pending'] = state['previous']
                state['pending_boot_attempted'] = False
            else:
                raise InstallError('No pending/previous release available')
            self._persist(state)
            return state
