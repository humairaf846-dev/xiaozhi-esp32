"""Initial data from the shop's dataset ("data set benar"). Everything can be edited in /admin."""

from sqlalchemy import func, select
from sqlalchemy.orm import Session

from .models import Faq, Produk

PRODUK = [
    # jenis, ukuran, harga per kg, stok kg, deskripsi
    ("Jeruk Manis", "Besar", 25000, 50, "Manis dan segar, paling enak dimakan langsung."),
    ("Jeruk Manis", "Sedang", 20000, 60, "Manis dan segar, paling enak dimakan langsung."),
    ("Jeruk Manis", "Kecil", 15000, 60, "Manis dan segar, paling enak dimakan langsung."),
    ("Jeruk Peras", "Besar", 20000, 50, "Airnya banyak, cocok untuk dijus."),
    ("Jeruk Peras", "Sedang", 18000, 60, "Airnya banyak, cocok untuk dijus."),
    ("Jeruk Peras", "Kecil", 15000, 60, "Airnya banyak, cocok untuk dijus."),
]

# (pertanyaan, jawaban, kata kunci)
FAQ = [
    ("Halo, jual jeruk apa aja kak?",
     "Halo kakak-kakak atau abang-abang, selamat datang! Kami ada jeruk manis dan jeruk peras. Mau cari jeruk buat dimakan langsung atau buat diperas?",
     "halo jual jeruk apa saja jenis produk"),
    ("Jeruknya manis atau nggak?",
     "Semua jeruk kami rasanya manis dan segar, kakak-kakak atau abang-abang. Cocok buat dimakan langsung maupun dijadikan jus.",
     "manis rasa asam enak"),
    ("Berapa harga jeruk per kilo?",
     "Tergantung jenis dan ukurannya kakak-kakak atau abang-abang. Jeruk peras: besar Rp20.000/kg, sedang Rp18.000/kg, kecil Rp15.000/kg. "
     "Jeruk manis: besar Rp25.000/kg, sedang Rp20.000/kg, kecil Rp15.000/kg. Kalau beli dalam jumlah banyak bisa kami kasih harga diskon.",
     "harga berapa per kilo kg mahal murah"),
    ("Minimal belinya berapa kilo kak?",
     "Minimal pembelian 1 kg, kakak-kakak atau abang-abang. Kalau mau coba dulu, kabari aja ya.",
     "minimal minimum beli kilo sedikit"),
    ("Gimana cara pesannya?",
     "Gampang kakak-kakak atau abang-abang. Bisa pesan lewat web kami, lewat robot ini, atau kirim pesan WhatsApp ke 089527905336. "
     "Sebutkan jenis jeruk, jumlah, dan alamat. Nanti kami konfirmasi total harganya.",
     "cara pesan order beli whatsapp wa"),
    ("Bisa kirim ke luar kota nggak?",
     "Untuk saat ini kami hanya melayani pengiriman dalam kota saja kakak-kakak atau abang-abang. Ongkirnya menyesuaikan tujuan dan berat paket, nanti kami bantu hitungkan.",
     "kirim luar kota antar pengiriman"),
    ("Berapa ongkirnya?",
     "Ongkir tergantung tujuan dan berat pesanan kakak-kakak atau abang-abang. Setelah pesanan masuk, kami hitungkan ongkir yang sesuai dan kabari lewat WhatsApp.",
     "ongkir ongkos kirim biaya antar bukittinggi"),
    ("Bisa COD nggak?",
     "Mohon maaf kakak-kakak atau abang-abang, kami belum bisa COD. Kami hanya melayani pembayaran lewat transfer atau QRIS. Kalau ada kendala, kabari aja ya.",
     "cod bayar di tempat"),
    ("Pembayarannya lewat apa aja?",
     "Kami terima pembayaran lewat transfer bank atau QRIS kakak-kakak atau abang-abang. Kode QRIS ada di menu Pembayaran di web. Pesanan diproses setelah pembayaran dikonfirmasi.",
     "bayar pembayaran transfer bank rekening qris"),
    ("Jeruknya dijamin fresh nggak? Dipetik kapan?",
     "Jeruk kami dikirim dalam kondisi segar kakak-kakak atau abang-abang, karena langsung dipetik dari kebun kami sendiri. Kami juga pilih buah yang bagus sebelum dikemas.",
     "fresh segar petik kebun baru"),
    ("Gimana cara nyimpannya biar awet?",
     "Simpan jeruk di tempat sejuk dan kering, jangan bertumpuk terlalu rapat supaya tidak lembap. Kalau mau lebih awet, simpan di kulkas bagian bawah. "
     "Buang buah yang mulai busuk agar tidak menular ke yang lain.",
     "simpan nyimpan awet tahan kulkas busuk"),
    ("Jeruknya tahan berapa hari?",
     "Di suhu ruang biasanya tahan sekitar 7 hari kakak-kakak atau abang-abang, dan lebih lama kalau disimpan di kulkas. Sebaiknya segera dikonsumsi biar rasanya tetap segar.",
     "tahan berapa hari lama awet"),
    ("Kalau jeruknya ada yang rusak gimana?",
     "Mohon maaf kakak-kakak atau abang-abang kalau ada yang rusak. Kirim foto buah yang bermasalah beserta video unboxing maksimal 2 hari setelah paket diterima, nanti kami bantu cek dan carikan solusinya.",
     "rusak busuk komplain keluhan retur unboxing"),
    ("Bedanya jeruk manis sama jeruk peras apa?",
     "Jeruk manis lebih enak dimakan langsung karena rasanya manis dan segar. Jeruk peras lebih cocok dijus karena airnya banyak. Mau kami rekomendasikan sesuai kebutuhan kakak-kakak atau abang-abang?",
     "beda bedanya perbedaan manis peras jus"),
    ("Jeruk nipis ada nggak?",
     "Mohon maaf kakak-kakak atau abang-abang, kami hanya menjual jeruk manis dan jeruk peras saja.",
     "nipis lemon jeruk lain"),
    ("Bisa pesan buat acara atau usaha minuman?",
     "Bisa banget kakak-kakak atau abang-abang. Untuk acara atau usaha minuman kami bisa siapkan dalam jumlah besar. Sebutkan jumlah dan tanggal butuhnya supaya kami cek stok dan harga khususnya.",
     "acara pesta usaha minuman jus banyak grosir"),
    ("Kalau beli banyak diskonnya berapa?",
     "Ada kakak-kakak atau abang-abang. Untuk pembelian 5 kilo ke atas kami beri diskon 5%. Hubungi kami untuk penawaran terbaiknya.",
     "diskon potongan banyak grosir murah"),
    ("Jam buka tokonya kapan?",
     "Kami melayani pesanan setiap hari mulai jam 7 pagi, kakak-kakak atau abang-abang. Di luar jam itu pesan tetap bisa dikirim dan akan dibalas saat kami buka.",
     "jam buka tutup kapan hari"),
    ("Alamat tokonya di mana?",
     "Kami berlokasi di pasar Pariaman, depan stasiun kereta api. Kalau mau datang langsung, kabari dulu supaya kami pastikan stoknya tersedia ya kakak-kakak atau abang-abang.",
     "alamat lokasi dimana toko pasar pariaman datang"),
    ("Jeruk bagus buat kesehatan nggak?",
     "Jeruk kaya vitamin C dan serat, jadi baik sebagai bagian dari pola makan sehat. Tapi untuk kondisi kesehatan tertentu, sebaiknya tanya dokter atau ahli gizi ya kakak-kakak atau abang-abang.",
     "sehat kesehatan vitamin gizi"),
    ("Bisa buat oleh-oleh?",
     "Bisa kakak-kakak atau abang-abang. Kami bisa kemas rapi dalam kotak khusus supaya cocok jadi oleh-oleh. Mau berapa kilo jadinya?",
     "oleh oleh kado hadiah kotak kemas"),
    ("Stoknya masih ada?",
     "Untuk memastikan stok terbaru, kabari jenis dan jumlah jeruk yang kakak-kakak atau abang-abang mau ya. Kami cek dulu dan segera konfirmasi.",
     "stok ada tersedia habis"),
    ("Ada jeruk tanpa biji?",
     "Mohon maaf kakak-kakak atau abang-abang, untuk sekarang jeruk tanpa biji masih belum ada.",
     "biji tanpa biji seedless"),
    ("Makasih ya kak",
     "Sama-sama kakak-kakak atau abang-abang! Kalau ada yang mau ditanyakan lagi, kabari saja ya!",
     "makasih terima kasih"),
]


def isi_contoh_jika_kosong(db: Session) -> None:
    if not db.scalar(select(func.count(Produk.id))):
        for jenis, ukuran, harga, stok, deskripsi in PRODUK:
            db.add(Produk(nama=f"{jenis} {ukuran}", jenis=jenis, ukuran=ukuran, deskripsi=deskripsi,
                          satuan="kg", harga=harga, stok=stok, aktif=True))
    if not db.scalar(select(func.count(Faq.id))):
        for pertanyaan, jawaban, kunci in FAQ:
            db.add(Faq(pertanyaan=pertanyaan, jawaban=jawaban, kata_kunci=kunci, aktif=True))
    db.commit()
