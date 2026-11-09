// Admin chat page: polls the conversation list and the open conversation.
(function () {
  const root = document.getElementById("admin-chat");
  if (!root) return;
  const daftar = document.getElementById("chat-daftar"), thread = document.getElementById("chat-thread");
  const form = document.getElementById("chat-balas"), judul = document.getElementById("chat-judul");
  const aksi = document.getElementById("chat-wa");
  let terpilih = Number(root.dataset.id) || 0, terakhir = 0;

  function gambarDaftar(rows) {
    daftar.replaceChildren(...rows.map((c) => {
      const a = document.createElement("a");
      a.href = "/admin/chat?id=" + c.id;
      a.className = "chat-item" + (c.id === terpilih ? " aktif" : "");
      const b = document.createElement("b"); b.textContent = c.nama;
      const s = document.createElement("span"); s.textContent = c.cuplikan;
      const w = document.createElement("small"); w.textContent = c.waktu;
      a.append(b, s, w);
      if (c.belum) { const n = document.createElement("i"); n.textContent = c.belum; a.appendChild(n); }
      return a;
    }));
    if (!rows.length) daftar.textContent = "Belum ada chat dari pembeli.";
    const c = rows.find((x) => x.id === terpilih);
    if (c) {
      judul.textContent = c.nama + (c.telepon ? " · " + c.telepon : "");
      aksi.hidden = !c.wa;
      if (c.wa) aksi.href = c.wa;
    }
  }

  function tambah(p) {
    const div = document.createElement("div");
    div.className = "chat-msg " + (p.pengirim === "admin" ? "pembeli" : "admin");
    div.textContent = p.isi;
    const w = document.createElement("small"); w.textContent = p.waktu;
    div.appendChild(w);
    thread.appendChild(div);
    terakhir = Math.max(terakhir, p.id);
  }

  async function ambil() {
    try {
      const r = await fetch(`/admin/api/chat?id=${terpilih}&setelah=${terakhir}`);
      if (r.redirected) { location.href = "/admin/login"; return; }
      const d = await r.json();
      gambarDaftar(d.daftar);
      d.pesan.forEach(tambah);
      if (d.pesan.length) thread.scrollTop = thread.scrollHeight;
    } catch (e) { /* try again on the next round */ }
  }

  if (form) form.addEventListener("submit", async (e) => {
    e.preventDefault();
    const r = await fetch(`/admin/chat/${terpilih}/balas`, { method: "POST", body: new FormData(form) });
    if (r.ok) { form.reset(); await ambil(); }
  });

  ambil();
  setInterval(ambil, 4000);
})();
