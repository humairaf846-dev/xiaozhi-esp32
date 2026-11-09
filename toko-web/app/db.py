from sqlalchemy import create_engine, inspect, text
from sqlalchemy.orm import DeclarativeBase, sessionmaker

from .config import DATABASE_URL

_connect_args = {"check_same_thread": False} if DATABASE_URL.startswith("sqlite") else {}
engine = create_engine(DATABASE_URL, connect_args=_connect_args, pool_pre_ping=True)
SessionLocal = sessionmaker(bind=engine, expire_on_commit=False)


class Base(DeclarativeBase):
    pass


# Columns added to a table that already shipped: create_all() only creates missing *tables*,
# so a column added later needs its own ALTER TABLE here (no migration framework in this project).
KOLOM_TAMBAHAN = {
    "pesanan": [("estimasi_hari", "VARCHAR(40) DEFAULT ''"), ("kode_promo", "VARCHAR(30) DEFAULT ''")],
    "pembayaran": [("bank", "VARCHAR(20) DEFAULT ''")],
    "aktivitas": [("pembeli", "VARCHAR(120) DEFAULT ''")],
}


def _migrasi() -> None:
    insp = inspect(engine)
    nama_tabel = set(insp.get_table_names())
    with engine.begin() as conn:
        for tabel, kolom in KOLOM_TAMBAHAN.items():
            if tabel not in nama_tabel:
                continue
            ada = {k["name"] for k in insp.get_columns(tabel)}
            for nama, tipe in kolom:
                if nama not in ada:
                    conn.execute(text(f"ALTER TABLE {tabel} ADD COLUMN {nama} {tipe}"))


def init_db():
    from . import models  # noqa: F401  (registers the tables)

    Base.metadata.create_all(engine)
    _migrasi()
