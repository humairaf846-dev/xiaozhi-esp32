"""WhatsApp notification to the shop owner through the Fonnte gateway (https://fonnte.com).

A website cannot send WhatsApp messages by itself, so this needs WA_NOTIF_TOKEN in .env.
Without a token, chats still work and appear in /admin/chat; only the notification is skipped.
"""

import json
import logging
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from . import config

log = logging.getLogger("toko")


def kirim_wa(pesan: str, tujuan: str = "") -> bool:
    """Send to the given number, or to the shop owner when no number is given."""
    if not config.WA_NOTIF_TOKEN:
        log.info("Notifikasi WA dilewati: WA_NOTIF_TOKEN belum diisi")
        return False
    data = urlencode({"target": tujuan or config.WA_NOTIF_TUJUAN, "message": pesan, "countryCode": "62"}).encode()
    req = Request(config.WA_NOTIF_URL, data=data, headers={"Authorization": config.WA_NOTIF_TOKEN})
    try:
        with urlopen(req, timeout=15) as r:
            hasil = json.loads(r.read().decode("utf-8", "replace"))
    except (OSError, ValueError) as e:
        log.warning("Notifikasi WA gagal: %s", e)
        return False
    if not hasil.get("status"):
        log.warning("Notifikasi WA ditolak gateway: %s", hasil.get("reason") or hasil.get("detail"))
        return False
    return True
