// Customer chat bubble: messages go to /admin/chat. Polls only while a conversation exists.
(function () {
  const $ = (id) => document.getElementById(id);
  const tombol = $("chat-tombol"), panel = $("chat-panel"), isi = $("chat-isi"), form = $("chat-form");
  if (!tombol) return;
  let terakhir = 0, aktif = false, buka = false, belum = 0, jalan = null, namaSaya = "";

  function tambah(p) {
    const div = document.createElement("div");
    div.className = "chat-msg " + p.pengirim;
    div.textContent = p.isi;
    const w = document.createElement("small");
    w.textContent = p.waktu;
    div.appendChild(w);
    isi.appendChild(div);
    terakhir = Math.max(terakhir, p.id);
  }

  function lencana() {
    const b = $("chat-lencana");
    b.textContent = belum; b.hidden = !belum;
  }

  function jadwal() {
    clearTimeout(jalan);
    if (aktif) jalan = setTimeout(ambil, buka ? 4000 : 20000);
  }

  async function ambil() {
    try {
      const r = await fetch("/chat/pesan?setelah=" + terakhir);
      const d = await r.json();
      const pertama = !aktif && !terakhir;
      aktif = d.aktif;
      namaSaya = d.nama || namaSaya;
      d.pesan.forEach((p) => {
        tambah(p);
        if (!buka && !pertama && p.pengirim === "admin") belum += 1;
      });
      if (d.pesan.length) isi.scrollTop = isi.scrollHeight;
      $("chat-identitas").hidden = aktif;
      lencana();
    } catch (e) { /* try again on the next round */ }
    jadwal();
  }

  tombol.addEventListener("click", () => {
    buka = !buka; panel.hidden = !buka;
    if (buka) { belum = 0; lencana(); isi.scrollTop = isi.scrollHeight; $("chat-teks").focus(); }
    jadwal();
  });
  $("chat-tutup").addEventListener("click", () => tombol.click());

  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    const galat = $("chat-galat");
    galat.hidden = true;
    const data = new FormData(form);
    const nama = namaSaya || data.get("nama").trim();
    if (!nama) {
      galat.textContent = "Isi nama dulu ya, kakak-kakak atau abang-abang.";
      galat.hidden = false;
      return;
    }
    const teks = `Halo ${form.dataset.toko}, saya ${nama}.\n${data.get("isi").trim()}`;
    try {
      const r = await fetch("/chat/kirim", { method: "POST", body: data });
      const d = await r.json();
      if (!r.ok) throw new Error(d.detail || "Pesan gagal dikirim");
      $("chat-teks").value = "";
      aktif = true;
      // Without a WhatsApp gateway the buyer can still choose to reach the admin there.
      if (form.dataset.wa) {
        $("chat-wa").href = form.dataset.wa + "?text=" + encodeURIComponent(teks);
        $("chat-wa").hidden = false;
      }
      await ambil();
    } catch (err) {
      galat.textContent = err.message || "Pesan gagal dikirim, coba lagi ya, kakak-kakak atau abang-abang.";
      galat.hidden = false;
    }
  });

  window.bukaChat = () => { if (!buka) tombol.click(); else $("chat-teks").focus(); };

  ambil();
})();
