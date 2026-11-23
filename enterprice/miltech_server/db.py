import os
from sqlalchemy import create_engine, event
from sqlalchemy.engine import Engine
from sqlalchemy.orm import sessionmaker
from miltech_server.models import Base

SCHEMA_NAMES = ('infra','integration','evidence','delivery')

def build_engine(url: str) -> Engine:
    engine = create_engine(url, pool_pre_ping=True)
    if engine.dialect.name == 'sqlite':
        @event.listens_for(engine, 'connect')
        def foreign_keys(conn, rec):
            conn.execute('PRAGMA foreign_keys=ON')
        engine = engine.execution_options(schema_translate_map={name:None for name in SCHEMA_NAMES})
    return engine

def init_dev_db(engine:Engine) -> None:
    if engine.dialect.name != 'sqlite':
        raise RuntimeError('create_all allowed only for SQLite dev/tests: PostgreSQL requires reviewed migrations')
    Base.metadata.create_all(engine)

def create_session_factory(engine:Engine):
    return sessionmaker(bind=engine, autoflush=False, expire_on_commit=False)
