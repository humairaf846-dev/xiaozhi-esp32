# Toko Jeruk Web + Robot Xiaozhi

Web jualan jeruk (jeruk manis & jeruk peras) dengan database SQL, tersambung ke robot xiaozhi lewat MCP.
Semua aktivitas robot dan pelanggan tercatat di `/admin/aktivitas`.

- **Pembeli** (`/`): lihat katalog, cari produk, masukkan ke keranjang, kirim pesanan.
- **Admin** (`/admin`): kelola produk & stok, proses pesanan, lihat laporan omzet dan produk terlaris.
- **Robot**: menjawab harga & stok (`cari_produk`), menjawab pertanyaan toko dari FAQ dataset (`info_toko`),
  menerima pesanan lewat suara (`buat_pesanan`), dan membacakan laporan penjualan pakai PIN (`laporan_penjualan`).
  FAQ bisa diubah di `/admin/faq`.

```
Pelanggan bicara ──► Robot ESP32 ──► Server xiaozhi.me (AI) ──► MCP endpoint (wss)
                                                                     │
Web ini (mcp_bridge.py ─► mcp_server.py) ◄───────────────────────────┘
         │
         └──► Database SQL (SQLite lokal / MySQL / PostgreSQL)
```

## Struktur

| File | Isi |
|---|---|
| `app/main.py` | Halaman web (pembeli + admin) |
| `app/services.py` | Logika toko: cari produk, buat pesanan, stok, laporan (dipakai web & robot) |
| `app/models.py` | Tabel database: `produk`, `pesanan`, `item_pesanan`, `faq`, `aktivitas` |
| `app/seed.py` | 6 produk jeruk + 24 tanya jawab dari dataset toko (diisi otomatis saat database kosong) |
| `mcp_server.py` | Alat untuk robot: `cari_produk`, `info_toko`, `buat_pesanan`, `laporan_penjualan` (semua dicatat ke tabel aktivitas) |
| `mcp_bridge.py` | Penghubung alat-alat di atas ke MCP endpoint xiaozhi |

## Menjalankan di komputer sendiri

```bash
cd toko-web
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
cp .env.example .env        # lalu isi nilainya
.venv/bin/uvicorn app.main:app --port 8000
```

Buka http://localhost:8000 (toko) dan http://localhost:8000/admin (admin).

## Menyambungkan ke robot

1. Buka **Xiaozhi Console** → **Agents** → pilih agen robot → **Configure**.
2. Cari bagian **MCP endpoint** (alamat yang diawali `wss://...?token=...`) lalu salin.
   Token ini rahasia: siapa pun yang memegangnya bisa memberi alat ke robotmu.
3. Tempel ke `.env`: `MCP_ENDPOINT=wss://...`
4. Jalankan ulang web. Di log akan muncul `Tersambung ke robot`. Dashboard admin juga
   menampilkan status sambungan.
5. Agar robot tahu perannya, isi **role / prompt** agen, misalnya:

   > Kamu adalah robot kucing biru penjaga toko Yufasya Family yang menjual jeruk manis dan
   > jeruk peras. Layani pelanggan dengan ramah dalam bahasa Indonesia, singkat, sapa dengan "Hallo kakak-kakak dan abang-abang semua", dan panggil pelanggan "kakak-kakak atau abang-abang", jangan "kak" atau "abang" saja.
   > Untuk harga dan stok selalu pakai alat cari_produk. Untuk pertanyaan lain tentang toko (pengiriman,
   > ongkir, COD, pembayaran, kesegaran, penyimpanan, komplain, jam buka, alamat, diskon, oleh-oleh)
   > selalu pakai alat info_toko dan jangan mengarang jawaban. Untuk memesan, tanyakan nama, nomor
   > WhatsApp, jenis + ukuran + jumlah kg, dan diantar (alamat) atau ambil di toko, lalu pakai buat_pesanan.
   > Laporan penjualan hanya untuk pemilik toko dan butuh PIN.

Contoh percakapan:
- "Harga jeruk manis berapa?" → `cari_produk`.
- "Bisa COD nggak?" → `info_toko` → "Mohon maaf kakak-kakak atau abang-abang, kami belum bisa COD..."
- "Saya mau pesan 4 kilo jeruk peras besar, atas nama Rina, WA 0813..., antar ke Jl. ..." → `buat_pesanan`,
  pesanan muncul di `/admin/pesanan` dengan label 🤖 robot dan tercatat di `/admin/aktivitas`.
- "Laporan penjualan hari ini, PIN 1234" → `laporan_penjualan`.

Bridge juga bisa dijalankan terpisah: `.venv/bin/python mcp_bridge.py`.

## Chat pembeli & notifikasi WhatsApp

Pembeli menulis di tombol Chat di web. Tanpa pengaturan apa pun, tombol kirim membuka WhatsApp pembeli dengan pesan
yang sudah terisi ke nomor `WHATSAPP`, jadi pembeli tinggal menekan kirim dan percakapan lanjut di WhatsApp.
Pesannya juga tersimpan di `/admin/chat`.

Supaya pesan masuk tanpa pembeli membuka WhatsApp, pakai gateway [Fonnte](https://fonnte.com):

1. Daftar di Fonnte, sambungkan satu nomor WhatsApp sebagai pengirim, lalu salin token perangkat.
2. Isi `WA_NOTIF_TOKEN` di `.env`. Penerima notifikasi adalah `WA_NOTIF_TUJUAN` (kosong = nomor `WHATSAPP`).
3. Jalankan ulang web.

Satu percakapan mengirim paling banyak satu notifikasi per 5 menit, dan pengingatnya aktif lagi setelah admin membalas.
Gambar QRIS ada di `app/static/img/qris.png`; ganti file itu dengan QRIS asli toko.

## Online (hosting)

Web ini butuh hosting yang **menjalankan proses terus-menerus** (bukan hosting PHP biasa),
karena sambungan ke robot harus selalu hidup. Pilihan:

- **VPS** (misalnya Biznet Gio, IDCloudHost, DigitalOcean): paling fleksibel.
- **Railway / Render / Fly.io**: cukup hubungkan repo GitHub, mereka membaca `Dockerfile`.
  Hindari paket gratis yang "tidur" saat sepi, karena robot akan terputus.

Langkah umum:

1. Siapkan database **MySQL** atau **PostgreSQL** dari hosting, lalu isi
   `DATABASE_URL` (contoh format ada di `.env.example`). Tabel dibuat otomatis saat start.
2. Isi variabel lingkungan di dashboard hosting: `DATABASE_URL`, `NAMA_TOKO`,
   `ADMIN_PASSWORD` (**ganti!**), `SECRET_KEY` (acak), `PIN_LAPORAN`, `MCP_ENDPOINT`.
3. Deploy dengan `Dockerfile` (perintah start: `uvicorn app.main:app --host 0.0.0.0 --port $PORT`).
4. Pakai HTTPS (Railway/Render sudah otomatis; di VPS pakai Nginx + Let's Encrypt).

Jalankan **satu** instance saja: kalau ada dua, keduanya akan menyambung ke MCP endpoint yang sama.

## Laporan langsung dari robot

Firmware (`main/toko_reporter.cc`) mengirim status robot tiap 20 detik dan setiap percakapan ke `POST /api/robot/laporan`.
Alamat dan token ada di `main/toko_lapor_config.h` (tidak ikut git); tokennya harus sama dengan `ROBOT_TOKEN` di `.env`.
Jalankan web dengan `--host 0.0.0.0` supaya bisa dijangkau robot, dan flash ulang kalau alamat web berubah.
