import asyncio
import hmac
import json
import logging
import secrets
from contextlib import asynccontextmanager
from datetime import date, datetime, timedelta
from urllib.parse import quote

from fastapi import BackgroundTasks, Depends, FastAPI, File, Form, HTTPException, Request, UploadFile
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, RedirectResponse, Response
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel, Field
from sqlalchemy import func, select
from sqlalchemy.orm import Session
from starlette.middleware.sessions import SessionMiddleware

from . import config, notif, services
from .config import BASE_DIR
from .db import SessionLocal, init_db
from .ikon import JERUK, SENYUM, ikon
from .models import Faq, KodePromo, Pembayaran, Percakapan, PesanChat, Pesanan, Produk
from .seed import isi_contoh_jika_kosong

log = logging.getLogger("toko")
APP_DIR = BASE_DIR / "app"


async def _laporan_harian_terjadwal():
    """Once a day around config.JAM_LAPORAN, WhatsApp the owner a short recap: today's sales and any
    low-stock products - so he does not have to open the dashboard to know how the day went."""
    while True:
        sekarang = datetime.now()
        try:
            jam, menit = (int(x) for x in config.JAM_LAPORAN.split(":"))
        except ValueError:
            jam, menit = 20, 0
        target = sekarang.replace(hour=jam, minute=menit, second=0, microsecond=0)
        if target <= sekarang:
            target += timedelta(days=1)
        await asyncio.sleep((target - sekarang).total_seconds())
        try:
            with SessionLocal() as db:
                mulai, akhir, _ = services.rentang_periode("hari_ini")
                d = services.laporan(db, mulai, akhir)
                menipis = services.stok_menipis(db)
            baris = [f"Laporan harian {config.NAMA_TOKO} - {target:%d/%m/%Y}",
                     f"Omzet: {services.rupiah(d['omzet'])} dari {d['jumlah_pesanan']} pesanan, {d['total_kg']} kg",
                     f"Dari web: {d['dari_web']} · dari robot: {d['dari_robot']}"]
            if menipis:
                baris.append("Stok menipis: " + ", ".join(f"{p.nama} ({p.stok} kg)" for p in menipis))
            notif.kirim_wa("\n".join(baris))
        except Exception:
            log.exception("Laporan harian otomatis gagal dikirim")


@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    with SessionLocal() as db:
        isi_contoh_jika_kosong(db)
    if config.ADMIN_PASSWORD in ("admin123", "ganti-password-ini"):
        log.warning("ADMIN_PASSWORD masih bawaan. Ganti di file .env sebelum web dibuka online!")
    bridge = None
    if config.MCP_ENDPOINT:
        from mcp_bridge import jalankan_bridge

        bridge = asyncio.create_task(jalankan_bridge(config.MCP_ENDPOINT))
    laporan_task = asyncio.create_task(_laporan_harian_terjadwal())
    yield
    if bridge is not None:
        bridge.cancel()
    laporan_task.cancel()


app = FastAPI(title=config.NAMA_TOKO, lifespan=lifespan)
app.add_middleware(SessionMiddleware, secret_key=config.SECRET_KEY, same_site="lax", max_age=60 * 60 * 12,
                   https_only=config.COOKIE_SECURE)
app.mount("/static", StaticFiles(directory=APP_DIR / "static"), name="static")

BATAS_BODI = 8 * 1024 * 1024  # a bit over the 5 MB photo limit (BATAS_BUKTI below); covers every form here


@app.middleware("http")
async def keamanan(request: Request, call_next):
    """Security headers + an early size cap, so a huge request body never even reaches a route handler."""
    panjang = request.headers.get("content-length")
    if panjang and panjang.isdigit() and int(panjang) > BATAS_BODI:
        return JSONResponse({"detail": "Berkas atau data yang dikirim terlalu besar."}, status_code=413)
    resp = await call_next(request)
    resp.headers["X-Content-Type-Options"] = "nosniff"
    resp.headers["X-Frame-Options"] = "DENY"
    resp.headers["Referrer-Policy"] = "strict-origin-when-cross-origin"
    resp.headers["Permissions-Policy"] = "geolocation=(), microphone=(), camera=()"
    resp.headers["Content-Security-Policy"] = (
        "default-src 'self'; script-src 'self' 'unsafe-inline'; "
        "style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; font-src https://fonts.gstatic.com; "
        "img-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'self'; "
        "frame-src https://maps.google.com https://www.google.com; "
        "form-action 'self'; frame-ancestors 'none'"
    )
    if config.COOKIE_SECURE:
        resp.headers["Strict-Transport-Security"] = "max-age=63072000; includeSubDomains"
    if request.url.path.startswith("/admin"):
        resp.headers["Cache-Control"] = "no-store"  # admin pages must not linger in a shared browser's cache
    return resp


def _ip(request: Request) -> str:
    return request.client.host if request.client else "?"

templates = Jinja2Templates(directory=APP_DIR / "templates")
templates.env.filters["rupiah"] = services.rupiah
templates.env.globals.update(
    toko=config, LABEL_STATUS=services.LABEL_STATUS, STATUS_PESANAN=services.STATUS_PESANAN,
    ikon=ikon, JERUK=JERUK, SENYUM=SENYUM, LABEL_BAYAR=services.LABEL_BAYAR, LABEL_METODE=services.LABEL_METODE,
    label_pembeli=services.label_pembeli, tab_pesanan=services.tab_pesanan, TAB_PESANAN=services.TAB_PESANAN,
    pesan_keputusan=services.pesan_keputusan,
    LABEL_JENIS=services.LABEL_JENIS,
)


def get_db():
    with SessionLocal() as db:
        yield db


def render(request: Request, name: str, **ctx):
    ctx.setdefault("admin", bool(request.session.get("admin")))
    if ctx["admin"]:
        with SessionLocal() as db:
            ctx["chat_belum"] = services.chat_belum_dibaca(db)
    return templates.TemplateResponse(request, name, ctx)


# ------------------------------------------------------------------ customer pages


@app.get("/", response_class=HTMLResponse)
def selamat_datang(request: Request, db: Session = Depends(get_db)):
    """Page 1: the welcome greeting."""
    return render(request, "welcome.html", jumlah_selesai=services.jumlah_pesanan_selesai(db))


@app.get("/pilih", response_class=HTMLResponse)
def pilih_peran(request: Request):
    """Page 2: buyers go straight to the shop, admins need the password."""
    return render(request, "pilih.html")


@app.get("/toko", response_class=HTMLResponse)
def beranda(request: Request, db: Session = Depends(get_db)):
    produk = services.daftar_produk(db)
    jenis = {}
    for p in produk:
        jenis.setdefault(p.jenis, []).append(p)
    faq = list(db.scalars(select(Faq).where(Faq.aktif.is_(True)).order_by(Faq.id)))
    return render(request, "index.html", jenis=jenis, faq=faq, ulasan=services.ulasan_tampil(db))


@app.get("/keranjang", response_class=HTMLResponse)
def keranjang(request: Request):
    return render(request, "keranjang.html", form={})


BATAS_BUKTI = 5 * 1024 * 1024
JENIS_FOTO = ((b"\xff\xd8\xff", "jpg"), (b"\x89PNG\r\n\x1a\n", "png"), (b"RIFF", "webp"))


def _pembayaran(db: Session, kode: str) -> Pembayaran:
    bayar = db.scalar(select(Pembayaran).where(Pembayaran.kode == kode))
    if bayar is None:
        raise HTTPException(404, "Halaman pembayaran tidak ditemukan")
    return bayar


def _jenis_foto(isi: bytes) -> str | None:
    for awal, jenis in JENIS_FOTO:
        if isi.startswith(awal) and (jenis != "webp" or isi[8:12] == b"WEBP"):
            return jenis
    return None


@app.get("/bayar/{kode}", response_class=HTMLResponse)
def halaman_bayar(request: Request, kode: str, error: str = "", baru: int = 0, db: Session = Depends(get_db)):
    bayar = _pembayaran(db, kode)
    return render(request, "bayar.html", bayar=bayar, pesanan=bayar.pesanan, error=error, baru=bool(baru),
                  wa_admin=config.wa_link(f"Halo {config.NAMA_TOKO}, saya sudah bayar pesanan #{bayar.pesanan_id} "
                                          f"sebesar {services.rupiah(bayar.pesanan.total)}."))


@app.post("/bayar/{kode}")
def kirim_bukti(request: Request, kode: str, tugas: BackgroundTasks, metode: str = Form(...),
                bank: str = Form(""), bukti: UploadFile | None = File(None), db: Session = Depends(get_db)):
    bayar = _pembayaran(db, kode)
    pesanan = bayar.pesanan
    kembali = f"/bayar/{kode}"
    if services.laju_terlampaui(f"bukti:{_ip(request)}", 10, timedelta(minutes=10)):
        return RedirectResponse(f"{kembali}?error={quote('Terlalu banyak percobaan, coba lagi sebentar lagi.')}",
                                status_code=303)
    if bayar.status == "lunas" or not pesanan.bisa_dibayar:
        return RedirectResponse(kembali, status_code=303)
    if metode not in services.LABEL_METODE or (metode == "transfer" and not config.ada_rekening) \
            or (metode == "cod" and pesanan.pengiriman != "ambil"):
        return RedirectResponse(f"{kembali}?error={quote('Pilih cara bayar dulu ya.')}", status_code=303)
    if metode == "transfer" and bank not in {b["bank"] for b in config.REKENING_BANKS}:
        return RedirectResponse(f"{kembali}?error={quote('Pilih bank tujuan transfer dulu ya.')}", status_code=303)
    percakapan = _chat_saya(request, db)
    if percakapan is None:
        percakapan = Percakapan(nama=pesanan.nama_pelanggan, telepon=pesanan.telepon)
        db.add(percakapan)
        db.flush()
        request.session["chat_id"] = percakapan.id
    if metode == "cod":
        services.konfirmasi_cod(db, bayar, percakapan)
        tugas.add_task(notif.kirim_wa,
                       f"Pesanan COD (bayar di tempat)\nPesanan #{pesanan.id} - {pesanan.nama_pelanggan}\n"
                       f"Jumlah: {services.rupiah(bayar.jumlah)}\n\nTerima uangnya saat pembeli ambil, "
                       "lalu tandai lunas di Admin > Pesanan.")
        return RedirectResponse(f"/pesanan/{kode}?dibayar=1", status_code=303)
    if bukti is None:
        return RedirectResponse(f"{kembali}?error={quote('Pilih foto bukti bayar dulu ya.')}", status_code=303)
    isi = bukti.file.read(BATAS_BUKTI + 1)
    jenis = _jenis_foto(isi)
    if jenis is None or len(isi) > BATAS_BUKTI:
        return RedirectResponse(f"{kembali}?error={quote('Bukti harus berupa foto JPG, PNG, atau WEBP, maksimal 5 MB.')}",
                                status_code=303)
    config.BUKTI_DIR.mkdir(parents=True, exist_ok=True)
    nama_berkas = f"{pesanan.id}-{secrets.token_hex(8)}.{jenis}"
    (config.BUKTI_DIR / nama_berkas).write_bytes(isi)
    services.simpan_bukti(db, bayar, metode, nama_berkas, percakapan, bank)
    cara = f"{services.LABEL_METODE[metode]} {bank}" if metode == "transfer" else services.LABEL_METODE[metode]
    tugas.add_task(notif.kirim_wa,
                   f"Pembayaran masuk, mohon dicek\nPesanan #{pesanan.id} - {pesanan.nama_pelanggan}\n"
                   f"Jumlah: {services.rupiah(bayar.jumlah)}\nCara bayar: {cara}\n\n"
                   "Pastikan uangnya sudah masuk, lalu terima di Admin > Pesanan.")
    return RedirectResponse(f"/pesanan/{kode}?dibayar=1", status_code=303)


@app.get("/api/produk")
def api_produk(ids: str = "", db: Session = Depends(get_db)):
    id_list = [int(x) for x in ids.split(",") if x.strip().isdigit()][:100]
    if not id_list:
        return []
    rows = db.scalars(select(Produk).where(Produk.id.in_(id_list), Produk.aktif.is_(True)))
    return [{"id": p.id, "nama": p.nama, "harga": p.harga, "stok": p.stok} for p in rows]


@app.post("/pesan", response_class=HTMLResponse)
def pesan(
    request: Request,
    nama: str = Form(...),
    telepon: str = Form(...),
    pengiriman: str = Form("antar"),
    alamat: str = Form(""),
    keperluan: str = Form("biasa"),
    tanggal_butuh: str = Form(""),
    catatan: str = Form(""),
    kode_promo: str = Form(""),
    item_json: str = Form(...),
    db: Session = Depends(get_db),
):
    form = {"nama": nama, "telepon": telepon, "pengiriman": pengiriman, "alamat": alamat,
            "keperluan": keperluan, "tanggal_butuh": tanggal_butuh, "catatan": catatan, "kode_promo": kode_promo}
    if services.laju_terlampaui(f"pesan:{_ip(request)}", 10, timedelta(minutes=10)):
        return render(request, "keranjang.html", error="Terlalu banyak pesanan dalam waktu singkat, coba lagi sebentar lagi.", form=form)
    try:
        item = [(int(d["id"]), int(d["jumlah"])) for d in json.loads(item_json)]
        tgl = date.fromisoformat(tanggal_butuh) if tanggal_butuh else None
    except (ValueError, KeyError, TypeError):
        return render(request, "keranjang.html", error="Data pesanan tidak valid, coba ulangi.", form=form)
    try:
        pesanan = services.buat_pesanan(db, nama, item, telepon, alamat, pengiriman, keperluan, tgl,
                                        catatan, sumber="web", kode_promo=kode_promo)
    except services.PesananError as e:
        db.rollback()
        return render(request, "keranjang.html", error=str(e), form=form)
    kode = pesanan.pembayaran.kode
    # Shopee-style: go straight to picking a payment method when the total is already final
    # (pickup orders). Delivery orders still need the admin to set ongkir first.
    tujuan = f"/bayar/{kode}?baru=1" if pesanan.bisa_dibayar else f"/pesanan/{kode}?baru=1"
    return _ke_pesanan(request, pesanan, tujuan)


def _kode_saya(request: Request) -> list[str]:
    return [k for k in request.cookies.get("pesanan_saya", "").split(".") if k]


def _ke_pesanan(request: Request, pesanan: Pesanan, tujuan: str) -> RedirectResponse:
    """Go to `tujuan` and remember the order in this browser.

    Buyers have no account, so a year-long cookie of the secret order codes is their order list.
    It is separate from the session, which ends after 12 hours to keep admin logins short.
    """
    kode = pesanan.pembayaran.kode
    daftar = [kode, *(k for k in _kode_saya(request) if k != kode)][:30]
    resp = RedirectResponse(tujuan, status_code=303)
    resp.set_cookie("pesanan_saya", ".".join(daftar), max_age=365 * 24 * 3600, httponly=True, samesite="lax")
    return resp


@app.get("/pesanan-saya", response_class=HTMLResponse)
def pesanan_saya(request: Request, tab: str = "", error: str = "", db: Session = Depends(get_db)):
    kode = _kode_saya(request)
    semua = list(db.scalars(select(Pesanan).join(Pembayaran).where(Pembayaran.kode.in_(kode))
                            .order_by(Pesanan.dibuat.desc()))) if kode else []
    tampil = [p for p in semua if not tab or services.tab_pesanan(p) == tab]
    jumlah = {t: sum(1 for p in semua if services.tab_pesanan(p) == t) for t in services.TAB_PESANAN}
    return render(request, "pesanan_saya.html", daftar=tampil, tab=tab, jumlah=jumlah, total=len(semua), error=error)


@app.post("/pesanan-saya/cari")
def cari_pesanan_saya(request: Request, nomor: str = Form(...), telepon: str = Form(...), db: Session = Depends(get_db)):
    if services.laju_terlampaui(f"cari:{_ip(request)}", 15, timedelta(minutes=5)):
        return RedirectResponse(f"/pesanan-saya?error={quote('Terlalu banyak percobaan. Coba lagi beberapa menit lagi.')}",
                                status_code=303)
    pesanan = services.cari_pesanan_pembeli(db, nomor.strip().lstrip("#"), telepon)
    if pesanan is None:
        return RedirectResponse(f"/pesanan-saya?error={quote('Pesanan tidak ditemukan. Cek lagi nomor pesanan dan nomor WhatsApp-nya.')}",
                                status_code=303)
    return _ke_pesanan(request, pesanan, f"/pesanan/{pesanan.pembayaran.kode}")


@app.get("/pesanan/{kode}", response_class=HTMLResponse)
def detail_pesanan(request: Request, kode: str, baru: int = 0, dibayar: int = 0, db: Session = Depends(get_db)):
    pesanan = _pembayaran(db, kode).pesanan
    return render(request, "pesanan_detail.html", pesanan=pesanan, baru=bool(baru), dibayar=bool(dibayar),
                  langkah=services.langkah_pesanan(pesanan), wa=config.wa_link(services.pesan_whatsapp(pesanan)))


@app.get("/pesanan/{kode}/status")
def status_pesanan(kode: str, db: Session = Depends(get_db)):
    """Polled by the open order page so a change by the admin shows up without a manual refresh."""
    return {"status": services.label_pembeli(_pembayaran(db, kode).pesanan)}


@app.post("/pesanan/{kode}/ulasan")
def kirim_ulasan(kode: str, bintang: int = Form(...), komentar: str = Form(""), db: Session = Depends(get_db)):
    pesanan = _pembayaran(db, kode).pesanan
    try:
        services.buat_ulasan(db, pesanan, bintang, komentar)
    except services.PesananError as e:
        db.rollback()
        return RedirectResponse(f"/pesanan/{kode}?error_ulasan={quote(str(e))}", status_code=303)
    return RedirectResponse(f"/pesanan/{kode}?ulasan_terkirim=1", status_code=303)


# ------------------------------------------------------------------ customer chat


def _pesan_json(m: PesanChat) -> dict:
    return {"id": m.id, "pengirim": m.pengirim, "isi": m.isi, "waktu": m.waktu.strftime("%d/%m %H:%M")}


def _chat_saya(request: Request, db: Session) -> Percakapan | None:
    chat_id = request.session.get("chat_id")
    return db.get(Percakapan, chat_id) if chat_id else None


@app.get("/chat/pesan")
def chat_pesan(request: Request, setelah: int = 0, db: Session = Depends(get_db)):
    p = _chat_saya(request, db)
    if p is None:
        return {"aktif": False, "nama": "", "pesan": []}
    rows = db.scalars(select(PesanChat).where(PesanChat.percakapan_id == p.id, PesanChat.id > setelah)
                      .order_by(PesanChat.id))
    return {"aktif": True, "nama": p.nama, "pesan": [_pesan_json(m) for m in rows]}


class LaporanRobot(BaseModel):
    token: str
    status: str = ""
    dengar: str = Field("", max_length=1000)
    jawab: str = Field("", max_length=1000)


@app.post("/api/robot/laporan")
def laporan_robot(request: Request, lap: LaporanRobot, db: Session = Depends(get_db)):
    """Called by the robot's firmware: its state every 20 seconds and each real conversation."""
    if services.laju_terlampaui(f"robot:{_ip(request)}", 60, timedelta(minutes=1)):
        raise HTTPException(429, "Terlalu banyak percobaan")
    if not config.ROBOT_TOKEN or not hmac.compare_digest(lap.token.encode(), config.ROBOT_TOKEN.encode()):
        raise HTTPException(403, "Token robot salah")
    services.robot_lapor(db, lap.status, lap.dengar, lap.jawab)
    return {"ok": True}


@app.post("/chat/kirim")
def chat_kirim(request: Request, tugas: BackgroundTasks, isi: str = Form(...), nama: str = Form(""),
               telepon: str = Form(""), db: Session = Depends(get_db)):
    if services.laju_terlampaui(f"chat:{_ip(request)}", 20, timedelta(minutes=1)):
        raise HTTPException(429, "Terlalu cepat, coba lagi sebentar ya.")
    isi = isi.strip()[:1000]
    if not isi:
        raise HTTPException(400, "Pesannya masih kosong, kakak-kakak atau abang-abang.")
    p = _chat_saya(request, db)
    if p is None:
        nama = nama.strip()[:60]
        if not nama:
            raise HTTPException(400, "Isi nama dulu ya, kakak-kakak atau abang-abang.")
        p = Percakapan(nama=nama, telepon="".join(c for c in telepon if c.isdigit() or c == "+")[:30])
        db.add(p)
        db.flush()
        request.session["chat_id"] = p.id
    elif services.chat_terlalu_cepat(db, p.id):
        raise HTTPException(429, "Terlalu cepat, kakak-kakak atau abang-abang. Tunggu sebentar ya.")
    if services.chat_masuk(db, p, isi):
        kontak = f" ({p.telepon})" if p.telepon else ""
        tugas.add_task(notif.kirim_wa, f"Chat baru di web {config.NAMA_TOKO}\nDari: {p.nama}{kontak}\n\n{isi}\n\n"
                                       "Balas lewat Admin > Chat di web.")
    return {"ok": True}


# ------------------------------------------------------------------ admin


class PerluLogin(Exception):
    pass


@app.exception_handler(PerluLogin)
def _ke_login(request: Request, exc: PerluLogin):
    return RedirectResponse("/admin/login", status_code=303)


def wajib_admin(request: Request):
    if not request.session.get("admin"):
        raise PerluLogin()


@app.get("/admin/login", response_class=HTMLResponse)
def login_form(request: Request):
    if request.session.get("admin"):
        return RedirectResponse("/admin", status_code=303)
    return render(request, "admin/login.html")


@app.post("/admin/login", response_class=HTMLResponse)
def login(request: Request, password: str = Form(...)):
    if services.laju_terlampaui(f"login:{_ip(request)}", 8, timedelta(minutes=5)):
        return render(request, "admin/login.html", error="Terlalu banyak percobaan. Coba lagi beberapa menit lagi.")
    if hmac.compare_digest(password.encode(), config.ADMIN_PASSWORD.encode()):
        request.session["admin"] = True
        return RedirectResponse("/admin", status_code=303)
    return render(request, "admin/login.html", error="Password salah.")


@app.post("/admin/logout")
def logout(request: Request):
    request.session.clear()
    return RedirectResponse("/", status_code=303)


@app.get("/admin", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def dashboard(request: Request, db: Session = Depends(get_db)):
    m, a, _ = services.rentang_periode("hari_ini")
    hari_ini = services.laporan(db, m, a)
    m, a, _ = services.rentang_periode("bulan_ini")
    bulan_ini = services.laporan(db, m, a)
    harian = services.omzet_harian(db, 7)
    maks = max([h["omzet"] for h in harian] + [1])
    perlu = list(db.scalars(select(Pesanan).where(Pesanan.status.in_(["baru", "menunggu_bayar"]))
                            .order_by(Pesanan.dibuat.desc())))
    aktivitas = services.aktivitas_terbaru(db, batas=15)
    return render(request, "admin/dashboard.html", hari_ini=hari_ini, bulan_ini=bulan_ini, harian=harian,
                  maks=maks, perlu=perlu, menipis=services.stok_menipis(db), aktivitas=aktivitas,
                  id_terakhir=aktivitas[0].id if aktivitas else 0, kelompok=services.aktivitas_per_pembeli(db),
                  robot_hari_ini=services.aktivitas_robot_hari_ini(db), robot=status_robot(), xiaozhi=status_xiaozhi(),
                  robot_dipakai=robot_terakhir_dipakai(db))


def status_xiaozhi() -> dict:
    """The live state of the web <-> Airamon (Xiaozhi) link. It says nothing about the physical robot."""
    try:
        from mcp_bridge import STATUS
    except ImportError:
        STATUS = {"terhubung": False, "sejak": None}
    if STATUS["terhubung"]:
        return {"kode": "aktif", "teks": f"Server web siap terima robot sejak {STATUS['sejak']:%H:%M} (ini bukan status fisik robot)"}
    if config.MCP_ENDPOINT:
        return {"kode": "terputus", "teks": "Server web terputus dari layanan Airamon, menyambung ulang..."}
    return {"kode": "belum", "teks": "Server web belum disambungkan ke layanan Airamon"}


LABEL_STATUS_ROBOT = {"siap": "siap dipanggil", "mendengarkan": "sedang mendengarkan", "berbicara": "sedang berbicara"}

# States the firmware can report where it is online but NOT usable (mid wifi-reconfig, OTA, activation,
# or a crash alert) - these used to fall through to the generic "siap" state and show as a healthy green
# "aktif" dot even though the robot could not actually respond to being called.
ROBOT_STATUS_BERMASALAH = {
    "atur_wifi": "sedang mengatur ulang WiFi",
    "update": "sedang update firmware",
    "aktivasi": "belum diaktivasi",
    "error": "mengalami error",
}


def status_robot() -> dict:
    """The physical robot, from its own reports: nothing is assumed while it stays silent."""
    terakhir = services.ROBOT["terakhir"]
    if terakhir is None:
        return {"kode": "belum", "teks": "Robot belum melapor sejak web dinyalakan"}
    if datetime.now() - terakhir > services.ROBOT_BATAS_DIAM:
        return {"kode": "terputus", "teks": f"Robot mati atau putus · terakhir terlihat {terakhir:%H:%M}"}
    status = services.ROBOT["status"]
    if status in ROBOT_STATUS_BERMASALAH:
        return {"kode": "terputus", "teks": f"Robot menyala tapi {ROBOT_STATUS_BERMASALAH[status]}, belum bisa dipanggil"}
    return {"kode": "aktif", "teks": "Robot menyala · " + LABEL_STATUS_ROBOT.get(status, "aktif")}


def robot_terakhir_dipakai(db: Session) -> str:
    terakhir = services.aktivitas_robot_terakhir(db)
    return f"terakhir dipakai {terakhir:%d/%m %H:%M}" if terakhir else "belum pernah dipakai pelanggan"


@app.get("/admin/api/live", dependencies=[Depends(wajib_admin)])
def api_live(request: Request, setelah: int = 0, db: Session = Depends(get_db)):
    """Polled by the dashboard every few seconds: new activities, today's numbers, robot status."""
    baru = services.aktivitas_setelah(db, setelah)
    m, a, _ = services.rentang_periode("hari_ini")
    hari_ini = services.laporan(db, m, a, batas_terlaris=0)
    m, a, _ = services.rentang_periode("bulan_ini")
    bulan_ini = services.laporan(db, m, a, batas_terlaris=0)
    perlu_html = None
    if any(x.jenis in ("pesanan", "status", "pembayaran") for x in baru):
        perlu = list(db.scalars(select(Pesanan).where(Pesanan.status.in_(["baru", "menunggu_bayar"]))
                                .order_by(Pesanan.dibuat.desc())))
        perlu_html = templates.get_template("admin/_pesanan.html").render(
            rows=perlu, kembali="/admin", toko=config, LABEL_STATUS=services.LABEL_STATUS,
            STATUS_PESANAN=services.STATUS_PESANAN, request=request)
    pembeli_html = None
    if any(x.pembeli for x in baru):
        pembeli_html = templates.get_template("admin/_pembeli.html").render(
            kelompok=services.aktivitas_per_pembeli(db), toko=config, LABEL_JENIS=services.LABEL_JENIS,
            request=request)
    return {
        "aktivitas": [
            {"id": x.id, "waktu": x.waktu.strftime("%H:%M:%S"), "sumber": x.sumber, "jenis": x.jenis,
             "label": services.LABEL_JENIS.get(x.jenis, x.jenis), "ringkasan": x.ringkasan,
             "detail": x.detail, "pesanan_id": x.pesanan_id}
            for x in baru
        ],
        "stat": {"omzet": services.rupiah(hari_ini["omzet"]), "jumlah": hari_ini["jumlah_pesanan"],
                 "kg": hari_ini["total_kg"], "web": hari_ini["dari_web"], "robot": hari_ini["dari_robot"],
                 "robot_hari_ini": services.aktivitas_robot_hari_ini(db), "robot_dipakai": robot_terakhir_dipakai(db),
                 "omzet_bulan": services.rupiah(bulan_ini["omzet"]), "kg_bulan": bulan_ini["total_kg"]},
        "robot": status_robot(),
        "xiaozhi": status_xiaozhi(),
        "perlu_html": perlu_html,
        "pembeli_html": pembeli_html,
        "jumlah_perlu": db.scalar(select(func.count(Pesanan.id)).where(Pesanan.status.in_(["baru", "menunggu_bayar"]))),
    }


@app.get("/admin/aktivitas", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def admin_aktivitas(request: Request, sumber: str = "", db: Session = Depends(get_db)):
    return render(request, "admin/aktivitas.html", aktivitas=services.aktivitas_terbaru(db, sumber, 300),
                  sumber=sumber)


@app.get("/admin/chat", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def admin_chat(request: Request, id: int = 0):
    return render(request, "admin/chat.html", chat_id=id)


@app.get("/admin/api/chat", dependencies=[Depends(wajib_admin)])
def api_chat(id: int = 0, setelah: int = 0, db: Session = Depends(get_db)):
    """Polled by the admin chat page; reading a conversation marks it as read."""
    pesan = []
    p = db.get(Percakapan, id) if id else None
    if p is not None:
        rows = db.scalars(select(PesanChat).where(PesanChat.percakapan_id == id, PesanChat.id > setelah)
                          .order_by(PesanChat.id))
        pesan = [_pesan_json(m) for m in rows]
        if p.belum_dibaca:
            p.belum_dibaca = 0
            db.commit()
    daftar = []
    for c in db.scalars(select(Percakapan).order_by(Percakapan.terakhir.desc()).limit(100)):
        daftar.append({"id": c.id, "nama": c.nama, "telepon": c.telepon, "belum": c.belum_dibaca,
                       "cuplikan": c.pesan[-1].isi[:60] if c.pesan else "",
                       "waktu": c.terakhir.strftime("%d/%m %H:%M"),
                       "wa": config.wa_link(nomor=c.telepon) if c.telepon else ""})
    return {"daftar": daftar, "pesan": pesan}


@app.post("/admin/chat/{chat_id}/balas", dependencies=[Depends(wajib_admin)])
def admin_chat_balas(chat_id: int, isi: str = Form(...), db: Session = Depends(get_db)):
    p = db.get(Percakapan, chat_id)
    isi = isi.strip()[:1000]
    if p is None or not isi:
        raise HTTPException(404)
    services.chat_balas(db, p, isi)
    return {"ok": True}


@app.get("/admin/produk", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def admin_produk(request: Request, db: Session = Depends(get_db)):
    return render(request, "admin/produk.html", produk=services.daftar_produk(db, semua=True))


@app.get("/admin/produk/baru", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def produk_baru(request: Request):
    return render(request, "admin/produk_form.html", p=None)


@app.get("/admin/produk/{produk_id}", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def produk_edit(request: Request, produk_id: int, db: Session = Depends(get_db)):
    p = db.get(Produk, produk_id)
    if p is None:
        raise HTTPException(404)
    return render(request, "admin/produk_form.html", p=p)


@app.post("/admin/produk/simpan", dependencies=[Depends(wajib_admin)])
def produk_simpan(
    produk_id: int | None = Form(None),
    jenis: str = Form(...),
    ukuran: str = Form(""),
    deskripsi: str = Form(""),
    harga: int = Form(...),
    stok: int = Form(0),
    aktif: bool = Form(False),
    db: Session = Depends(get_db),
):
    p = db.get(Produk, produk_id) if produk_id else Produk()
    if p is None:
        raise HTTPException(404)
    p.jenis = jenis.strip()[:60]
    p.ukuran = ukuran.strip()[:30]
    p.nama = f"{p.jenis} {p.ukuran}".strip()
    p.deskripsi = deskripsi.strip()
    p.satuan = "kg"
    p.harga = max(0, harga)
    p.stok = max(0, stok)
    p.aktif = aktif
    db.add(p)
    services.catat(db, "admin", "produk", f"Produk {p.nama} disimpan: {services.rupiah(p.harga)}/kg, stok {p.stok} kg",
                   commit=False)
    db.commit()
    return RedirectResponse("/admin/produk", status_code=303)


@app.post("/admin/produk/{produk_id}/stok", dependencies=[Depends(wajib_admin)])
def produk_tambah_stok(produk_id: int, tambah: int = Form(...), db: Session = Depends(get_db)):
    p = db.get(Produk, produk_id)
    if p is None:
        raise HTTPException(404)
    p.stok = max(0, p.stok + tambah)
    services.cek_alert_stok(p)  # clears the "already alerted" flag once restocked above the threshold
    services.catat(db, "admin", "stok", f"Stok {p.nama} {'+' if tambah >= 0 else ''}{tambah} kg → {p.stok} kg",
                   commit=False)
    db.commit()
    return RedirectResponse("/admin/produk", status_code=303)


@app.get("/admin/pesanan", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def admin_pesanan(request: Request, status: str = "", sumber: str = "", db: Session = Depends(get_db)):
    q = select(Pesanan).order_by(Pesanan.dibuat.desc()).limit(300)
    if status:
        q = q.where(Pesanan.status == status)
    if sumber:
        q = q.where(Pesanan.sumber == sumber)
    return render(request, "admin/pesanan.html", pesanan=list(db.scalars(q)), status=status, sumber=sumber,
                  error=request.query_params.get("error"))


@app.post("/admin/pembayaran/{bayar_id}", dependencies=[Depends(wajib_admin)])
def admin_putuskan_bayar(tugas: BackgroundTasks, bayar_id: int, keputusan: str = Form(...),
                         kembali: str = Form("/admin/pesanan"), db: Session = Depends(get_db)):
    if not kembali.startswith("/admin") or kembali.startswith("//"):
        kembali = "/admin/pesanan"
    bayar = db.get(Pembayaran, bayar_id)
    if bayar is None:
        raise HTTPException(404, "Pembayaran tidak ditemukan")
    try:
        services.putuskan_bayar(db, bayar, keputusan == "terima")
    except services.PesananError as e:
        db.rollback()
        return RedirectResponse(f"/admin/pesanan?error={quote(str(e))}", status_code=303)
    if bayar.pesanan.telepon:
        tugas.add_task(notif.kirim_wa, services.pesan_keputusan(bayar, keputusan == "terima"), bayar.pesanan.telepon)
    return RedirectResponse(kembali, status_code=303)


@app.get("/admin/bukti/{bayar_id}", dependencies=[Depends(wajib_admin)])
def admin_lihat_bukti(bayar_id: int, db: Session = Depends(get_db)):
    bayar = db.get(Pembayaran, bayar_id)
    if bayar is None or not bayar.bukti:
        raise HTTPException(404, "Bukti tidak ditemukan")
    return FileResponse(config.BUKTI_DIR / bayar.bukti)


@app.post("/admin/pesanan/{pesanan_id}", dependencies=[Depends(wajib_admin)])
def admin_ubah_pesanan(request: Request, tugas: BackgroundTasks, pesanan_id: int, status: str = Form(...),
                       ongkir: str = Form(""), estimasi_hari: str = Form(""), kembali: str = Form("/admin/pesanan"),
                       db: Session = Depends(get_db)):
    if not kembali.startswith("/admin"):
        kembali = "/admin/pesanan"
    status_lama = db.scalar(select(Pesanan.status).where(Pesanan.id == pesanan_id))
    try:
        nilai_ongkir = int(ongkir) if ongkir.strip() else None
        pesanan = services.ubah_pesanan(db, pesanan_id, status, nilai_ongkir, estimasi_hari)
    except ValueError as e:
        return RedirectResponse(f"/admin/pesanan?error={quote(str(e))}", status_code=303)
    if pesanan.telepon and status_lama is not None and status_lama != pesanan.status:
        tugas.add_task(notif.kirim_wa, services.pesan_status(pesanan, str(request.base_url)), pesanan.telepon)
    return RedirectResponse(kembali, status_code=303)


@app.post("/admin/pesanan/{pesanan_id}/ongkir-otomatis", dependencies=[Depends(wajib_admin)])
def admin_ongkir_otomatis(pesanan_id: int, kembali: str = Form("/admin/pesanan"), db: Session = Depends(get_db)):
    """Distance-based ongkir via Google Maps - only does anything once GOOGLE_MAPS_API_KEY is set."""
    if not kembali.startswith("/admin"):
        kembali = "/admin/pesanan"
    pesanan = db.get(Pesanan, pesanan_id)
    if pesanan is None:
        raise HTTPException(404)
    ongkir = services.hitung_ongkir_jarak(pesanan.alamat)
    if ongkir is None:
        return RedirectResponse(f"/admin/pesanan?error={quote('Ongkir otomatis belum aktif atau alamat tidak ditemukan. Isi GOOGLE_MAPS_API_KEY di .env, atau hitung manual.')}", status_code=303)
    services.ubah_pesanan(db, pesanan_id, pesanan.status, ongkir)
    return RedirectResponse(kembali, status_code=303)


@app.get("/admin/pesanan/{pesanan_id}/cetak", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def admin_cetak_label(request: Request, pesanan_id: int, db: Session = Depends(get_db)):
    pesanan = db.get(Pesanan, pesanan_id)
    if pesanan is None:
        raise HTTPException(404)
    return render(request, "admin/cetak_label.html", pesanan=pesanan)


@app.get("/admin/faq", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def admin_faq(request: Request, db: Session = Depends(get_db)):
    belum_terjawab = services.aktivitas_terbaru(db, sumber="robot", batas=300)
    belum_terjawab = [a for a in belum_terjawab if a.jenis == "tanya_tak_terjawab"][:15]
    return render(request, "admin/faq.html", faq=list(db.scalars(select(Faq).order_by(Faq.id))),
                  belum_terjawab=belum_terjawab)


@app.post("/admin/faq/simpan", dependencies=[Depends(wajib_admin)])
def admin_faq_simpan(faq_id: int | None = Form(None), pertanyaan: str = Form(...), jawaban: str = Form(...),
                     aktif: bool = Form(False), db: Session = Depends(get_db)):
    f = db.get(Faq, faq_id) if faq_id else Faq()
    if f is None:
        raise HTTPException(404)
    f.pertanyaan, f.jawaban, f.aktif = pertanyaan.strip(), jawaban.strip(), aktif
    db.add(f)
    db.commit()
    return RedirectResponse("/admin/faq", status_code=303)


@app.get("/admin/laporan", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def admin_laporan(request: Request, periode: str = "hari_ini", db: Session = Depends(get_db)):
    mulai, akhir, label = services.rentang_periode(periode)
    return render(request, "admin/laporan.html", data=services.laporan(db, mulai, akhir, 10),
                  periode=periode, label=label)


@app.get("/admin/laporan/export", dependencies=[Depends(wajib_admin)])
def admin_laporan_export(periode: str = "hari_ini", db: Session = Depends(get_db)):
    mulai, akhir, label = services.rentang_periode(periode)
    isi = services.ekspor_excel(db, mulai, akhir, label)
    nama = f"laporan-{label.replace(' ', '-')}-{date.today():%Y%m%d}.xlsx"
    return Response(isi, media_type="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
                    headers={"Content-Disposition": f'attachment; filename="{nama}"'})


@app.get("/admin/promo", response_class=HTMLResponse, dependencies=[Depends(wajib_admin)])
def admin_promo(request: Request, db: Session = Depends(get_db)):
    return render(request, "admin/promo.html", promo=services.daftar_promo(db),
                  error=request.query_params.get("error"))


@app.post("/admin/promo/simpan", dependencies=[Depends(wajib_admin)])
def admin_promo_simpan(promo_id: int | None = Form(None), kode: str = Form(...), jenis: str = Form("nominal"),
                       nilai: int = Form(...), kuota: int = Form(0), tak_terbatas: bool = Form(False),
                       kadaluarsa: str = Form(""), aktif: bool = Form(False), db: Session = Depends(get_db)):
    try:
        exp = date.fromisoformat(kadaluarsa) if kadaluarsa.strip() else None
        services.simpan_promo(db, promo_id, kode, jenis, nilai, kuota, tak_terbatas, exp, aktif)
    except (ValueError, services.PesananError) as e:
        return RedirectResponse(f"/admin/promo?error={quote(str(e))}", status_code=303)
    return RedirectResponse("/admin/promo", status_code=303)


@app.get("/sehat")
def sehat():
    return JSONResponse({"ok": True})
