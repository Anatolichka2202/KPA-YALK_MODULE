import argparse,os
from miltech_server.db import build_engine,init_dev_db

def main():
    p=argparse.ArgumentParser()
    p.add_argument('operation',choices=['init-dev-db'])
    p.add_argument('--url',default=os.getenv('MILTECH_DATABASE_URL','sqlite:///./miltech-dev.sqlite3'))
    args=p.parse_args()
    engine=build_engine(args.url)
    init_dev_db(engine)
    print('Initialized SQLite development schema; this is NOT a production migration.')

if __name__=='__main__':main()
