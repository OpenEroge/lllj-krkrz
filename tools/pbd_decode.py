#!/usr/bin/env python3
"""Decode TJS/4s0 / TJS/ns0 data packs (.pbd) -- reference for the C++ loader.

Format notes (from lifegpc/msg-tool's analysis, re-implemented here):
  header: "TJS/" "4s0\\0" | u32 seed | u16 crypt | u16 iv_len | iv[iv_len]
  4s0 body: (optional) ChaCha stream xor (key = keyed BLAKE2s-256(key=seed_le, msg=iv),
            nonce = (xxh32(iv, seed), seed)), then framed LZ4: [u16 csize][block]...
            each block may reference up to 64KiB of previous output (dictionary).
  value: u16 (check<<8 | type) ; 0 void, 2 str(u32 len, utf16le), 4 i64, 5 f64,
         0x81 array(u32 n, values), 0xC1 dict(u32 n, (str key, value)*)

usage: pbd_decode.py file.pbd [--json]
       pbd_decode.py --all DIR      (decode every .pbd under DIR, report failures)
"""
import hashlib
import json
import os
import struct
import sys

M32 = 0xffffffff


def rotl(x, n):
    return ((x << n) | (x >> (32 - n))) & M32


def xxh32(data, seed):
    P1, P2, P3, P4, P5 = 2654435761, 2246822519, 3266489917, 668265263, 374761393
    n = len(data)
    i = 0
    if n >= 16:
        v = [(seed + P1 + P2) & M32, (seed + P2) & M32, seed & M32, (seed - P1) & M32]
        while i + 16 <= n:
            for k in range(4):
                lane = struct.unpack_from('<I', data, i + 4 * k)[0]
                v[k] = (rotl((v[k] + lane * P2) & M32, 13) * P1) & M32
            i += 16
        h = (rotl(v[0], 1) + rotl(v[1], 7) + rotl(v[2], 12) + rotl(v[3], 18)) & M32
    else:
        h = (seed + P5) & M32
    h = (h + n) & M32
    while i + 4 <= n:
        h = (rotl((h + struct.unpack_from('<I', data, i)[0] * P3) & M32, 17) * P4) & M32
        i += 4
    while i < n:
        h = (rotl((h + data[i] * P5) & M32, 11) * P1) & M32
        i += 1
    h ^= h >> 15
    h = (h * P2) & M32
    h ^= h >> 13
    h = (h * P3) & M32
    h ^= h >> 16
    return h


def chacha_block(key, nonce_lo, nonce_hi, counter, rounds):
    st = [0x61707865, 0x3320646e, 0x79622d32, 0x6b206574] + list(struct.unpack('<8I', key)) + \
         [counter & M32, (counter >> 32) & M32, nonce_lo, nonce_hi]
    x = st[:]

    def qr(a, b, c, d):
        x[a] = (x[a] + x[b]) & M32; x[d] = rotl(x[d] ^ x[a], 16)
        x[c] = (x[c] + x[d]) & M32; x[b] = rotl(x[b] ^ x[c], 12)
        x[a] = (x[a] + x[b]) & M32; x[d] = rotl(x[d] ^ x[a], 8)
        x[c] = (x[c] + x[d]) & M32; x[b] = rotl(x[b] ^ x[c], 7)
    for _ in range(rounds // 2):
        qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15)
        qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14)
    return [(x[i] + st[i]) & M32 for i in range(16)]


CRYPT = {1: (8, 16), 2: (12, 8), 3: (20, 4), 4: (8, 1), 5: (12, 1), 6: (20, 1)}


def decrypt(body, seed, iv, field):
    rounds, p4 = CRYPT[field]
    key = hashlib.blake2s(iv, key=struct.pack('<I', seed), digest_size=32).digest()
    nlo = xxh32(iv, seed)
    nhi = seed
    fb = nhi ^ nlo
    if fb == 0:
        fb = seed if seed else M32
    out = bytearray(len(body))
    pos = 0
    bc = 0
    while pos < len(body):
        words = chacha_block(key, nlo, nhi, bc, rounds)
        if p4 > 1:
            src = 0
            for _ in range(p4 * 4 - 4):
                for _k in range(4):
                    w = words[src]; src += 1
                    w ^= (w << 13) & M32; w ^= w >> 17; w ^= (w << 5) & M32
                    words.append(w if w else fb)
        ks = struct.pack('<%dI' % len(words), *words)
        n = min(len(ks), len(body) - pos)
        for i in range(n):
            out[pos + i] = body[pos + i] ^ ks[i]
        pos += n
        bc += 1
    return bytes(out)


def lz4_block(src, out):
    """decode one LZ4 block appending to out (bytearray); prior out is the dictionary"""
    i = 0
    n = len(src)
    while i < n:
        tok = src[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255: break
        out += src[i:i + lit]; i += lit
        if i >= n:
            break
        off = src[i] | (src[i + 1] << 8); i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        start = len(out) - off
        if start < 0:
            raise ValueError('lz4 offset out of range')
        for k in range(ml):
            out.append(out[start + k])


def lz4_frames(data):
    out = bytearray()
    pos = 0
    while pos + 2 <= len(data):
        n = struct.unpack_from('<H', data, pos)[0]
        if n == 0 or pos + 2 + n > len(data):
            break
        lz4_block(data[pos + 2:pos + 2 + n], out)
        pos += 2 + n
    return bytes(out)


def parse_value(b, p):
    t = b[p]; p += 2
    if t == 0:
        return None, p
    if t == 2:
        n = struct.unpack_from('<I', b, p)[0]; p += 4
        return b[p:p + 2 * n].decode('utf-16le'), p + 2 * n
    if t == 4:
        return struct.unpack_from('<q', b, p)[0], p + 8
    if t == 5:
        return struct.unpack_from('<d', b, p)[0], p + 8
    if t == 0x81:
        n = struct.unpack_from('<I', b, p)[0]; p += 4
        arr = []
        for _ in range(n):
            v, p = parse_value(b, p); arr.append(v)
        return arr, p
    if t == 0xC1:
        n = struct.unpack_from('<I', b, p)[0]; p += 4
        d = {}
        for _ in range(n):
            kl = struct.unpack_from('<I', b, p)[0]; p += 4
            k = b[p:p + 2 * kl].decode('utf-16le'); p += 2 * kl
            v, p = parse_value(b, p); d[k] = v
        return d, p
    raise ValueError('bad type %#x at %d' % (t, p - 2))


def decode(buf):
    magic, check, seed, crypt, ivlen = struct.unpack_from('<4s4sIHH', buf, 0)
    assert magic == b'TJS/' and check[1:] == b's0\0', (magic, check)
    p = 16
    iv = buf[p:p + ivlen]; p += ivlen
    body = buf[p:]
    if check[0:1] == b'4':
        if crypt:
            body = decrypt(body, seed, iv, crypt)
        body = lz4_frames(body)
    v, _ = parse_value(body, 0)
    return v


def main():
    if sys.argv[1] == '--all':
        ok = bad = 0
        for root, _, files in os.walk(sys.argv[2], followlinks=True):
            for f in files:
                if f.lower().endswith('.pbd'):
                    p = os.path.join(root, f)
                    try:
                        decode(open(p, 'rb').read()); ok += 1
                    except Exception as e:
                        bad += 1; print('FAIL', p, e)
        print('ok', ok, 'fail', bad)
        return
    v = decode(open(sys.argv[1], 'rb').read())
    print(json.dumps(v, ensure_ascii=False, indent=1)[:4000])


if __name__ == '__main__':
    main()
