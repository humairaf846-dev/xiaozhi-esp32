// Live admin updates: polls /admin/api/live and shows new robot/web activity right away.
(function () {
  const IKON = {};  // filled from the icons already on the page
  document.querySelectorAll("[data-ikon]").forEach(el => { IKON[el.dataset.ikon] = el.innerHTML; });
  let terakhir = 0, suaraOn = localStorage.getItem("suara") === "1", audio = null;

  function bunyi() {
    if (!suaraOn) return;
    try {
      audio = audio || new (window.AudioContext || window.webkitAudioContext)();
      const o = audio.createOscillator(), g = audio.createGain();
      o.type = "sine"; o.frequency.setValueAtTime(880, audio.currentTime);
      o.frequency.setValueAtTime(1320, audio.currentTime + 0.12);
      g.gain.setValueAtTime(0.15, audio.currentTime); g.gain.exponentialRampToValueAtTime(0.001, audio.currentTime + 0.35);
      o.connect(g).connect(audio.destination); o.start(); o.stop(audio.currentTime + 0.36);
    } catch (e) { /* sound is optional */ }
  }

  function toast(teks) {
    const t = document.getElementById("toast");
    if (!t) return;
    t.textContent = teks; t.classList.add("show");
    clearTimeout(window._toast); window._toast = setTimeout(() => t.classList.remove("show"), 4000);
  }

  function itemBaru(a) {
    const li = document.createElement("li");
    li.className = a.sumber + " new";
    const dot = document.createElement("span"); dot.className = "dot"; dot.innerHTML = IKON[a.sumber] || "•";  // our own icons, never customer text
    const box = document.createElement("div");
    const meta = document.createElement("div"); meta.className = "meta";
    meta.textContent = `${a.waktu} · ${a.sumber} · ${a.label}`;
    const isi = document.createElement("div"); isi.textContent = a.ringkasan;  // textContent: never HTML
    if (a.pesanan_id) {
      const l = document.createElement("a"); l.href = "/admin/pesanan#p" + a.pesanan_id; l.textContent = " · lihat pesanan";
      isi.appendChild(l);
    }
    box.append(meta, isi);
    if (a.detail) { const d = document.createElement("div"); d.className = "detail"; d.textContent = a.detail; box.appendChild(d); }
    li.append(dot, box);
    return li;
  }

  function set(id, nilai) {
    const el = document.getElementById(id);
    if (el && el.textContent !== String(nilai)) { el.textContent = nilai; el.closest("b")?.classList.remove("pulse"); void el.offsetWidth; el.closest("b")?.classList.add("pulse"); }
  }

  async function tarik(dashboard) {
    try {
      const r = await fetch("/admin/api/live?setelah=" + terakhir, { headers: { Accept: "application/json" } });
      if (r.redirected) return;
      if (!r.ok) throw new Error("web error");
      const d = await r.json();
      const feed = document.getElementById("feed");
      const filter = feed?.dataset.sumber || "";
      const ul = feed?.querySelector(".timeline");
      const baru = d.aktivitas.slice().reverse();  // oldest first, so the newest ends on top
      for (const a of baru) {
        terakhir = Math.max(terakhir, a.id);
        if (filter && a.sumber !== filter) continue;
        if (ul) { ul.prepend(itemBaru(a)); feed.querySelector(".kosong")?.setAttribute("hidden", ""); }
      }
      const dariPelanggan = baru.filter(a => a.sumber !== "admin" && a.jenis !== "jawab_robot");
      if (dariPelanggan.length) {
        const a = dariPelanggan[dariPelanggan.length - 1];
        toast(a.ringkasan);
        bunyi();
      }
      if (dashboard) {
        set("s-omzet", d.stat.omzet); set("s-jumlah", d.stat.jumlah); set("s-kg", d.stat.kg);
        set("s-web", d.stat.web); set("s-robot", d.stat.robot); set("s-robot-akt", d.stat.robot_hari_ini); set("s-robot-dipakai", d.stat.robot_dipakai);
        set("s-omzet-bulan", d.stat.omzet_bulan); set("s-kg-bulan", d.stat.kg_bulan);
        set("jumlah-perlu", d.jumlah_perlu);
        const rs = document.getElementById("robot-status");
        if (rs) { rs.className = "robot-status " + d.robot.kode; rs.querySelector("span").textContent = d.robot.teks; }
        const xs = document.getElementById("xiaozhi-status");
        if (xs) { xs.className = "robot-status " + d.xiaozhi.kode; xs.querySelector("span").textContent = d.xiaozhi.teks; }
        if (d.perlu_html !== null) {
          const p = document.getElementById("perlu");
          // Don't replace the list while the owner is typing a shipping cost.
          if (p && !p.contains(document.activeElement)) p.innerHTML = d.perlu_html;
        }
        if (d.pembeli_html !== null) {
          const b = document.getElementById("pembeli");
          if (b) {
            // Keep whichever buyers were expanded open across the refresh.
            const terbuka = new Set([...b.querySelectorAll("details[open]")].map(el => el.dataset.nama));
            b.innerHTML = d.pembeli_html;
            b.querySelectorAll("details").forEach(el => { if (terbuka.has(el.dataset.nama)) el.open = true; });
          }
        }
      }
    } catch (e) {
      // The web itself is not answering, so the status on screen can no longer be trusted.
      const rs = dashboard && document.getElementById("robot-status");
      if (rs) { rs.className = "robot-status terputus"; rs.querySelector("span").textContent = "Web tidak merespons, status tidak diketahui"; }
    }
  }

  window.mulaiLive = function (idAwal, dashboard) {
    terakhir = idAwal || 0;
    const tombol = document.getElementById("suara");
    const label = () => { if (tombol) tombol.textContent = suaraOn ? "Suara nyala" : "Nyalakan suara"; };
    label();
    tombol?.addEventListener("click", () => { suaraOn = !suaraOn; localStorage.setItem("suara", suaraOn ? "1" : "0"); label(); bunyi(); });
    setInterval(() => tarik(dashboard), 3000);
  };
})();
