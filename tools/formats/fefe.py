#!/usr/bin/env python3
"""Decode krkr "simple crypt" text (FE FE <mode> FF FE ...). modes 0/1/2.
usage: fefe.py FILE            -> prints text
       fefe.py --tree SRC DST  -> decode every FEFE file under SRC into DST (UTF-8)"""
import os, struct, sys, zlib

def decode(b):
    if b[:2] != b'\xfe\xfe' or b[3:5] != b'\xff\xfe':
        if b[:2] == b'\xff\xfe':
            return b[2:].decode('utf-16le', 'replace')
        return b.decode('utf-8', 'replace')
    mode = b[2]
    body = b[5:]
    if mode == 2:
        comp, raw = struct.unpack_from('<qq', body, 0)
        return zlib.decompress(body[16:16 + comp]).decode('utf-16le', 'replace')
    n = len(body) // 2
    chars = struct.unpack('<%dH' % n, body[:2 * n])
    out = []
    for c in chars:
        if mode == 0:
            if c >= 0x20:
                c ^= (((c & 0xfe) << 8) ^ 1)
        elif mode == 1:
            c = ((c & 0xaaaa) >> 1) | ((c & 0x5555) << 1)
        out.append(c)
    return struct.pack('<%dH' % n, *out).decode('utf-16le', 'replace')

if sys.argv[1] == '--tree':
    src, dst = sys.argv[2], sys.argv[3]; k = 0
    for root, _, files in os.walk(src, followlinks=True):
        for f in files:
            p = os.path.join(root, f)
            with open(p, 'rb') as fh: head = fh.read(5)
            if head[:2] == b'\xfe\xfe' and head[3:5] == b'\xff\xfe':
                q = os.path.join(dst, os.path.relpath(p, src))
                os.makedirs(os.path.dirname(q), exist_ok=True)
                open(q, 'w', encoding='utf-8').write(decode(open(p, 'rb').read())); k += 1
    print('decoded', k)
else:
    sys.stdout.write(decode(open(sys.argv[1], 'rb').read()))
