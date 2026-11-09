import os
from pathlib import Path

from dotenv import load_dotenv

BASE_DIR = Path(__file__).resolve().parent.parent
load_dotenv(BASE_DIR / ".env")

DATABASE_URL = os.getenv("DATABASE_URL", f"sqlite:///{BASE_DIR / 'toko.db'}")
NAMA_TOKO = os.getenv("NAMA_TOKO", "Yufasya Family")
WHATSAPP = os.getenv("WHATSAPP", "089527905336")
ALAMAT_TOKO = os.getenv("ALAMAT_TOKO", "Pasar Pariaman, depan stasiun kereta api")
JAM_BUKA = os.getenv("JAM_BUKA", "Setiap hari mulai jam 07.00")
REKENING_NAMA = os.getenv("REKENING_NAMA", "")


def _urai_rekening_banks() -> list[dict]:
    """REKENING_BANKS="BRI:123,BCA:456" (semua a.n. REKENING_NAMA), atau "BRI:123:Nama Lain" per bank."""
    hasil = []
    for bagian in os.getenv("REKENING_BANKS", "").split(","):
        potong = [p.strip() for p in bagian.split(":")]
        if len(potong) == 2 and all(potong):
            bank, nomor, nama = (*potong, REKENING_NAMA)
        elif len(potong) == 3 and all(potong):
            bank, nomor, nama = potong
        else:
            continue
        if bank and nomor and nama:
            hasil.append({"bank": bank, "nomor": nomor, "nama": nama})
    return hasil


REKENING_BANKS = _urai_rekening_banks()
ada_rekening = bool(REKENING_BANKS)
BUKTI_DIR = BASE_DIR / "data" / "bukti"
# What Google Maps searches for. Replace with the exact stall's Google Maps place name or "lat,lng" if needed.
MAPS_QUERY = os.getenv("MAPS_QUERY", "-0.62680,100.11710")  # Pasar Pariaman, depan Stasiun Pariaman
NAMA_PEMILIK = os.getenv("NAMA_PEMILIK", "Papa")  # contoh: "BRI 1234-5678-90 a.n. Nama Papa"

MIN_KG = int(os.getenv("MIN_KG", "1"))
DISKON_MIN_KG = int(os.getenv("DISKON_MIN_KG", "5"))
DISKON_PERSEN = int(os.getenv("DISKON_PERSEN", "5"))

ADMIN_PASSWORD = os.getenv("ADMIN_PASSWORD", "admin123")
SECRET_KEY = os.getenv("SECRET_KEY", "dev-secret-ganti-saat-online")
# Set 1 once the site is served over HTTPS, so the login cookie is never sent in the clear.
# Keep 0 for local http:// development, else the browser silently drops the cookie and login breaks.
COOKIE_SECURE = os.getenv("COOKIE_SECURE", "0") == "1"
PIN_LAPORAN = os.getenv("PIN_LAPORAN", "")
MCP_ENDPOINT = os.getenv("MCP_ENDPOINT", "")

# WhatsApp notification for new web chats, sent through the Fonnte gateway.
ROBOT_TOKEN = os.getenv("ROBOT_TOKEN", "")
WA_NOTIF_TOKEN = os.getenv("WA_NOTIF_TOKEN", "")
WA_NOTIF_TUJUAN = os.getenv("WA_NOTIF_TUJUAN", "") or WHATSAPP
WA_NOTIF_URL = os.getenv("WA_NOTIF_URL", "https://api.fonnte.com/send")

# What time of day the automatic WhatsApp sales recap goes out (24h "HH:MM").
JAM_LAPORAN = os.getenv("JAM_LAPORAN", "20:00")

# Google Maps Distance Matrix API key, for auto-computed delivery cost by distance. Needs a billing-enabled
# Google Cloud project - get one at https://console.cloud.google.com/google/maps-apis. Leave empty to keep
# computing ongkir by hand in /admin/pesanan (what this shop does today).
GOOGLE_MAPS_API_KEY = os.getenv("GOOGLE_MAPS_API_KEY", "")
# Rupiah per km, used together with the distance when GOOGLE_MAPS_API_KEY is set.
ONGKIR_PER_KM = int(os.getenv("ONGKIR_PER_KM", "2000"))


def aset(jalur: str) -> str:
    """URL of a static file with its modified time, so browsers fetch it again after it changes."""
    berkas = BASE_DIR / "app/static" / jalur
    versi = int(berkas.stat().st_mtime) if berkas.exists() else 0
    return f"/static/{jalur}?v={versi}"


def foto_produk(nama: str) -> str:
    """Photo for a product: app/static/img/produk/<name-with-dashes>.jpg, else the harvest photo."""
    berkas = "-".join(nama.lower().split()) + ".jpg"
    if (BASE_DIR / "app/static/img/produk" / berkas).exists():
        return aset(f"img/produk/{berkas}")
    return aset("img/panen.jpg")


def wa_link(pesan: str = "", nomor: str = "") -> str:
    """wa.me link for a WhatsApp number (default: the shop's), 08xx -> 628xx."""
    from urllib.parse import quote

    nomor = "".join(c for c in (nomor or WHATSAPP) if c.isdigit())
    if nomor.startswith("0"):
        nomor = "62" + nomor[1:]
    return f"https://wa.me/{nomor}" + (f"?text={quote(pesan)}" if pesan else "")


def maps_embed() -> str:
    from urllib.parse import quote

    return f"https://maps.google.com/maps?q={quote(MAPS_QUERY)}&z=17&output=embed"


def maps_link() -> str:
    from urllib.parse import quote

    return f"https://www.google.com/maps/search/?api=1&query={quote(MAPS_QUERY)}"
