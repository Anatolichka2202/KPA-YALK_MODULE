"""Reference CLI for signed release supply and autonomous station update pilot."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from miltech_delivery.edge_client import DeliveryClient
from miltech_delivery.installer import ReleaseInstaller
from miltech_delivery.reports import DeliveryReportOutbox
from miltech_delivery.package import (build_bundle, generate_publisher_key,
                                      read_trust_file, verify_bundle)


def output(value):
    print(json.dumps(value, indent=2, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    keygen = subs.add_parser('keygen', help='Generate a development signing key (not for production)')
    keygen.add_argument('--key-id', required=True)
    keygen.add_argument('--private', required=True)
    keygen.add_argument('--trust-file', required=True)

    build = subs.add_parser('build', help='Sign release directory as .mtbundle')
    build.add_argument('--spec', required=True)
    build.add_argument('--source', required=True)
    build.add_argument('--private', required=True)
    build.add_argument('--output', required=True)

    verify = subs.add_parser('verify', help='Verify bundle publisher signature and every payload hash')
    verify.add_argument('--bundle', required=True)
    verify.add_argument('--trust-file', required=True)

    commands = ('pull', 'stage', 'status', 'activate', 'resolve-boot', 'confirm-boot', 'rollback',
                'report-queue', 'reports-sync')
    for name in commands:
        sub = subs.add_parser(name)
        sub.add_argument('--install-root', required=True)
        sub.add_argument('--data-root', required=True)
        sub.add_argument('--trust-file', required=True)
        sub.add_argument('--platform', default='windows-x64')
        sub.add_argument('--capability', action='append', default=[])
        if name in ('pull', 'reports-sync'):
            sub.add_argument('--server', required=True)
        if name == 'stage':
            sub.add_argument('--bundle', required=True)
        if name in ('activate', 'confirm-boot', 'report-queue'):
            sub.add_argument('--release-id', required=True)
        if name == 'report-queue':
            sub.add_argument('--target-revision', required=True, type=int)
            sub.add_argument('--state', required=True, choices=sorted(DeliveryReportOutbox.STATES))
            sub.add_argument('--detail')
        if name in ('activate', 'rollback'):
            sub.add_argument('--maintenance-confirmed', action='store_true')
    args = parser.parse_args()
    try:
        if args.command == 'keygen':
            generate_publisher_key(args.private, args.trust_file, args.key_id)
            output({'created': True, 'private': args.private, 'trust_file': args.trust_file})
            return
        if args.command == 'build':
            spec = json.loads(Path(args.spec).read_text(encoding='utf-8'))
            b = build_bundle(spec, args.source, args.private, args.output)
            output({'release_id': b.manifest['release_id'], 'bundle_sha256': b.bundle_sha256,
                    'manifest_sha256': b.manifest_sha256, 'size': b.bundle_size})
            return
        if args.command == 'verify':
            b = verify_bundle(args.bundle, read_trust_file(args.trust_file))
            output({'valid': True, 'release_id': b.manifest['release_id'],
                    'version': b.manifest['version'], 'bundle_sha256': b.bundle_sha256})
            return
        installer = ReleaseInstaller(args.install_root, args.data_root,
                                     read_trust_file(args.trust_file), args.platform,
                                     set(args.capability))
        if args.command == 'pull':
            token = os.environ.get('MILTECH_STATION_TOKEN', '')
            pulled = DeliveryClient(args.server, token, installer.trust_keys,
                                    Path(args.data_root) / 'delivery' / 'cache').pull()
            if pulled.get('path'):
                pulled['stage'] = installer.stage(pulled['path'])
                outbox = DeliveryReportOutbox(Path(args.data_root) / 'delivery' / 'reports.sqlite3')
                try:
                    pulled['status_event'] = outbox.enqueue(pulled['release_id'],pulled['target_revision'],'STAGED')
                    pulled['report_delivery'] = outbox.sync(args.server, token)
                finally:
                    outbox.close()
            output(pulled)
        elif args.command == 'stage':
            output(installer.stage(args.bundle))
        elif args.command == 'status':
            output(installer.status())
        elif args.command == 'activate':
            output(installer.request_activation(args.release_id,
                                                maintenance_confirmed=args.maintenance_confirmed))
        elif args.command == 'resolve-boot':
            output(installer.resolve_boot())
        elif args.command == 'confirm-boot':
            output(installer.confirm_boot(args.release_id))
        elif args.command == 'rollback':
            output(installer.request_rollback(maintenance_confirmed=args.maintenance_confirmed))
        elif args.command == 'report-queue':
            outbox = DeliveryReportOutbox(Path(args.data_root) / 'delivery' / 'reports.sqlite3')
            try:
                output(outbox.enqueue(args.release_id, args.target_revision, args.state, args.detail))
            finally:outbox.close()
        elif args.command == 'reports-sync':
            outbox = DeliveryReportOutbox(Path(args.data_root) / 'delivery' / 'reports.sqlite3')
            try:
                output(outbox.sync(args.server, os.environ.get('MILTECH_STATION_TOKEN','')))
            finally:outbox.close()
    except (ValueError, RuntimeError, OSError) as exc:
        parser.exit(2, 'ERROR: ' + str(exc) + '\n')


if __name__ == '__main__':
    main()
