"""MCP tools the xiaozhi robot uses to serve customers of the orange shop.

Every call is written to the `aktivitas` table, so the website shows what the robot did.
Runs over stdio; normally started by mcp_bridge.py. Log to stderr only (stdout is MCP).
"""

import logging
import sys
from typing import Literal

from pydantic import BaseModel, Field

from mcp.server.mcpserver import MCPServer

from app import config, services
from app.db import SessionLocal, init_db

logging.basicConfig(stream=sys.stderr, level=logging.INFO)

mcp = MCPServer(
    name="toko-jeruk",
    instructions=(
        f"Alat untuk melayani pelanggan {config.NAMA_TOKO} (jual jeruk manis dan jeruk peras). "
        "Untuk harga dan stok pakai cari_produk, untuk pertanyaan lain tentang toko pakai info_toko, "
        "untuk memesan pakai buat_pesanan. Kalau pelanggan menyebutkan sendiri nomor WhatsApp-nya (misalnya "
        "bilang pernah pesan sebelumnya), pakai cek_pelanggan untuk tahu apakah ini pelanggan lama. "
        "Jawab dengan ramah dan singkat. Sapa pelanggan dengan 'Hallo kakak-kakak dan abang-abang semua'. Panggil pelanggan 'kakak-kakak atau abang-abang', jangan 'kak' atau 'abang' saja."
    ),
)


class JerukDipesan(BaseModel):
    nama_produk: str = Field(description="Jenis dan ukuran jeruk, misalnya 'jeruk manis besar' atau 'jeruk peras sedang'")
    jumlah_kg: int = Field(ge=1, description="Jumlah dalam kilogram, minimal 1")


def _baris(p) -> str:
    stok = f"stok {p.stok} kg" if p.stok > 0 else "stok habis"
    return f"- {p.nama}: {services.rupiah(p.harga)} per kg, {stok}"


@mcp.tool()
def cari_produk(kata_kunci: str = "") -> str:
    """Cek harga dan stok jeruk. Kata kunci contoh: 'manis', 'peras besar', 'manis kecil'.
    Kosongkan untuk melihat semua jeruk."""
    with SessionLocal() as db:
        hasil = services.cari_produk(db, kata_kunci) or services.daftar_produk(db)
        teks = "\n".join(_baris(p) for p in hasil)
        teks += (f"\nMinimal pembelian {config.MIN_KG} kg. Beli {config.DISKON_MIN_KG} kg ke atas diskon "
                 f"{config.DISKON_PERSEN}%.")
        services.catat(db, "robot", "tanya_harga",
                       f"Pelanggan menanyakan harga/stok: {kata_kunci or 'semua jeruk'}", teks)
        return teks


@mcp.tool()
def info_toko(pertanyaan: str) -> str:
    """Jawaban resmi toko untuk pertanyaan pelanggan selain harga, misalnya: cara pesan, ongkir,
    pengiriman luar kota, COD, pembayaran, kesegaran, cara menyimpan, tahan berapa hari, komplain buah rusak,
    beda jeruk manis dan peras, jeruk nipis, tanpa biji, pesan untuk acara, diskon, jam buka, alamat, oleh-oleh.
    Isi dengan pertanyaan pelanggan apa adanya. Sampaikan jawabannya dengan bahasa yang natural."""
    with SessionLocal() as db:
        faq = services.cari_faq(db, pertanyaan)
        if faq:
            teks = faq[0].jawaban  # only the best match, so the robot never mixes in unrelated answers
        else:
            teks = (f"Belum ada jawaban resmi untuk pertanyaan ini. Sarankan pelanggan bertanya lewat WhatsApp "
                    f"{config.WHATSAPP}. Info umum: {config.ALAMAT_TOKO}, {config.JAM_BUKA}.")
        jenis = "tanya_info" if faq else "tanya_tak_terjawab"
        services.catat(db, "robot", jenis, f"Pelanggan bertanya: {pertanyaan}",
                       faq[0].pertanyaan if faq else "Belum ada di FAQ - admin bisa tambahkan jawabannya di /admin/faq")
        return teks


@mcp.tool()
def cek_pelanggan(nomor_whatsapp: str) -> str:
    """Cek apakah nomor WhatsApp ini pernah pesan sebelumnya, supaya robot bisa menyapa pelanggan lama
    secara personal. Robot TIDAK bisa mengenali orang dari suara - panggil alat ini hanya setelah
    pelanggan menyebutkan sendiri nomor WhatsApp-nya, misalnya saat mereka bilang 'halo, aku pernah
    pesan di sini' atau sejenisnya. Jangan memanggil ini untuk pelanggan yang jelas baru pertama kali."""
    with SessionLocal() as db:
        info = services.riwayat_pelanggan(db, nomor_whatsapp)
        if info is None:
            return "Nomor ini belum pernah pesan di toko. Sapa sebagai pelanggan baru."
        return (f"Pelanggan lama: {info['nama']}, sudah {info['jumlah_pesanan']}x pesan di sini. "
                f"Pesanan terakhir ({info['tanggal_terakhir'].strftime('%d/%m/%Y')}): {info['pesanan_terakhir']}. "
                "Sapa dengan hangat dan boleh sebutkan kalau robot 'ingat' pesanan terakhirnya.")


@mcp.tool()
def buat_pesanan(
    nama_pelanggan: str,
    nomor_whatsapp: str,
    jeruk: list[JerukDipesan],
    pengiriman: Literal["antar", "ambil"] = "antar",
    alamat: str = "",
    keperluan: Literal["biasa", "acara", "oleh-oleh"] = "biasa",
    catatan: str = "",
) -> str:
    """Catat pesanan jeruk. Sebelum memanggil, tanyakan dan pastikan ke pelanggan: nama, nomor WhatsApp,
    jenis & ukuran jeruk serta jumlah kg, diantar (wajib alamat, hanya dalam kota) atau ambil di toko.
    Setelah berhasil, sampaikan bahwa total dan ongkir akan dikirim lewat WhatsApp,
    pembayaran lewat transfer atau QRIS (tidak bisa COD)."""
    with SessionLocal() as db:
        item, masalah = [], []
        for j in jeruk:
            cocok = services.cocokkan_produk(db, j.nama_produk)
            if not cocok:
                masalah.append(f"'{j.nama_produk}' tidak ada. Kami hanya jual jeruk manis dan jeruk peras.")
            elif len(cocok) > 1:
                masalah.append(f"'{j.nama_produk}' kurang jelas, pilihannya: {', '.join(p.nama for p in cocok)}. Tanyakan ukurannya.")
            else:
                item.append((cocok[0].id, j.jumlah_kg))
        if not masalah:
            try:
                p = services.buat_pesanan(db, nama_pelanggan, item, nomor_whatsapp, alamat, pengiriman,
                                          keperluan, None, catatan, sumber="robot")
            except services.PesananError as e:
                db.rollback()
                masalah.append(str(e))
        if masalah:
            services.catat(db, "robot", "gagal_pesan", f"Pesanan dari {nama_pelanggan or 'pelanggan'} belum bisa dibuat",
                           " ".join(masalah))
            return "Pesanan BELUM dibuat.\n" + "\n".join(masalah)
        rincian = "\n".join(f"- {it.jumlah} kg {it.nama_produk} = {services.rupiah(it.subtotal)}" for it in p.item)
        diskon = f"\nDiskon {config.DISKON_PERSEN}%: -{services.rupiah(p.diskon)}" if p.diskon else ""
        return (f"Pesanan nomor {p.id} atas nama {p.nama_pelanggan} berhasil dicatat.\n{rincian}{diskon}\n"
                f"Total belanja {services.rupiah(p.subtotal - p.diskon)}, belum termasuk ongkir. "
                f"Total dan ongkir akan dikirim lewat WhatsApp ke {p.telepon}. Bayar lewat transfer atau QRIS.")


@mcp.tool()
def laporan_penjualan(
    periode: Literal["hari_ini", "minggu_ini", "bulan_ini"] = "hari_ini",
    pin: str = "",
) -> str:
    """Laporan penjualan untuk pemilik toko: omzet, kg terjual, jumlah pesanan, jeruk paling laku.
    Hanya untuk pemilik toko: minta PIN laporan dulu, lalu isi parameter pin."""
    with SessionLocal() as db:
        if not config.PIN_LAPORAN:
            return "Laporan belum bisa dibacakan: PIN_LAPORAN belum diatur di file .env toko."
        if pin.strip() != config.PIN_LAPORAN:
            services.catat(db, "robot", "laporan", "Permintaan laporan ditolak (PIN salah)")
            return "PIN laporan salah atau belum disebutkan. Minta pemilik toko menyebutkan PIN laporan."
        mulai, akhir, label = services.rentang_periode(periode)
        d = services.laporan(db, mulai, akhir)
        services.catat(db, "robot", "laporan", f"Laporan penjualan {label} dibacakan ke pemilik toko")
    if d["jumlah_pesanan"] == 0:
        return f"Belum ada penjualan {label}."
    laris = ", ".join(f"{t['nama']} ({t['jumlah']} kg)" for t in d["terlaris"][:3])
    return (f"Penjualan {label}: omzet {services.rupiah(d['omzet'])} dari {d['jumlah_pesanan']} pesanan "
            f"({d['dari_web']} dari web, {d['dari_robot']} dari robot), total {d['total_kg']} kg. Paling laku: {laris}.")


if __name__ == "__main__":
    init_db()
    mcp.run("stdio")
