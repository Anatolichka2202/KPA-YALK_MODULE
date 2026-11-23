"""Station DB administrative commands; read-only by default except enroll/profile caching."""
import argparse
import json
from pathlib import Path
from miltech_edge.station_store import StationStore


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--db',required=True,help='station.db path in persistent ProgramData')
    subs=p.add_subparsers(dest='command',required=True)
    enroll=subs.add_parser('enroll'); enroll.add_argument('--station-id',required=True)
    enroll.add_argument('--code',required=True);enroll.add_argument('--site',required=True)
    subs.add_parser('verify')
    backup=subs.add_parser('backup');backup.add_argument('--output',required=True)
    s=subs.add_parser('profile-cache');s.add_argument('--revision',type=int,required=True)
    s.add_argument('--sha256',required=True);s.add_argument('--json',required=True)
    args=p.parse_args()
    store=StationStore(args.db)
    try:
        if args.command=='enroll':
            store.initialize(args.station_id,args.code,args.site)
            print(json.dumps({'status':'enrolled'}))
        elif args.command=='verify':
            print(json.dumps(store.verify_integrity()))
        elif args.command=='backup':
            print(json.dumps(store.backup(args.output)))
        elif args.command=='profile-cache':
            print(json.dumps(store.cache_profile(json.loads(Path(args.json).read_text('utf-8')),
                                                 args.revision,args.sha256)))
    finally:store.close()

if __name__=='__main__':main()
