"""Shop logic shared by the website and the robot (MCP tools)."""

import re
import secrets
from datetime import date, datetime, time, timedelta

from sqlalchemy import func, select
from sqlalchemy.orm import Session

from . import config, notif
from .models import (
    Aktivitas, Faq, ItemPesanan, KodePromo, Percakapan, PesanChat, Pembayaran, Pesanan, Produk, RiwayatPesanan, Ulasan,
)

# Order flow: new -> waiting for transfer (after admin sets the shipping cost) -> paid/processing
# -> shipped -> done. Cancelling returns the stock.
STATUS_PESANAN = ["baru", "menunggu_bayar", "diproses", "dikirim", "selesai", "batal"]
LABEL_STATUS = {
    "baru": "Baru (cek & hitung ongkir)",
    "menunggu_bayar": "Menunggu pembayaran",
    "diproses": "Sudah bayar, diproses",
    "dikirim": "Dikirim / siap diambil",
    "selesai": "Selesai",
    "batal": "Batal",
}


def rupiah(n: int | None) -> str:
    return "Rp " + f"{int(n or 0):,}".replace(",", ".")


# ---------------------------------------------------------------- activity log


def catat(db: Session, sumber: str, jenis: str, ringkasan: str, detail: str = "",
          pesanan_id: int | None = None, pembeli: str = "", commit: bool = True) -> None:
    db.add(Aktivitas(sumber=sumber, jenis=jenis, ringkasan=ringkasan[:255], detail=detail,
                     pesanan_id=pesanan_id, pembeli=pembeli[:120]))
    if commit:
        db.commit()


LABEL_JENIS = {
    "tanya_harga": "Tanya harga/stok", "tanya_info": "Tanya info", "pesanan": "Pesanan baru",
    "gagal_pesan": "Pesanan belum lengkap", "laporan": "Laporan", "status": "Status pesanan",
    "produk": "Produk", "stok": "Stok", "koneksi": "Koneksi robot",
    "pembayaran": "Pembayaran", "tanya_robot": "Ucapan ke robot", "jawab_robot": "Jawaban robot", "chat": "Chat pembeli",
    "tanya_tak_terjawab": "Pertanyaan belum terjawab", "ulasan": "Ulasan pembeli",
}


def aktivitas_setelah(db: Session, setelah_id: int, batas: int = 50) -> list[Aktivitas]:
    q = select(Aktivitas).where(Aktivitas.id > setelah_id, Aktivitas.jenis != "koneksi") \
        .order_by(Aktivitas.id.desc()).limit(batas)
    return list(db.scalars(q))


def aktivitas_robot_terakhir(db: Session) -> datetime | None:
    return db.scalar(select(func.max(Aktivitas.waktu)).where(Aktivitas.jenis == "tanya_robot"))


# The robot reports its state every 20 seconds; missing two reports in a row means it is gone.
ROBOT_BATAS_DIAM = timedelta(seconds=50)
ROBOT = {"status": "", "terakhir": None}


def robot_lapor(db: Session, status: str, dengar: str, jawab: str) -> None:
    """Store a report sent by the robot itself (see main/toko_reporter.cc in the firmware)."""
    ROBOT["status"] = status[:30]
    ROBOT["terakhir"] = datetime.now()
    dengar, jawab = dengar.strip(), jawab.strip()
    if dengar:
        catat(db, "robot", "tanya_robot", f"Robot mendengar: {dengar}")
    if jawab:
        jawab = re.sub(r"([.!?])(?=[^\s\d])", r"\1 ", jawab)  # the robot sends sentences without spaces
        catat(db, "robot", "jawab_robot", f"Robot menjawab: {jawab}")


def aktivitas_terbaru(db: Session, sumber: str = "", batas: int = 100) -> list[Aktivitas]:
    q = select(Aktivitas).where(Aktivitas.jenis != "koneksi") \
        .order_by(Aktivitas.waktu.desc(), Aktivitas.id.desc()).limit(batas)
    if sumber:
        q = q.where(Aktivitas.sumber == sumber)
    return list(db.scalars(q))


def aktivitas_per_pembeli(db: Session, batas_baris: int = 150, maks_pembeli: int = 15) -> list[dict]:
    """Recent activity (chat, orders, payments) grouped by buyer, most recently active first.

    So the admin can scan one customer at a time ("pembeli 1, pembeli 2, ...") instead of a single
    feed where everyone's chat and orders are interleaved - the thing that made it hard to tell
    "jeruk ini punya siapa" at a glance.
    """
    baris = db.scalars(select(Aktivitas).where(Aktivitas.pembeli != "")
                       .order_by(Aktivitas.waktu.desc(), Aktivitas.id.desc()).limit(batas_baris))
    kelompok: dict[str, list[Aktivitas]] = {}
    for a in baris:
        kelompok.setdefault(a.pembeli, []).append(a)
    hasil = []
    for nama, item in list(kelompok.items())[:maks_pembeli]:
        hasil.append({"nama": nama, "terakhir": item[0].waktu, "aktivitas": item})
    return hasil


# ---------------------------------------------------------------- products


def daftar_produk(db: Session, semua: bool = False) -> list[Produk]:
    q = select(Produk)
    if not semua:
        q = q.where(Produk.aktif.is_(True))
    return list(db.scalars(q.order_by(Produk.jenis, Produk.harga.desc())))


def cari_produk(db: Session, kata: str = "") -> list[Produk]:
    produk = daftar_produk(db)
    kata = (kata or "").strip().lower()
    if not kata:
        return produk
    # Every word must appear in the name ("manis besar" -> Jeruk Manis Besar).
    kata_list = [k for k in re.findall(r"\w+", kata) if k not in ("jeruk", "yang", "harga", "berapa")]
    if not kata_list:
        return produk
    return [p for p in produk if all(k in p.nama.lower() for k in kata_list)]


def cocokkan_produk(db: Session, nama: str) -> list[Produk]:
    """Products matching a spoken/typed name such as 'jeruk manis besar' or 'peras sedang'."""
    nama = (nama or "").strip().lower()
    persis = [p for p in daftar_produk(db) if p.nama.lower() == nama]
    return persis or cari_produk(db, nama)


# ---------------------------------------------------------------- FAQ


def _kata(teks: str) -> set[str]:
    return {k for k in re.findall(r"\w+", (teks or "").lower()) if len(k) > 2}


def cari_faq(db: Session, pertanyaan: str, batas: int = 3) -> list[Faq]:
    """Match the customer's question against each FAQ's question and answer, no manual keywords needed."""
    kata = _kata(pertanyaan) - {"kak", "kakak", "abang", "bang", "jeruk", "nggak", "ngga", "gak", "apa", "aja", "ya", "dong"}
    hasil = []
    for f in db.scalars(select(Faq).where(Faq.aktif.is_(True))):
        skor = (len(kata & _kata(f.kata_kunci)) * 2 + len(kata & _kata(f.pertanyaan)) * 2
                + len(kata & _kata(f.jawaban)))
        if skor:
            hasil.append((skor, f))
    hasil.sort(key=lambda x: -x[0])
    return [f for _, f in hasil[:batas]]


# ---------------------------------------------------------------- orders


class PesananError(ValueError):
    pass


def hitung_diskon(subtotal: int, total_kg: int) -> int:
    if total_kg >= config.DISKON_MIN_KG:
        return round(subtotal * config.DISKON_PERSEN / 100)
    return 0


# Starting estimate shown to the buyer right away; admin can refine it per order once ongkir is known.
ESTIMASI_DEFAULT = {"ambil": "Siap diambil hari ini", "antar": "1-2 hari setelah pembayaran dikonfirmasi"}

DISKON_REFERRAL = 5000  # rupiah off for whoever enters a friend's "REF<id>" code at checkout


def cek_kode_promo(db: Session, kode: str, subtotal_setelah_diskon: int, telepon_pemesan: str) -> tuple[int, str]:
    """Validate a promo or referral code typed in the cart. Returns (extra discount in rupiah, a short
    note for the activity log) - (0, "") when no code was given. Raises PesananError when it doesn't apply."""
    kode = (kode or "").strip().upper()
    if not kode:
        return 0, ""
    promo = db.scalar(select(KodePromo).where(func.upper(KodePromo.kode) == kode, KodePromo.aktif.is_(True)))
    if promo is not None:
        if promo.kadaluarsa and promo.kadaluarsa < date.today():
            raise PesananError("Kode promo sudah kedaluwarsa.")
        if not promo.tak_terbatas and promo.dipakai >= promo.kuota:
            raise PesananError("Kode promo sudah habis dipakai.")
        diskon = promo.nilai if promo.jenis == "nominal" else round(subtotal_setelah_diskon * promo.nilai / 100)
        promo.dipakai += 1
        return min(diskon, subtotal_setelah_diskon), f"kode promo {promo.kode}"
    if kode.startswith("REF") and kode[3:].isdigit():
        asal = db.get(Pesanan, int(kode[3:]))
        digit_baru = "".join(c for c in telepon_pemesan if c.isdigit())
        if asal is None or asal.status == "batal":
            raise PesananError("Kode referral tidak ditemukan.")
        if digit_baru and "".join(c for c in asal.telepon if c.isdigit()) == digit_baru:
            raise PesananError("Kode referral tidak bisa dipakai untuk diri sendiri.")
        return min(DISKON_REFERRAL, subtotal_setelah_diskon), f"referral dari pesanan #{asal.id}"
    raise PesananError("Kode promo tidak dikenal.")


AMBANG_STOK_MENIPIS = 10
_STOK_DIALERT: set[int] = set()


def cek_alert_stok(produk: Produk) -> None:
    """WhatsApp the owner once when a product's stock first drops to/under the low-stock threshold;
    allowed to alert again next time, reset as soon as it is restocked above the threshold."""
    if produk.stok <= AMBANG_STOK_MENIPIS:
        if produk.id not in _STOK_DIALERT:
            _STOK_DIALERT.add(produk.id)
            notif.kirim_wa(f"Stok {produk.nama} tinggal {produk.stok} kg, segera tambah stok ya.")
    else:
        _STOK_DIALERT.discard(produk.id)


def buat_pesanan(
    db: Session,
    nama_pelanggan: str,
    item: list[tuple[int, int]],
    telepon: str = "",
    alamat: str = "",
    pengiriman: str = "antar",
    keperluan: str = "biasa",
    tanggal_butuh: date | None = None,
    catatan: str = "",
    sumber: str = "web",
    kode_promo: str = "",
) -> Pesanan:
    """Create an order and reduce stock. item = [(produk_id, kg), ...]."""
    nama_pelanggan = (nama_pelanggan or "").strip()
    if not nama_pelanggan:
        raise PesananError("Nama pelanggan wajib diisi.")
    telepon = (telepon or "").strip()
    if not telepon:
        raise PesananError("Nomor WhatsApp wajib diisi supaya kami bisa konfirmasi ongkir dan pembayaran.")
    pengiriman = pengiriman if pengiriman in ("antar", "ambil") else "antar"
    if pengiriman == "antar" and not (alamat or "").strip():
        raise PesananError("Alamat pengiriman wajib diisi (pengiriman hanya dalam kota).")
    keperluan = keperluan if keperluan in ("biasa", "acara", "oleh-oleh") else "biasa"

    gabung: dict[int, int] = {}
    for produk_id, jumlah in item:
        if jumlah is None or int(jumlah) <= 0:
            continue
        gabung[int(produk_id)] = gabung.get(int(produk_id), 0) + int(jumlah)
    if not gabung:
        raise PesananError("Keranjang masih kosong.")

    pesanan = Pesanan(
        nama_pelanggan=nama_pelanggan[:120], telepon=telepon[:30], alamat=(alamat or "").strip(),
        pengiriman=pengiriman, keperluan=keperluan, tanggal_butuh=tanggal_butuh,
        catatan=(catatan or "").strip(), sumber=sumber, status="baru",
        estimasi_hari=ESTIMASI_DEFAULT.get(pengiriman, ""),
    )
    subtotal = 0
    total_kg = 0
    for produk_id, jumlah in gabung.items():
        produk = db.scalars(select(Produk).where(Produk.id == produk_id).with_for_update()).first()
        if produk is None or not produk.aktif:
            raise PesananError(f"Produk dengan id {produk_id} tidak tersedia.")
        if jumlah < config.MIN_KG:
            raise PesananError(f"Minimal pembelian {config.MIN_KG} kg per jenis jeruk.")
        if produk.stok < jumlah:
            raise PesananError(f"Stok {produk.nama} tinggal {produk.stok} kg, tidak cukup untuk {jumlah} kg.")
        produk.stok -= jumlah
        cek_alert_stok(produk)
        pesanan.item.append(ItemPesanan(produk_id=produk.id, nama_produk=produk.nama,
                                        harga=produk.harga, jumlah=jumlah))
        subtotal += produk.harga * jumlah
        total_kg += jumlah
    pesanan.subtotal = subtotal
    pesanan.total_kg = total_kg
    pesanan.diskon = hitung_diskon(subtotal, total_kg)
    catatan_promo = ""
    tambahan, catatan_promo = cek_kode_promo(db, kode_promo, subtotal - pesanan.diskon, telepon)
    if tambahan:
        pesanan.diskon += tambahan
        pesanan.kode_promo = kode_promo.strip().upper()[:30]
    pesanan.pembayaran = Pembayaran(kode=secrets.token_urlsafe(16))
    pesanan.riwayat.append(RiwayatPesanan(status=pesanan.status))
    db.add(pesanan)
    db.flush()
    isi = ", ".join(f"{it.jumlah} kg {it.nama_produk}" for it in pesanan.item)
    detail = f"Total {rupiah(pesanan.subtotal - pesanan.diskon)} (belum ongkir), {pengiriman}, {keperluan}"
    if catatan_promo:
        detail += f", {catatan_promo}"
    catat(db, sumber, "pesanan", f"Pesanan #{pesanan.id} dari {pesanan.nama_pelanggan}: {isi}",
          detail, pesanan_id=pesanan.id, pembeli=pesanan.nama_pelanggan, commit=False)
    db.commit()
    return pesanan


def hitung_ongkir_jarak(alamat_tujuan: str) -> int | None:
    """Distance-based shipping cost via the Google Maps Distance Matrix API. Needs GOOGLE_MAPS_API_KEY
    in .env; returns None (caller falls back to typing ongkir by hand) when that's not set or the
    lookup fails for any reason."""
    if not config.GOOGLE_MAPS_API_KEY or not alamat_tujuan.strip():
        return None
    import json
    from urllib.parse import quote
    from urllib.request import urlopen

    url = ("https://maps.googleapis.com/maps/api/distancematrix/json"
           f"?origins={quote(config.MAPS_QUERY)}&destinations={quote(alamat_tujuan)}"
           f"&key={config.GOOGLE_MAPS_API_KEY}")
    try:
        with urlopen(url, timeout=10) as r:
            data = json.loads(r.read().decode("utf-8", "replace"))
        elemen = data["rows"][0]["elements"][0]
        if elemen["status"] != "OK":
            return None
        meter = elemen["distance"]["value"]
        return round((meter / 1000) * config.ONGKIR_PER_KM)
    except Exception:
        return None


def ubah_pesanan(db: Session, pesanan_id: int, status_baru: str, ongkir: int | None = None,
                 estimasi_hari: str | None = None) -> Pesanan:
    if status_baru not in STATUS_PESANAN:
        raise PesananError("Status tidak dikenal.")
    pesanan = db.get(Pesanan, pesanan_id)
    if pesanan is None:
        raise PesananError("Pesanan tidak ditemukan.")
    if ongkir is not None:
        pesanan.ongkir = max(0, ongkir)
    if estimasi_hari is not None:
        pesanan.estimasi_hari = estimasi_hari.strip()[:40]
    lama = pesanan.status
    if lama != status_baru:
        pesanan.riwayat.append(RiwayatPesanan(status=status_baru))
        if status_baru == "batal":
            for it in pesanan.item:
                produk = db.get(Produk, it.produk_id)
                if produk is not None:
                    produk.stok += it.jumlah
        elif lama == "batal":
            for it in pesanan.item:
                produk = db.get(Produk, it.produk_id)
                if produk is None or produk.stok < it.jumlah:
                    db.rollback()
                    raise PesananError(f"Stok {it.nama_produk} tidak cukup untuk mengaktifkan lagi pesanan ini.")
                produk.stok -= it.jumlah
        pesanan.status = status_baru
        catat(db, "admin", "status", f"Pesanan #{pesanan.id}: {LABEL_STATUS[lama]} → {LABEL_STATUS[status_baru]}",
              pesanan_id=pesanan.id, pembeli=pesanan.nama_pelanggan, commit=False)
    db.commit()
    return pesanan


def pesan_status(pesanan: Pesanan, base_url: str = "") -> str:
    """Buyer-friendly WhatsApp update sent whenever the admin changes an order's status by hand
    (status accepted via a payment decision already has its own message, see pesan_keputusan)."""
    status = pesanan.status
    if status == "menunggu_bayar":
        link = f" Yuk bayar di sini: {base_url}bayar/{pesanan.pembayaran.kode}" if base_url and pesanan.pembayaran else ""
        return (f"Ongkir pesanan #{pesanan.id} sudah kami hitung: {rupiah(pesanan.ongkir)}. "
                f"Total jadi {rupiah(pesanan.total)}.{link} - {config.NAMA_TOKO}")
    if status == "diproses":
        return f"Pesanan #{pesanan.id} sudah dikemas, segera kami siapkan. - {config.NAMA_TOKO}"
    if status == "dikirim":
        if pesanan.pengiriman == "ambil":
            return f"Pesanan #{pesanan.id} sudah siap diambil di toko ya. Ditunggu kedatangannya! - {config.NAMA_TOKO}"
        return f"Pesanan #{pesanan.id} sedang dalam perjalanan ke alamat kakak-kakak atau abang-abang. - {config.NAMA_TOKO}"
    if status == "selesai":
        return f"Pesanan #{pesanan.id} sudah selesai. Terima kasih sudah belanja di {config.NAMA_TOKO}!"
    if status == "batal":
        return f"Pesanan #{pesanan.id} kami batalkan. Hubungi kami lewat chat kalau ada pertanyaan ya. - {config.NAMA_TOKO}"
    return f"Status pesanan #{pesanan.id} diperbarui: {LABEL_STATUS.get(status, status)}. - {config.NAMA_TOKO}"


def pesan_whatsapp(pesanan: Pesanan) -> str:
    """Pre-filled WhatsApp message the customer can send to confirm the order."""
    baris = [f"Halo {config.NAMA_TOKO}, saya mau konfirmasi pesanan #{pesanan.id}:"]
    baris += [f"- {it.jumlah} kg {it.nama_produk}" for it in pesanan.item]
    baris.append(f"Total belanja: {rupiah(pesanan.subtotal - pesanan.diskon)} (belum ongkir)")
    baris.append(f"Nama: {pesanan.nama_pelanggan}")
    baris.append("Ambil di toko" if pesanan.pengiriman == "ambil" else f"Alamat: {pesanan.alamat}")
    return "\n".join(baris)


# ---------------------------------------------------------------- promo codes


def daftar_promo(db: Session) -> list[KodePromo]:
    return list(db.scalars(select(KodePromo).order_by(KodePromo.dibuat.desc())))


def simpan_promo(db: Session, promo_id: int | None, kode: str, jenis: str, nilai: int, kuota: int,
                 tak_terbatas: bool, kadaluarsa: date | None, aktif: bool) -> KodePromo:
    kode = kode.strip().upper()
    if not kode:
        raise PesananError("Kode promo wajib diisi.")
    if kode.startswith("REF"):
        raise PesananError('Kode tidak boleh diawali "REF" (dipakai khusus untuk kode referral pelanggan).')
    p = db.get(KodePromo, promo_id) if promo_id else KodePromo()
    if p is None:
        raise PesananError("Kode promo tidak ditemukan.")
    sudah = db.scalar(select(KodePromo).where(func.upper(KodePromo.kode) == kode, KodePromo.id != (promo_id or 0)))
    if sudah is not None:
        raise PesananError("Kode promo ini sudah dipakai, pilih kode lain.")
    p.kode, p.jenis, p.nilai = kode, ("persen" if jenis == "persen" else "nominal"), max(0, nilai)
    p.kuota, p.tak_terbatas, p.kadaluarsa, p.aktif = max(0, kuota), tak_terbatas, kadaluarsa, aktif
    db.add(p)
    db.commit()
    return p


# ---------------------------------------------------------------- payments

LABEL_BAYAR = {"belum": "Belum bayar", "menunggu": "Bukti masuk, perlu dicek", "lunas": "Lunas", "ditolak": "Bukti ditolak"}
LABEL_METODE = {"qris": "QRIS", "transfer": "Transfer bank", "cod": "Bayar di tempat (COD)"}


def pesan_keputusan(bayar: Pembayaran, terima: bool) -> str:
    """The reply a buyer gets, in the web chat and on WhatsApp, after the admin checks a payment."""
    pesanan = bayar.pesanan
    if terima:
        return (f"Pembayaran pesanan #{pesanan.id} sebesar {rupiah(bayar.jumlah)} sudah kami terima. "
                f"Terima kasih, jeruknya segera kami siapkan. - {config.NAMA_TOKO}")
    return (f"Pembayaran pesanan #{pesanan.id} belum bisa kami terima. Silakan cek lagi dan kirim ulang "
            f"bukti bayarnya, atau balas chat ini kalau ada kendala. - {config.NAMA_TOKO}")


def simpan_bukti(db: Session, bayar: Pembayaran, metode: str, berkas: str, percakapan: Percakapan,
                 bank: str = "") -> None:
    """Record the buyer's proof. The order only moves on after the admin accepts it."""
    pesanan = bayar.pesanan
    bayar.percakapan_id = percakapan.id
    chat_balas(db, percakapan, f"Bukti bayar pesanan #{pesanan.id} sudah masuk. Kami cek dulu ya, nanti kami kabari di sini.")
    bayar.metode, bayar.bukti, bayar.status = metode, berkas, "menunggu"
    bayar.bank = bank if metode == "transfer" else ""
    bayar.jumlah, bayar.dikirim = pesanan.total, datetime.now()
    cara = f"{LABEL_METODE[metode]} {bank}" if bayar.bank else LABEL_METODE[metode]
    catat(db, "web", "pembayaran",
          f"Bukti bayar pesanan #{pesanan.id} dari {pesanan.nama_pelanggan}: "
          f"{rupiah(pesanan.total)} via {cara}", pesanan_id=pesanan.id, pembeli=pesanan.nama_pelanggan)


def konfirmasi_cod(db: Session, bayar: Pembayaran, percakapan: Percakapan) -> None:
    """Pickup orders only: the buyer commits to pay cash in person, no proof photo needed. The admin
    still has to mark the payment accepted once the cash is actually in hand, same as any other method."""
    pesanan = bayar.pesanan
    bayar.percakapan_id = percakapan.id
    chat_balas(db, percakapan, f"Pesanan #{pesanan.id} dicatat bayar di tempat (COD). Siapkan uang pas {rupiah(pesanan.total)} ya saat ambil.")
    bayar.metode, bayar.bukti, bayar.bank, bayar.status = "cod", "", "", "menunggu"
    bayar.jumlah, bayar.dikirim = pesanan.total, datetime.now()
    catat(db, "web", "pembayaran",
          f"Pesanan #{pesanan.id} dari {pesanan.nama_pelanggan} pilih bayar di tempat (COD): {rupiah(pesanan.total)}",
          pesanan_id=pesanan.id, pembeli=pesanan.nama_pelanggan)


def putuskan_bayar(db: Session, bayar: Pembayaran, terima: bool) -> None:
    if bayar.status != "menunggu":
        raise PesananError("Pembayaran ini tidak sedang menunggu dicek.")
    bayar.status, bayar.diputus = ("lunas" if terima else "ditolak"), datetime.now()
    if terima:
        ubah_pesanan(db, bayar.pesanan_id, "diproses")
    percakapan = db.get(Percakapan, bayar.percakapan_id) if bayar.percakapan_id else None
    if percakapan is not None:
        chat_balas(db, percakapan, pesan_keputusan(bayar, terima))
    catat(db, "admin", "pembayaran", f"Pembayaran pesanan #{bayar.pesanan_id} {'diterima' if terima else 'ditolak'}",
          pesanan_id=bayar.pesanan_id, pembeli=bayar.pesanan.nama_pelanggan)


def riwayat_pelanggan(db: Session, telepon: str) -> dict | None:
    """What the robot can say to recognize a returning customer, once they say their own WhatsApp
    number out loud - the robot has no voice-ID, so it can only "remember" someone this way."""
    digit = "".join(c for c in (telepon or "") if c.isdigit())
    if not digit:
        return None
    semua = list(db.scalars(select(Pesanan).where(Pesanan.status != "batal").order_by(Pesanan.dibuat.desc())))
    punya = [p for p in semua if "".join(c for c in p.telepon if c.isdigit()) == digit]
    if not punya:
        return None
    terakhir = punya[0]
    isi = ", ".join(f"{it.jumlah} kg {it.nama_produk}" for it in terakhir.item)
    return {
        "nama": terakhir.nama_pelanggan,
        "jumlah_pesanan": len(punya),
        "pesanan_terakhir": isi,
        "tanggal_terakhir": terakhir.dibuat.date(),
    }


# ---------------------------------------------------------------- buyer's order page

# Tabs on "Pesanan Saya", in the order the buyer sees them.
TAB_PESANAN = {"belum_bayar": "Belum bayar", "dikemas": "Dikemas", "dikirim": "Dikirim", "selesai": "Selesai", "batal": "Dibatalkan"}
_TAB_DARI_STATUS = {"baru": "belum_bayar", "menunggu_bayar": "belum_bayar", "diproses": "dikemas",
                    "dikirim": "dikirim", "selesai": "selesai", "batal": "batal"}


def tab_pesanan(pesanan: Pesanan) -> str:
    return _TAB_DARI_STATUS[pesanan.status]


def label_pembeli(pesanan: Pesanan) -> str:
    """The status in the buyer's words."""
    bayar = pesanan.pembayaran
    if pesanan.status in ("baru", "menunggu_bayar"):
        if bayar and bayar.status == "menunggu":
            return "Pembayaran sedang dicek"
        if bayar and bayar.status == "ditolak":
            return "Bukti bayar ditolak"
        return "Belum bayar" if pesanan.bisa_dibayar else "Menunggu ongkir"
    if pesanan.status == "dikirim" and pesanan.pengiriman == "ambil":
        return "Siap diambil"
    return TAB_PESANAN[tab_pesanan(pesanan)]


def langkah_pesanan(pesanan: Pesanan) -> list[dict]:
    """The timeline: each step with whether it is done or current, and when it happened."""
    waktu = {r.status: r.waktu for r in pesanan.riwayat}
    bayar = pesanan.pembayaran
    tingkat = {"baru": 0, "menunggu_bayar": 0, "diproses": 2, "dikirim": 3, "selesai": 4}.get(pesanan.status, 0)
    lunas = bayar.diputus if bayar and bayar.status == "lunas" else None
    daftar = [("Pesanan dibuat", pesanan.dibuat), ("Dibayar", lunas), ("Dikemas", waktu.get("diproses")),
              ("Siap diambil" if pesanan.pengiriman == "ambil" else "Dikirim", waktu.get("dikirim")),
              ("Selesai", waktu.get("selesai"))]
    return [{"nama": nama, "waktu": wkt, "selesai": i < tingkat or tingkat == 4, "sekarang": i == tingkat and tingkat < 4}
            for i, (nama, wkt) in enumerate(daftar)]


def cari_pesanan_pembeli(db: Session, nomor: str, telepon: str) -> Pesanan | None:
    """Find an order from another device: both the order number and its WhatsApp number must match."""
    digit = "".join(c for c in telepon if c.isdigit())
    pesanan = db.get(Pesanan, int(nomor)) if nomor.isdigit() and len(nomor) < 10 else None
    if pesanan is None or not digit or "".join(c for c in pesanan.telepon if c.isdigit()) != digit:
        return None
    return pesanan


# ---------------------------------------------------------------- reviews


def buat_ulasan(db: Session, pesanan: Pesanan, bintang: int, komentar: str) -> Ulasan:
    if pesanan.status != "selesai":
        raise PesananError("Ulasan baru bisa dikirim setelah pesanan selesai.")
    if pesanan.ulasan is not None:
        raise PesananError("Pesanan ini sudah diberi ulasan.")
    ulasan = Ulasan(pesanan_id=pesanan.id, nama=pesanan.nama_pelanggan, bintang=max(1, min(5, bintang)),
                    komentar=komentar.strip()[:500])
    db.add(ulasan)
    catat(db, "web", "ulasan", f"Ulasan {ulasan.bintang} bintang dari {pesanan.nama_pelanggan} (pesanan #{pesanan.id})",
          ulasan.komentar, pesanan_id=pesanan.id, pembeli=pesanan.nama_pelanggan)
    return ulasan


def ulasan_tampil(db: Session, batas: int = 12) -> list[Ulasan]:
    return list(db.scalars(select(Ulasan).where(Ulasan.tampil.is_(True))
                           .order_by(Ulasan.dibuat.desc()).limit(batas)))


def jumlah_pesanan_selesai(db: Session) -> int:
    return db.scalar(select(func.count(Pesanan.id)).where(Pesanan.status == "selesai")) or 0


# ---------------------------------------------------------------- web chat

CHAT_JEDA_NOTIF = timedelta(minutes=5)  # one WhatsApp per conversation per 5 minutes, not one per message
CHAT_BATAS_PER_MENIT = 10


def chat_terlalu_cepat(db: Session, percakapan_id: int) -> bool:
    batas = datetime.now() - timedelta(minutes=1)
    jumlah = db.scalar(select(func.count(PesanChat.id)).where(
        PesanChat.percakapan_id == percakapan_id, PesanChat.pengirim == "pembeli", PesanChat.waktu >= batas))
    return (jumlah or 0) >= CHAT_BATAS_PER_MENIT


def chat_masuk(db: Session, p: Percakapan, isi: str) -> bool:
    """Store a customer message. Returns True when the owner should get a WhatsApp notification."""
    sekarang = datetime.now()
    p.terakhir = sekarang
    p.belum_dibaca += 1
    db.add(PesanChat(percakapan=p, pengirim="pembeli", isi=isi))
    kabari = p.notif_terakhir is None or sekarang - p.notif_terakhir >= CHAT_JEDA_NOTIF
    if kabari:
        p.notif_terakhir = sekarang
        catat(db, "web", "chat", f"Chat dari {p.nama}: {isi[:120]}", pembeli=p.nama, commit=False)
    db.commit()
    return kabari


def chat_balas(db: Session, p: Percakapan, isi: str) -> None:
    p.terakhir = datetime.now()
    p.belum_dibaca = 0
    p.notif_terakhir = None  # the next customer message is a new question, so notify again
    db.add(PesanChat(percakapan=p, pengirim="admin", isi=isi))
    db.commit()


def chat_belum_dibaca(db: Session) -> int:
    return db.scalar(select(func.coalesce(func.sum(Percakapan.belum_dibaca), 0))) or 0


# ---------------------------------------------------------------- rate limiting

_PERCOBAAN: dict[str, list[datetime]] = {}


def laju_terlampaui(kunci: str, batas: int, jendela: timedelta) -> bool:
    """In-memory sliding-window limiter, e.g. laju_terlampaui(f"login:{ip}", 8, timedelta(minutes=5)).

    Keyed per action+IP so one abusive visitor cannot brute-force the admin password, flood orders,
    or hammer the robot report endpoint. Good enough for the single process this app runs as; a
    restart just clears it, which only ever helps an attacker for a moment.
    """
    sekarang = datetime.now()
    sisa = [t for t in _PERCOBAAN.get(kunci, []) if sekarang - t < jendela]
    if len(sisa) >= batas:
        _PERCOBAAN[kunci] = sisa  # trimmed; a flood of blocked attempts must not grow this forever
        return True
    sisa.append(sekarang)
    _PERCOBAAN[kunci] = sisa
    return False


# ---------------------------------------------------------------- reports


def rentang_periode(periode: str) -> tuple[datetime, datetime, str]:
    hari_ini = date.today()
    if periode == "minggu_ini":
        mulai, label = hari_ini - timedelta(days=hari_ini.weekday()), "minggu ini"
    elif periode == "bulan_ini":
        mulai, label = hari_ini.replace(day=1), "bulan ini"
    else:
        mulai, label = hari_ini, "hari ini"
    return datetime.combine(mulai, time.min), datetime.combine(hari_ini + timedelta(days=1), time.min), label


def laporan(db: Session, mulai: datetime, akhir: datetime, batas_terlaris: int = 5) -> dict:
    sah = (Pesanan.status != "batal", Pesanan.dibuat >= mulai, Pesanan.dibuat < akhir)
    omzet, kg, jumlah = db.execute(
        select(func.coalesce(func.sum(Pesanan.subtotal - Pesanan.diskon), 0),
               func.coalesce(func.sum(Pesanan.total_kg), 0), func.count(Pesanan.id)).where(*sah)
    ).one()
    per_sumber = dict(db.execute(select(Pesanan.sumber, func.count(Pesanan.id)).where(*sah).group_by(Pesanan.sumber)).all())
    terlaris = db.execute(
        select(ItemPesanan.nama_produk, func.sum(ItemPesanan.jumlah), func.sum(ItemPesanan.jumlah * ItemPesanan.harga))
        .join(Pesanan).where(*sah).group_by(ItemPesanan.nama_produk)
        .order_by(func.sum(ItemPesanan.jumlah).desc()).limit(batas_terlaris)
    ).all() if batas_terlaris else []
    return {
        "omzet": int(omzet or 0),
        "total_kg": int(kg or 0),
        "jumlah_pesanan": int(jumlah or 0),
        "dari_web": int(per_sumber.get("web", 0)),
        "dari_robot": int(per_sumber.get("robot", 0)),
        "terlaris": [{"nama": n, "jumlah": int(q), "nilai": int(v)} for n, q, v in terlaris],
    }


def ekspor_excel(db: Session, mulai: datetime, akhir: datetime, label: str) -> bytes:
    """One sheet of order rows for the period, for the owner's own bookkeeping/taxes."""
    from openpyxl import Workbook
    from openpyxl.utils import get_column_letter

    wb = Workbook()
    ws = wb.active
    ws.title = "Pesanan"
    kolom = ["No", "Tanggal", "Pelanggan", "Sumber", "Status", "Jeruk", "Berat (kg)",
             "Subtotal", "Diskon", "Ongkir", "Total"]
    ws.append(kolom)
    for sel in ws[1]:
        sel.font = sel.font.copy(bold=True)
    q = select(Pesanan).where(Pesanan.dibuat >= mulai, Pesanan.dibuat < akhir).order_by(Pesanan.dibuat)
    for p in db.scalars(q):
        isi = ", ".join(f"{it.jumlah} kg {it.nama_produk}" for it in p.item)
        ws.append([p.id, p.dibuat.strftime("%d/%m/%Y %H:%M"), p.nama_pelanggan, p.sumber, LABEL_STATUS.get(p.status, p.status),
                  isi, p.total_kg, p.subtotal, p.diskon, p.ongkir or 0, p.total])
    for i, lebar in enumerate([6, 16, 20, 8, 20, 36, 10, 12, 10, 10, 12], start=1):
        ws.column_dimensions[get_column_letter(i)].width = lebar
    import io
    buf = io.BytesIO()
    wb.save(buf)
    return buf.getvalue()


def omzet_harian(db: Session, hari: int = 7) -> list[dict]:
    hasil = []
    for i in range(hari - 1, -1, -1):
        tgl = date.today() - timedelta(days=i)
        mulai = datetime.combine(tgl, time.min)
        data = laporan(db, mulai, mulai + timedelta(days=1), batas_terlaris=0)
        hasil.append({"tanggal": tgl, **data})
    return hasil


def aktivitas_robot_hari_ini(db: Session) -> int:
    mulai, akhir, _ = rentang_periode("hari_ini")
    return db.scalar(select(func.count(Aktivitas.id)).where(
        Aktivitas.jenis == "tanya_robot", Aktivitas.waktu >= mulai, Aktivitas.waktu < akhir)) or 0


def stok_menipis(db: Session, batas: int = 10) -> list[Produk]:
    return list(db.scalars(select(Produk).where(Produk.aktif.is_(True), Produk.stok <= batas).order_by(Produk.stok)))
