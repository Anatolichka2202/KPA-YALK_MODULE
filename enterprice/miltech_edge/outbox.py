"""Autonomous on-disk outbox: never deletes unacknowledged run events."""
import json
import hashlib
from urllib.parse import urlparse
import sqlite3
from pathlib import Path
from uuid import uuid4
import httpx
from miltech_server.contracts import RunPayload

DDL='''
CREATE TABLE IF NOT EXISTS outbox (
  sequence INTEGER PRIMARY KEY AUTOINCREMENT,
  event_id TEXT NOT NULL UNIQUE,
  event_type TEXT NOT NULL DEFAULT 'RUN_COMPLETED',
  payload TEXT NOT NULL,
  state TEXT NOT NULL DEFAULT 'PENDING',
  queued_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
  acknowledged_at TEXT,
  CHECK(state IN ('PENDING','ACKED'))
);
CREATE UNIQUE INDEX IF NOT EXISTS uq_run_outbox ON outbox(json_extract(payload,'$.source_run_id'));
'''

class Outbox:
    def __init__(self,path:str):
        self.path=Path(path)
        self.path.parent.mkdir(parents=True,exist_ok=True)
        self.conn=sqlite3.connect(str(self.path),timeout=30)
        self.conn.execute('PRAGMA journal_mode=WAL')
        self.conn.execute('PRAGMA synchronous=FULL')
        self.conn.executescript(DDL)
        # Additive upgrade from Slice 1 (user_version=0). Never rebuild the queue.
        actual={row[1] for row in self.conn.execute('PRAGMA table_info(outbox)')}
        needed={'sequence','event_id','event_type','payload','state','queued_at','acknowledged_at'}
        if not needed.issubset(actual):
            raise RuntimeError('outbox schema incompatible: migration requires manual audit')
        version=self.conn.execute('PRAGMA user_version').fetchone()[0]
        if version>1:
            raise RuntimeError('outbox schema newer than this agent')
        if version==0:
            self.conn.execute('PRAGMA user_version=1')

    def close(self):self.conn.close()

    def enqueue(self, payload:dict)->dict:
        parsed=RunPayload.model_validate(payload)
        data=json.dumps(parsed.model_dump(mode='json'),sort_keys=True,separators=(',',':'),ensure_ascii=False)
        old=self.conn.execute("SELECT sequence,event_id,payload FROM outbox WHERE json_extract(payload,'$.source_run_id')=?",(parsed.source_run_id,)).fetchone()
        if old:
            if old[2]!=data:raise ValueError('source_run_id conflict: existing event is immutable')
            return {'sequence':old[0],'event_id':old[1],'status':'existing'}
        event_id=str(uuid4())
        with self.conn:
            cur=self.conn.execute('INSERT INTO outbox(event_id,payload) VALUES (?,?)',(event_id,data))
        return {'sequence':cur.lastrowid,'event_id':event_id,'status':'queued'}

    def pending(self):
        rows=self.conn.execute("SELECT sequence,event_id,event_type,payload FROM outbox WHERE state='PENDING' ORDER BY sequence").fetchall()
        return [{'stream_sequence':seq,'event_id':eid,'event_type':typ,'payload':json.loads(data)} for seq,eid,typ,data in rows]

    def ack(self, sequence:int, event_id:str):
        with self.conn:
            self.conn.execute("UPDATE outbox SET state='ACKED',acknowledged_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE sequence=? AND event_id=?",(sequence,event_id))

    def sync(self, base_url:str,token:str,transport=None)->dict:
        target=urlparse(base_url)
        if target.scheme != 'https' and not (target.scheme=='http' and target.hostname in ('localhost','127.0.0.1','::1')):
            raise ValueError('Station API requires HTTPS except localhost development mode')
        sent=0
        with httpx.Client(base_url=base_url,timeout=15.0,transport=transport,trust_env=False) as client:
            for event in self.pending():
                try:
                    resp=client.post('/api/v1/sync/events',json=event,headers={'Authorization':'Bearer '+token})
                except (httpx.TransportError, httpx.TimeoutException) as exc:
                    return {'sent':sent,'pending':len(self.pending()),'stopped_by':'network_unavailable', 'details':type(exc).__name__}
                if resp.status_code != 200:
                    return {'sent':sent,'pending':len(self.pending()),'stopped_by':resp.status_code,'details':resp.text[:300]}
                response=resp.json()
                local_hash=hashlib.sha256(json.dumps(event['payload'],sort_keys=True,separators=(',',':'),ensure_ascii=False).encode('utf-8')).hexdigest()
                if (response.get('status') not in ('applied','duplicate')
                        or response.get('event_id')!=event['event_id']
                        or response.get('sequence')!=event['stream_sequence']
                        or response.get('payload_sha256')!=local_hash):
                    return {'sent':sent,'pending':len(self.pending()),'stopped_by':'untrusted acknowledgement'}
                self.ack(event['stream_sequence'],event['event_id'])
                sent+=1
        return {'sent':sent,'pending':len(self.pending())}
