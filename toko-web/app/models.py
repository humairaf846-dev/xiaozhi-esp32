from datetime import date, datetime

from sqlalchemy import Boolean, Date, DateTime, ForeignKey, Integer, String, Text
from sqlalchemy.orm import Mapped, mapped_column, relationship

from .db import Base


class Produk(Base):
    __tablename__ = "produk"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    nama: Mapped[str] = mapped_column(String(120), index=True)  # "Jeruk Manis Besar"
    jenis: Mapped[str] = mapped_column(String(60))               # "Jeruk Manis" / "Jeruk Peras"
    ukuran: Mapped[str] = mapped_column(String(30), default="")  # Besar / Sedang / Kecil
    deskripsi: Mapped[str] = mapped_column(Text, default="")
    satuan: Mapped[str] = mapped_column(String(20), default="kg")
    harga: Mapped[int] = mapped_column(Integer)  # Rupiah per satuan
    stok: Mapped[int] = mapped_column(Integer, default=0)
    aktif: Mapped[bool] = mapped_column(Boolean, default=True)


class Pesanan(Base):
    __tablename__ = "pesanan"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    nama_pelanggan: Mapped[str] = mapped_column(String(120))
    telepon: Mapped[str] = mapped_column(String(30), default="")
    alamat: Mapped[str] = mapped_column(Text, default="")
    pengiriman: Mapped[str] = mapped_column(String(20), default="antar")   # antar / ambil
    keperluan: Mapped[str] = mapped_column(String(20), default="biasa")    # biasa / acara / oleh-oleh
    tanggal_butuh: Mapped[date | None] = mapped_column(Date, nullable=True)
    catatan: Mapped[str] = mapped_column(Text, default="")
    sumber: Mapped[str] = mapped_column(String(20), default="web")  # web / robot
    status: Mapped[str] = mapped_column(String(20), default="baru")
    total_kg: Mapped[int] = mapped_column(Integer, default=0)
    subtotal: Mapped[int] = mapped_column(Integer, default=0)
    diskon: Mapped[int] = mapped_column(Integer, default=0)
    ongkir: Mapped[int | None] = mapped_column(Integer, nullable=True)  # set by admin
    estimasi_hari: Mapped[str] = mapped_column(String(40), default="")  # "2-3 hari", editable by admin
    kode_promo: Mapped[str] = mapped_column(String(30), default="")  # promo or referral code used at checkout
    dibuat: Mapped[datetime] = mapped_column(DateTime, default=datetime.now, index=True)

    item: Mapped[list["ItemPesanan"]] = relationship(
        back_populates="pesanan", cascade="all, delete-orphan", lazy="selectin"
    )

    riwayat: Mapped[list["RiwayatPesanan"]] = relationship(
        back_populates="pesanan", cascade="all, delete-orphan", lazy="selectin", order_by="RiwayatPesanan.id"
    )
    pembayaran: Mapped["Pembayaran | None"] = relationship(
        back_populates="pesanan", cascade="all, delete-orphan", lazy="selectin", uselist=False
    )
    ulasan: Mapped["Ulasan | None"] = relationship(
        back_populates="pesanan", cascade="all, delete-orphan", lazy="selectin", uselist=False
    )

    @property
    def total(self) -> int:
        return self.subtotal - self.diskon + (self.ongkir or 0)

    @property
    def bisa_dibayar(self) -> bool:
        """Payable once the total is final: pickup orders, or delivery with the shipping cost set."""
        return self.status in ("baru", "menunggu_bayar") and (self.pengiriman == "ambil" or self.ongkir is not None)

    @property
    def kode_referral(self) -> str:
        """Short code this buyer can share; a new buyer enters it at checkout for a small discount."""
        return f"REF{self.id}"


class RiwayatPesanan(Base):
    """When an order entered each status, shown to the buyer on the order timeline."""

    __tablename__ = "riwayat_pesanan"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    pesanan_id: Mapped[int] = mapped_column(ForeignKey("pesanan.id"), index=True)
    status: Mapped[str] = mapped_column(String(20))
    waktu: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)

    pesanan: Mapped[Pesanan] = relationship(back_populates="riwayat")


class Pembayaran(Base):
    """One per order. A buyer's proof stays 'menunggu' until the admin has seen the money arrive."""

    __tablename__ = "pembayaran"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    pesanan_id: Mapped[int] = mapped_column(ForeignKey("pesanan.id"), unique=True)
    kode: Mapped[str] = mapped_column(String(32), unique=True, index=True)  # secret part of the /bayar link
    metode: Mapped[str] = mapped_column(String(20), default="")   # qris / transfer
    bank: Mapped[str] = mapped_column(String(20), default="")     # which bank, when metode == transfer
    jumlah: Mapped[int] = mapped_column(Integer, default=0)       # total when the proof was sent
    bukti: Mapped[str] = mapped_column(String(80), default="")    # file name in data/bukti
    percakapan_id: Mapped[int | None] = mapped_column(ForeignKey("percakapan.id"), nullable=True)  # buyer's web chat
    status: Mapped[str] = mapped_column(String(20), default="belum")  # belum / menunggu / lunas / ditolak
    dikirim: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)
    diputus: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)

    pesanan: Mapped[Pesanan] = relationship(back_populates="pembayaran")


class ItemPesanan(Base):
    __tablename__ = "item_pesanan"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    pesanan_id: Mapped[int] = mapped_column(ForeignKey("pesanan.id"))
    produk_id: Mapped[int] = mapped_column(ForeignKey("produk.id"))
    nama_produk: Mapped[str] = mapped_column(String(120))
    harga: Mapped[int] = mapped_column(Integer)
    jumlah: Mapped[int] = mapped_column(Integer)  # kg

    pesanan: Mapped[Pesanan] = relationship(back_populates="item")

    @property
    def subtotal(self) -> int:
        return self.harga * self.jumlah


class Faq(Base):
    """Questions and answers the robot uses (from the shop's dataset), editable in /admin/faq."""

    __tablename__ = "faq"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    pertanyaan: Mapped[str] = mapped_column(Text)
    jawaban: Mapped[str] = mapped_column(Text)
    kata_kunci: Mapped[str] = mapped_column(Text, default="")
    aktif: Mapped[bool] = mapped_column(Boolean, default=True)


class Percakapan(Base):
    """One customer's chat from the website, answered in /admin/chat."""

    __tablename__ = "percakapan"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    nama: Mapped[str] = mapped_column(String(60))
    telepon: Mapped[str] = mapped_column(String(30), default="")
    terakhir: Mapped[datetime] = mapped_column(DateTime, default=datetime.now, index=True)
    belum_dibaca: Mapped[int] = mapped_column(Integer, default=0)
    notif_terakhir: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)

    pesan: Mapped[list["PesanChat"]] = relationship(
        back_populates="percakapan", cascade="all, delete-orphan", order_by="PesanChat.id"
    )


class PesanChat(Base):
    __tablename__ = "pesan_chat"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    percakapan_id: Mapped[int] = mapped_column(ForeignKey("percakapan.id"), index=True)
    pengirim: Mapped[str] = mapped_column(String(10))  # pembeli / admin
    isi: Mapped[str] = mapped_column(Text)
    waktu: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)

    percakapan: Mapped[Percakapan] = relationship(back_populates="pesan")


class Aktivitas(Base):
    """Everything the robot and customers do: questions, orders, reports."""

    __tablename__ = "aktivitas"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    waktu: Mapped[datetime] = mapped_column(DateTime, default=datetime.now, index=True)
    sumber: Mapped[str] = mapped_column(String(20))  # robot / web / admin
    jenis: Mapped[str] = mapped_column(String(30))   # tanya_harga / tanya_info / pesanan / ...
    ringkasan: Mapped[str] = mapped_column(String(255))
    detail: Mapped[str] = mapped_column(Text, default="")
    pesanan_id: Mapped[int | None] = mapped_column(Integer, nullable=True)
    pembeli: Mapped[str] = mapped_column(String(120), default="")  # buyer name, when this activity has one


class Ulasan(Base):
    """One review per finished order, shown as testimonials on the homepage once approved."""

    __tablename__ = "ulasan"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    pesanan_id: Mapped[int] = mapped_column(ForeignKey("pesanan.id"), unique=True)
    nama: Mapped[str] = mapped_column(String(120))
    bintang: Mapped[int] = mapped_column(Integer)  # 1-5
    komentar: Mapped[str] = mapped_column(Text, default="")
    tampil: Mapped[bool] = mapped_column(Boolean, default=True)  # admin can hide an inappropriate one
    dibuat: Mapped[datetime] = mapped_column(DateTime, default=datetime.now, index=True)

    pesanan: Mapped[Pesanan] = relationship(back_populates="ulasan")


class KodePromo(Base):
    """Admin-made discount codes, entered by the buyer in the cart at checkout."""

    __tablename__ = "kode_promo"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    kode: Mapped[str] = mapped_column(String(30), unique=True, index=True)
    jenis: Mapped[str] = mapped_column(String(10), default="nominal")  # nominal / persen
    nilai: Mapped[int] = mapped_column(Integer)  # rupiah, or percent 1-100
    kuota: Mapped[int] = mapped_column(Integer, default=0)  # remaining uses; 0 = unlimited... see dipakai below
    tak_terbatas: Mapped[bool] = mapped_column(Boolean, default=False)
    dipakai: Mapped[int] = mapped_column(Integer, default=0)  # times redeemed so far
    kadaluarsa: Mapped[date | None] = mapped_column(Date, nullable=True)
    aktif: Mapped[bool] = mapped_column(Boolean, default=True)
    dibuat: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)
