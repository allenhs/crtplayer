#!/usr/bin/env python3
"""Writes a Blu-ray picture-subtitle stream (PGS, a .sup file) with the test lines of the jump_* clips:
line N is N white blocks. Usage: make-pgs.py OUT.sup"""
import struct, sys
W, H = 640, 360
CUES = [(2, 6, 1), (10, 20, 2), (25, 28, 3), (30, 45, 4), (50, 55, 5), (60, 110, 6), (112, 118, 7)]
def seg(pts, kind, data):
    return b'PG' + struct.pack('>IIBH', int(pts * 90000), 0, kind, len(data)) + data
def rle(rows):
    out = bytearray()
    for row in rows:
        i = 0
        while i < len(row):
            c = row[i]; n = 1
            while i + n < len(row) and row[i + n] == c and n < 16383: n += 1
            if c == 0:
                out += bytes([0, n]) if n < 64 else bytes([0, 0x40 | (n >> 8), n & 0xff])
            elif n < 3:
                out += bytes([c]) * n
            else:
                out += bytes([0, 0x80 | n, c]) if n < 64 else bytes([0, 0xc0 | (n >> 8), n & 0xff, c])
            i += n
        out += b'\x00\x00'
    return bytes(out)
def show(pts, number, blocks):
    bw, bh, gap = 16, 22, 34
    w, h = blocks * bw + (blocks - 1) * gap, bh
    x, y = (W - w) // 2, H - 60
    rows = [[(1 if (cx % (bw + gap)) < bw else 0) for cx in range(w)] for _ in range(h)]
    data = rle(rows)
    pcs = struct.pack('>HHBHBBBB', W, H, 0x10, number, 0x80, 0, 0, 1) + struct.pack('>HBBHH', 0, 0, 0, x, y)
    wds = bytes([1]) + struct.pack('>BHHHH', 0, x, y, w, h)
    pds = bytes([0, 0]) + bytes([0, 16, 128, 128, 0]) + bytes([1, 235, 128, 128, 255])
    ods = struct.pack('>HBB', 0, 0, 0xc0) + (len(data) + 4).to_bytes(3, 'big') + struct.pack('>HH', w, h) + data
    return seg(pts, 0x16, pcs) + seg(pts, 0x17, wds) + seg(pts, 0x14, pds) + seg(pts, 0x15, ods) + seg(pts, 0x80, b'')
def clear(pts, number):
    pcs = struct.pack('>HHBHBBBB', W, H, 0x10, number, 0, 0, 0, 0)
    return seg(pts, 0x16, pcs) + seg(pts, 0x17, bytes([1]) + struct.pack('>BHHHH', 0, 0, 0, 8, 8)) + seg(pts, 0x80, b'')
with open(sys.argv[1], 'wb') as f:
    n = 0
    for a, b, blocks in CUES:
        f.write(show(a, n, blocks)); n += 1
        f.write(clear(b, n)); n += 1
