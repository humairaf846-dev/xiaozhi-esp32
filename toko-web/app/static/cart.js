// Cart kept in the browser (localStorage) until the customer checks out. Quantities are in kg.
function ambilKeranjang() {
  try { return JSON.parse(localStorage.getItem("keranjang")) || {}; } catch (e) { return {}; }
}
function simpanKeranjang(k) { localStorage.setItem("keranjang", JSON.stringify(k)); updateBadge(); }
function updateBadge() {
  const el = document.getElementById("cart-count");
  if (el) el.textContent = Object.values(ambilKeranjang()).reduce((a, b) => a + b, 0) + " kg";
}
function rupiah(n) { return "Rp " + Math.round(n).toLocaleString("id-ID"); }

function tambahKeKeranjang(btn) {
  const input = btn.parentElement.querySelector("input");
  const kg = Math.max(window.TOKO.minKg, parseInt(input && input.value || "1", 10) || 1);
  const k = ambilKeranjang();
  k[btn.dataset.id] = (k[btn.dataset.id] || 0) + kg;
  simpanKeranjang(k);
  const t = document.getElementById("toast");
  if (t) {
    t.textContent = `${kg} kg ${btn.dataset.nama} masuk keranjang`;
    t.classList.add("show");
    clearTimeout(window._toast);
    window._toast = setTimeout(() => t.classList.remove("show"), 1800);
  }
}

async function tampilkanKeranjang() {
  const k = ambilKeranjang();
  const ids = Object.keys(k);
  const kosong = document.getElementById("cart-empty");
  if (!ids.length) { kosong.hidden = false; return; }
  const produk = await (await fetch("/api/produk?ids=" + ids.join(","))).json();
  const body = document.getElementById("cart-body");
  body.innerHTML = "";
  let subtotal = 0, totalKg = 0;
  const ada = {};
  for (const p of produk) {
    ada[p.id] = true;
    const jumlah = Math.min(k[p.id], p.stok);
    k[p.id] = jumlah;
    subtotal += p.harga * jumlah;
    totalKg += jumlah;
    const tr = document.createElement("tr");
    tr.innerHTML = `<td></td><td>${rupiah(p.harga)}</td>
      <td><input type="number" min="0" max="${p.stok}" value="${jumlah}" class="small-input"></td>
      <td>${rupiah(p.harga * jumlah)}</td><td><button class="link" type="button">hapus</button></td>`;
    tr.children[0].textContent = p.nama;  // textContent: names are never treated as HTML
    tr.querySelector("input").addEventListener("change", (e) => {
      const v = Math.max(0, Math.min(p.stok, parseInt(e.target.value || "0", 10)));
      const kk = ambilKeranjang();
      if (v > 0) kk[p.id] = v; else delete kk[p.id];
      simpanKeranjang(kk); tampilkanKeranjang();
    });
    tr.querySelector("button").addEventListener("click", () => {
      const kk = ambilKeranjang(); delete kk[p.id]; simpanKeranjang(kk); tampilkanKeranjang();
    });
    body.appendChild(tr);
  }
  for (const id of ids) { if (!ada[id] || !k[id]) delete k[id]; }
  simpanKeranjang(k);

  const T = window.TOKO;
  const diskon = totalKg >= T.diskonMinKg ? Math.round(subtotal * T.diskonPersen / 100) : 0;
  const kosongSekarang = !Object.keys(k).length;
  kosong.hidden = !kosongSekarang;
  document.getElementById("cart-table").hidden = kosongSekarang;
  document.getElementById("checkout").hidden = kosongSekarang;
  document.getElementById("cart-kg").textContent = totalKg + " kg";
  document.getElementById("row-diskon").hidden = !diskon;
  document.getElementById("cart-diskon").textContent = "− " + rupiah(diskon);
  document.getElementById("cart-total").textContent = rupiah(subtotal - diskon);
  const hint = document.getElementById("hint-diskon");
  const kurang = T.diskonMinKg - totalKg;
  hint.hidden = kosongSekarang || kurang <= 0;
  hint.textContent = `Tambah ${kurang} kg lagi untuk dapat diskon ${T.diskonPersen}%!`;
}

function toggleAlamat() {
  const ambil = document.querySelector("input[name=pengiriman][value=ambil]");
  const wrap = document.getElementById("alamat-wrap");
  if (!ambil || !wrap) return;
  wrap.hidden = ambil.checked;
  wrap.querySelector("textarea").required = !ambil.checked;
}

function pesanLagi(itemJson) {
  const k = ambilKeranjang();
  for (const it of JSON.parse(itemJson)) {
    k[it.id] = (k[it.id] || 0) + it.jumlah;
  }
  simpanKeranjang(k);
  location.href = "/keranjang";
}

function siapkanCheckout() {
  const item = Object.entries(ambilKeranjang()).map(([id, jumlah]) => ({ id: parseInt(id, 10), jumlah }));
  if (!item.length) return false;
  document.getElementById("item_json").value = JSON.stringify(item);
  return true;
}

document.addEventListener("DOMContentLoaded", updateBadge);
