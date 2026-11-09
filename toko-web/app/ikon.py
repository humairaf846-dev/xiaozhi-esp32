"""Simple line icons used instead of emoji, so the site looks hand-made and stays consistent."""

from markupsafe import Markup

_GARIS = {
    "keranjang": '<path d="M5 9h14l-1.4 9.2a2 2 0 0 1-2 1.8H8.4a2 2 0 0 1-2-1.8L5 9Z"/><path d="M9 9l2.5-5M15 9l-2.5-5M3 9h18"/>',
    "chat": '<path d="M21 12a8 8 0 0 1-11.8 7L4 20.5 5.5 16A8 8 0 1 1 21 12Z"/>',
    "lokasi": '<path d="M12 21s7-6.2 7-11.5A7 7 0 0 0 5 9.5C5 14.8 12 21 12 21Z"/><circle cx="12" cy="9.5" r="2.5"/>',
    "jam": '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3.5 2"/>',
    "kirim": '<path d="M3 7h11v9H3zM14 10h4l3 3v3h-7z"/><circle cx="7" cy="17.5" r="2"/><circle cx="17" cy="17.5" r="2"/>',
    "bayar": '<rect x="3" y="6" width="18" height="13" rx="2"/><path d="M3 10.5h18M7 15h3"/>',
    "qris": '<path d="M4 9V5a1 1 0 0 1 1-1h4M15 4h4a1 1 0 0 1 1 1v4M20 15v4a1 1 0 0 1-1 1h-4M9 20H5a1 1 0 0 1-1-1v-4"/><path d="M8 8h3v3H8zM13 13h3v3h-3zM13 8h3M8 13v3"/>',
    "peta": '<path d="M9 4 3 6.5v13L9 17l6 3 6-2.5v-13L15 7 9 4Z"/><path d="M9 4v13M15 7v13"/>',
    "robot": '<rect x="4" y="8" width="16" height="12" rx="3"/><path d="M12 8V4M9 13h.01M15 13h.01M9.5 16.5h5"/>',
    "web": '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c3 3 3 15 0 18M12 3c-3 3-3 15 0 18"/>',
    "admin": '<circle cx="12" cy="8" r="4"/><path d="M4.5 20c1-4 4-6 7.5-6s6.500 2 7.500 6"/>',
    "telepon": '<rect x="7" y="3" width="10" height="18" rx="2"/><path d="M11 18h2"/>',
    "catatan": '<path d="M6 3h9l4 4v14H6zM9 12h6M9 16h6"/>',
    "kunci": '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>',
    "cek": '<path d="M5 12.500 10 17.500 19 7"/>',
    "salin": '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V6a2 2 0 0 1 2-2h8"/>',
    "unggah": '<path d="M12 16V5M7.500 9.500 12 5l4.500 4.500M4 19h16"/>',
}


def ikon(nama: str) -> Markup:
    return Markup('<svg class="ikon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" '
                  f'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">{_GARIS[nama]}</svg>')


JERUK = Markup('''<svg class="jeruk-logo" viewBox="0 0 64 64" aria-hidden="true">
  <defs><radialGradient id="kulit" cx="38%" cy="32%" r="72%"><stop offset="0" stop-color="#f9b24a"/><stop offset=".6" stop-color="#ee7d1a"/><stop offset="1" stop-color="#c85c10"/></radialGradient></defs>
  <circle cx="32" cy="36" r="24" fill="url(#kulit)"/>
  <path d="M33 13c-1-5 3-9 11-9 0 7-4 10-11 9Z" fill="#3f7d48"/>
  <path d="M32 13v-2" stroke="#143a2c" stroke-width="2.2" stroke-linecap="round"/>
  <ellipse cx="23" cy="26" rx="6" ry="3.500" fill="#fff" opacity=".28" transform="rotate(-30 23 26)"/>
</svg>''')

SENYUM = Markup('''<svg class="jeruk-senyum" viewBox="0 0 120 120" role="img" aria-label="Jeruk tersenyum">
  <defs><radialGradient id="kulit2" cx="37%" cy="30%" r="75%"><stop offset="0" stop-color="#fbbb58"/><stop offset=".58" stop-color="#ef8320"/><stop offset="1" stop-color="#c2570e"/></radialGradient></defs>
  <circle cx="60" cy="66" r="46" fill="url(#kulit2)"/>
  <path d="M62 22c-2-11 7-18 24-18 0 15-9 22-24 18Z" fill="#3f7d48"/>
  <path d="M62 22c6-5 13-9 20-14" stroke="#2c5e36" stroke-width="1.500" fill="none"/>
  <path d="M60 21v-5" stroke="#143a2c" stroke-width="3.500" stroke-linecap="round"/>
  <ellipse cx="41" cy="46" rx="11" ry="6" fill="#fff" opacity=".26" transform="rotate(-32 41 46)"/>
  <path d="M42 62c2-4 7-4 9 0M69 62c2-4 7-4 9 0" stroke="#5a2a08" stroke-width="3.4" fill="none" stroke-linecap="round"/>
  <path d="M46 76c6 10 22 10 28 0" stroke="#5a2a08" stroke-width="3.6" fill="none" stroke-linecap="round"/>
  <circle cx="37" cy="74" r="6" fill="#e85a3a" opacity=".28"/><circle cx="83" cy="74" r="6" fill="#e85a3a" opacity=".28"/>
</svg>''')
