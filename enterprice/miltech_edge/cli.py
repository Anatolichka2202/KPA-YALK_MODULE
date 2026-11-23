"""Local CLI. Provisioning is explicit; runtime is never privileged."""
import argparse,json,os,sys
from pathlib import Path
from miltech_edge.outbox import Outbox
from miltech_edge.preflight import load_profile,validate_environment

def main():
    p=argparse.ArgumentParser(description='MilTech autonomous station tools')
    sub=p.add_subparsers(dest='cmd',required=True)
    a=sub.add_parser('preflight');a.add_argument('--profile',required=True)
    a=sub.add_parser('init');a.add_argument('--db',required=True)
    a=sub.add_parser('enqueue');a.add_argument('--db',required=True);a.add_argument('--run-json',required=True)
    a=sub.add_parser('sync');a.add_argument('--db',required=True);a.add_argument('--url',required=True)
    a.add_argument('--token-env',default='MILTECH_STATION_TOKEN')
    args=p.parse_args()
    if args.cmd=='preflight':
        result=validate_environment(load_profile(args.profile));print(json.dumps(result,ensure_ascii=False,indent=2));
        return 0 if result['ready'] else 2
    outbox=Outbox(args.db)
    try:
        if args.cmd=='init':result={'database':args.db,'status':'initialized'}
        elif args.cmd=='enqueue':result=outbox.enqueue(json.loads(Path(args.run_json).read_text(encoding='utf-8')))
        elif args.cmd=='sync':
            token=os.getenv(args.token_env)
            if not token:
                p.error(f'{args.token_env} missing: no credentials stored in CLI arguments')
            result=outbox.sync(args.url,token)
        print(json.dumps(result,ensure_ascii=False,indent=2))
    finally:outbox.close()
    return 0

if __name__=='__main__':sys.exit(main())
