"""Connects the shop's MCP tools (mcp_server.py) to the robot.

The Xiaozhi Console gives each agent an "MCP endpoint" (a wss:// URL with a token). This
bridge opens that WebSocket, starts mcp_server.py, and passes JSON-RPC messages both ways:
robot -> xiaozhi server -> WebSocket -> mcp_server.py stdin, and stdout -> WebSocket back.
It reconnects automatically if the connection drops.

Started automatically by the web app when MCP_ENDPOINT is set, or run it alone:
    python mcp_bridge.py
"""

import asyncio
import logging
import os
import sys
import time
from pathlib import Path
from urllib.parse import urlsplit

from websockets.asyncio.client import connect

BASE_DIR = Path(__file__).resolve().parent
log = logging.getLogger("mcp_bridge")

# Connection state, read by the admin dashboard (the bridge runs inside the web process).
STATUS = {"terhubung": False, "sejak": None}


def _catat_koneksi(terhubung: bool, alasan: str = "") -> None:
    from datetime import datetime

    STATUS["terhubung"] = terhubung
    STATUS["sejak"] = datetime.now()
    try:
        from app import services
        from app.db import SessionLocal

        with SessionLocal() as db:
            if terhubung:
                services.catat(db, "robot", "koneksi", "Web tersambung ke Xiaozhi")
            else:
                log.warning("Sambungan ke Xiaozhi putus: %s", alasan)
                services.catat(db, "robot", "koneksi", "Web terputus dari Xiaozhi, menyambung ulang otomatis")
    except Exception:  # the log must never break the bridge
        log.exception("Gagal mencatat status koneksi")


def _tanpa_token(url: str) -> str:
    # Never print the token: it gives control over the robot's tools.
    p = urlsplit(url)
    return f"{p.scheme}://{p.netloc}{p.path}"


async def _satu_sesi(endpoint: str) -> None:
    async with connect(endpoint, ping_interval=10, ping_timeout=15, max_size=None) as ws:
        log.info("Tersambung ke robot: %s", _tanpa_token(endpoint))
        _catat_koneksi(True)
        proc = await asyncio.create_subprocess_exec(
            sys.executable, str(BASE_DIR / "mcp_server.py"),
            cwd=str(BASE_DIR),
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
        )

        async def ws_ke_server():
            async for pesan in ws:
                if isinstance(pesan, bytes):
                    pesan = pesan.decode("utf-8")
                proc.stdin.write(pesan.encode("utf-8") + b"\n")
                await proc.stdin.drain()

        async def server_ke_ws():
            while True:
                baris = await proc.stdout.readline()
                if not baris:
                    raise RuntimeError("mcp_server.py berhenti")
                await ws.send(baris.decode("utf-8").rstrip("\n"))

        tugas = [asyncio.create_task(ws_ke_server()), asyncio.create_task(server_ke_ws())]
        try:
            selesai, _ = await asyncio.wait(tugas, return_when=asyncio.FIRST_COMPLETED)
            for t in selesai:
                t.result()  # re-raise the error, if any
        finally:
            for t in tugas:
                t.cancel()
            if proc.returncode is None:
                proc.terminate()
                try:
                    await asyncio.wait_for(proc.wait(), 5)
                except asyncio.TimeoutError:
                    proc.kill()


async def jalankan_bridge(endpoint: str) -> None:
    jeda = 1
    while True:
        alasan = ""
        mulai = time.monotonic()
        try:
            await _satu_sesi(endpoint)
            jeda = 1
            log.warning("Koneksi ke robot terputus, menyambung ulang...")
        except asyncio.CancelledError:
            if STATUS["terhubung"]:
                _catat_koneksi(False, "web dimatikan")
            raise
        except Exception as e:  # network errors, server restarts, ...
            alasan = str(e)
            if time.monotonic() - mulai > 60:  # it was a healthy connection, so retry quickly
                jeda = 1
            log.warning("Gagal tersambung ke robot (%s). Coba lagi dalam %s detik.", e, jeda)
        if STATUS["terhubung"]:
            _catat_koneksi(False, alasan)
        await asyncio.sleep(jeda)
        jeda = min(jeda * 2, 60)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    sys.path.insert(0, str(BASE_DIR))
    from app import config

    url = os.getenv("MCP_ENDPOINT") or config.MCP_ENDPOINT
    if not url:
        sys.exit("MCP_ENDPOINT belum diisi di file .env")
    try:
        asyncio.run(jalankan_bridge(url))
    except KeyboardInterrupt:
        pass
