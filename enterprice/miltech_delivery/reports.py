"""Persistent delivery-status outbox, separate from immutable RUN_COMPLETED stream.

Never block local station operation on a status reporting outage. Reports have
stable event IDs so retries after connection loss are idempotent on the server.
"""
from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from uuid import UUID, uuid4

import httpx

from miltech_delivery.edge_client import validate_server_url


class DeliveryReportOutbox:
    STATES = frozenset({'STAGED', 'PENDING_REBOOT', 'ACTIVE', 'FAILED', 'ROLLED_BACK'})

    def __init__(self, path: str | Path):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(path, timeout=10)
        self.db.row_factory = sqlite3.Row
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.execute('PRAGMA synchronous=FULL')
        self.db.execute('PRAGMA busy_timeout=10000')
        self.db.execute('''CREATE TABLE IF NOT EXISTS delivery_reports (
          ordinal INTEGER PRIMARY KEY AUTOINCREMENT,
          event_id TEXT NOT NULL UNIQUE,
          release_id TEXT NOT NULL,
          target_revision INTEGER NOT NULL CHECK(target_revision>0),
          state TEXT NOT NULL CHECK(state IN ('STAGED','PENDING_REBOOT','ACTIVE','FAILED','ROLLED_BACK')),
          detail TEXT,
          sync_state TEXT NOT NULL DEFAULT 'PENDING' CHECK(sync_state IN ('PENDING','ACKED')),
          queued_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
          acked_at TEXT
        ) STRICT''')
        self.db.commit()

    def close(self):
        self.db.close()

    def enqueue(self, release_id: str, target_revision: int, state: str, detail: str | None = None,
                event_id: str | None = None) -> dict:
        release_id = str(UUID(release_id))
        if not isinstance(target_revision, int) or isinstance(target_revision, bool) or target_revision < 1:
            raise ValueError('Invalid target_revision')
        if state not in self.STATES:
            raise ValueError('Invalid delivery state')
        if detail is not None and len(detail) > 500:
            raise ValueError('Detail exceeds 500 characters')
        event_id = str(UUID(event_id)) if event_id else str(uuid4())
        old = self.db.execute('SELECT * FROM delivery_reports WHERE event_id=?',(event_id,)).fetchone()
        if old:
            if (old['release_id'],old['target_revision'],old['state'],old['detail']) != (
                release_id,target_revision,state,detail):
                raise ValueError('Delivery report event ID collision')
            return {'event_id': event_id, 'status': 'existing'}
        with self.db:
            self.db.execute('''INSERT INTO delivery_reports
              (event_id,release_id,target_revision,state,detail) VALUES (?,?,?,?,?)''',
              (event_id,release_id,target_revision,state,detail))
        return {'event_id':event_id,'status':'queued'}

    def pending(self) -> list[dict]:
        result = self.db.execute('SELECT * FROM delivery_reports WHERE sync_state=? ORDER BY ordinal',('PENDING',))
        return [{k: row[k] for k in ('event_id','release_id','target_revision','state','detail')} for row in result]

    def sync(self, server_url: str, station_token: str,
             transport: httpx.BaseTransport | None = None) -> dict:
        server_url = validate_server_url(server_url)
        if not station_token:
            raise ValueError('Station token required')
        sent = 0
        with httpx.Client(transport=transport, timeout=20, follow_redirects=False) as client:
            for row in self.pending():
                try:
                    resp = client.post(server_url + '/api/v1/stations/me/delivery/reports',
                                       headers={'Authorization':'Bearer '+station_token}, json=row)
                    if resp.status_code != 200 or resp.json().get('status') not in ('recorded','duplicate'):
                        return {'sent':sent,'remaining':len(self.pending()),'stopped_by':f'http_{resp.status_code}'}
                    with self.db:
                        self.db.execute('''UPDATE delivery_reports SET sync_state='ACKED',acked_at=CURRENT_TIMESTAMP
                                           WHERE event_id=? AND sync_state='PENDING' ''',(row['event_id'],))
                    sent += 1
                except (httpx.HTTPError, ValueError, json.JSONDecodeError):
                    return {'sent':sent,'remaining':len(self.pending()),'stopped_by':'network_unavailable'}
        return {'sent':sent,'remaining':len(self.pending()),'stopped_by':None}
