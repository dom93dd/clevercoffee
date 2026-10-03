"""
Files next to the Orione web page, put there at build time (orione_frontend.py) and served by the
mock ESP32 (test/mock_esp32.py):
- manifest and icons for the home screen app: the orange ring of the favicon and the round display
  on the page's dark background, drawn with smooth edges, no image library needed;
- the page's fonts (fonts/*.woff2): Barlow Semi Condensed Medium and SemiBold as on the round
  display, cut down to German and English (SIL OFL, fonts/OFL.txt).

    files() -> {"manifest.json": bytes, "icon-192.png": bytes, ...}
"""

import json
import math
import os
import struct
import zlib

FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts")  # the round display's font for the page
BACKGROUND = (11, 11, 12)
RING = (255, 146, 38)


def png(size):
    """Ring centered, radius 30 % and stroke 7 % of the size: inside the safe zone of maskable icons"""
    radius, half = size * 0.30, size * 0.035
    rows = []
    for y in range(size):
        row = bytearray([0])  # filter: none
        for x in range(size):
            d = math.hypot(x + 0.5 - size / 2, y + 0.5 - size / 2)
            a = max(0.0, min(1.0, half + 0.5 - abs(d - radius)))  # coverage of the pixel by the ring
            row += bytes(round(b + (r - b) * a) for b, r in zip(BACKGROUND, RING))
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def files():
    manifest = {
        "name": "Orione",
        "short_name": "Orione",
        "start_url": "/",
        "display": "standalone",
        "background_color": "#0b0b0c",
        "theme_color": "#0b0b0c",
        "icons": [
            {"src": "/icon-192.png", "sizes": "192x192", "type": "image/png"},
            {"src": "/icon-512.png", "sizes": "512x512", "type": "image/png", "purpose": "any maskable"},
        ],
    }
    fonts = {}
    for name in sorted(os.listdir(FONTS)):
        if name.endswith(".woff2"):
            with open(os.path.join(FONTS, name), "rb") as f:
                fonts["fonts/" + name] = f.read()
    return {
        **fonts,
        "manifest.json": json.dumps(manifest, separators=(",", ":")).encode(),
        "icon-192.png": png(192),
        "icon-512.png": png(512),
        "apple-touch-icon.png": png(180),  # iOS: "Zum Home-Bildschirm"
    }
