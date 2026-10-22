#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generates the plugin's toolbar/tab icons (Markdown "M with down arrow" mark).

Writes, next to this script:
  icon_light.ico   dark glyph, for light toolbars / panel tab
  icon_dark.ico    light glyph, for Notepad++ dark mode
  toolbar.bmp      16x16 bitmap for the "standard icons" toolbar mode
Pure Python (no Pillow) so it runs anywhere; the outputs are committed anyway.
"""
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
SIZES = [16, 20, 24, 32, 40, 48, 64]
SS = 6  # supersampling per axis


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def round_rect_sdf(px, py, x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = abs(px - cx) - hx, abs(py - cy) - hy
    return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - r


def in_triangle(px, py, a, b, c):
    def sign(p1, p2, p3):
        return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])
    p = (px, py)
    d1, d2, d3 = sign(p, a, b), sign(p, b, c), sign(p, c, a)
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)


# Geometry in a 16x16 design grid.
M_PTS = [(3.2, 10.6), (3.2, 5.4), (5.2, 7.9), (7.2, 5.4), (7.2, 10.6)]
SHAFT = (11.3, 5.2, 11.3, 9.2)
HEAD = ((9.0, 8.2), (13.6, 8.2), (11.3, 11.0))


def covered(x, y):
    if abs(round_rect_sdf(x, y, 0.7, 2.7, 15.3, 13.3, 1.8)) <= 0.6:
        return True
    for (ax, ay), (bx, by) in zip(M_PTS, M_PTS[1:]):
        if seg_dist(x, y, ax, ay, bx, by) <= 0.7:
            return True
    if seg_dist(x, y, *SHAFT) <= 0.7:
        return True
    return in_triangle(x, y, *HEAD)


def render(size, rgb):
    """Returns rows (top to bottom) of (r, g, b, a) tuples."""
    rows = []
    scale = 16.0 / size
    for py in range(size):
        row = []
        for px in range(size):
            hits = 0
            for sy in range(SS):
                for sx in range(SS):
                    x = (px + (sx + 0.5) / SS) * scale
                    y = (py + (sy + 0.5) / SS) * scale
                    hits += covered(x, y)
            row.append((*rgb, round(255 * hits / (SS * SS))))
        rows.append(row)
    return rows


def dib(rows):
    size = len(rows)
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    xor = b''.join(bytes((b, g, r, a)) for row in reversed(rows) for (r, g, b, a) in row)
    mask_stride = ((size + 31) // 32) * 4
    and_mask = bytes(mask_stride * size)
    return header + xor + and_mask


def write_ico(path, rgb):
    images = [dib(render(s, rgb)) for s in SIZES]
    out = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, img in zip(SIZES, images):
        out += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    out += b''.join(images)
    with open(path, 'wb') as f:
        f.write(out)


def write_bmp(path, rgb, bg=(192, 192, 192)):
    rows = render(16, rgb)
    pixels = b''
    for row in reversed(rows):
        for (r, g, b, a) in row:
            k = a / 255
            pixels += bytes(round(c * k + d * (1 - k)) for c, d in ((b, bg[2]), (g, bg[1]), (r, bg[0])))
    info = struct.pack('<IiiHHIIiiII', 40, 16, 16, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    file_header = struct.pack('<2sIHHI', b'BM', 14 + len(info) + len(pixels), 0, 0, 14 + len(info))
    with open(path, 'wb') as f:
        f.write(file_header + info + pixels)


if __name__ == '__main__':
    write_ico(os.path.join(HERE, 'icon_light.ico'), (0x24, 0x29, 0x2f))
    write_ico(os.path.join(HERE, 'icon_dark.ico'), (0xe6, 0xed, 0xf3))
    write_bmp(os.path.join(HERE, 'toolbar.bmp'), (0x24, 0x29, 0x2f))
    print('icons written to', HERE)
