#!/usr/bin/env python3
"""Pack a directory into a plain (unencrypted, uncompressed-data) XP3 archive.

usage: xp3plain.py SRC_DIR OUT.xp3

Files named as a bare 64-hex hash (unrecoverable names / the anti-unpack decoy)
are skipped. Archive member names are the paths relative to SRC_DIR.
"""
import os
import argparse
import json
import re
import struct
import sys
import zlib

HEADER = b'XP3\r\n \n\x1a\x8b\x67\x01'
HASHNAME = re.compile(r'^[0-9A-Fa-f]{64}$')


def chunk(tag, body):
    return tag + struct.pack('<Q', len(body)) + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('src')
    parser.add_argument('out')
    parser.add_argument('--if-stale', action='store_true')
    args = parser.parse_args()
    src, out = args.src, args.out
    files = []
    for root, _, names in os.walk(src, followlinks=True):
        for n in names:
            if HASHNAME.match(n):
                continue
            p = os.path.join(root, n)
            files.append((os.path.relpath(p, src).replace(os.sep, '/'), p))
    files.sort()
    fingerprint = json.dumps({
        'source': os.path.realpath(src),
        'files': [(name, os.stat(path).st_size, os.stat(path).st_mtime_ns) for name, path in files],
    }, ensure_ascii=False, sort_keys=True)
    stamp = out + '.source.json'
    if args.if_stale and os.path.isfile(out) and os.path.isfile(stamp):
        with open(stamp, encoding='utf-8') as f:
            if f.read() == fingerprint:
                print('up to date:', out)
                return
    index = bytearray()
    tmp = out + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(HEADER + struct.pack('<Q', 0))
        for name, path in files:
            off = f.tell()
            adler = 1
            size = 0
            with open(path, 'rb') as fh:
                while True:
                    b = fh.read(1 << 22)
                    if not b:
                        break
                    adler = zlib.adler32(b, adler)
                    size += len(b)
                    f.write(b)
            nm = name.encode('utf-16le')
            info = struct.pack('<IQQH', 0, size, size, len(nm) // 2) + nm
            segm = struct.pack('<IQQQ', 0, off, size, size)
            body = chunk(b'info', info) + chunk(b'segm', segm) + chunk(b'adlr', struct.pack('<I', adler & 0xffffffff))
            index += chunk(b'File', body)
        idx_off = f.tell()
        comp = zlib.compress(bytes(index), 9)
        f.write(b'\x01' + struct.pack('<QQ', len(comp), len(index)) + comp)
        f.seek(len(HEADER))
        f.write(struct.pack('<Q', idx_off))
    os.replace(tmp, out)
    with open(stamp + '.tmp', 'w', encoding='utf-8') as f:
        f.write(fingerprint)
    os.replace(stamp + '.tmp', stamp)
    print('packed', len(files), 'files ->', out)


if __name__ == '__main__':
    main()
