#!/usr/bin/env python3
"""Reader for the XPCK container used by the Chinese patch (lllj_zh.pck).

usage: xpck_unpack.py PCK list [GLOB]
       xpck_unpack.py PCK text NAME            (FEFE-decrypts / decodes a script)
       xpck_unpack.py PCK dump GLOB OUTDIR     (writes matching files to OUTDIR)

Format (little-endian except tags):
  "XPCK" 00 00 00 01 00 00, u32 index offset at byte 10.
  At the index offset: u8 method (2 = zstd), u64 packed size, u64 size, packed index.
  The index is a list of chunks (4-byte tag, u64 length, body). Each "File" chunk holds
  "info" (u32 flags, u32 size, u32 packed size, u16 name bytes, UTF-8 name; names are flat)
  and "segm" (16-byte records: u32 method, u32 absolute offset, u32 size, u32 packed size).
  Files are split into 2 MiB segments, each a zstd frame. Segments may be shared.
The runtime reader is modules/xpck-patch/src/xpck.cpp; this tool is for inspection and tests.
"""
import fnmatch
import os
import struct
import sys
from compression import zstd


def _chunks(b):
    p = 0
    while p < len(b):
        tag = b[p:p + 4]
        (n,) = struct.unpack_from('<Q', b, p + 4)
        yield tag, b[p + 12:p + 12 + n]
        p += 12 + n


class Xpck:
    def __init__(self, path):
        self.f = open(path, 'rb')
        head = self.f.read(14)
        if head[:4] != b'XPCK':
            raise ValueError('not an XPCK file')
        (off,) = struct.unpack_from('<I', head, 10)
        self.f.seek(off)
        method, packed, size = struct.unpack('<BQQ', self.f.read(17))
        idx = self.f.read(packed)
        idx = zstd.decompress(idx) if method == 2 else idx
        if len(idx) != size:
            raise ValueError('bad index size')
        self.entries = {}
        for tag, body in _chunks(idx):
            if tag != b'File':
                continue
            e = {}
            for t, b in _chunks(body):
                if t == b'info':
                    flags, usz, csz, nl = struct.unpack_from('<IIIH', b)
                    e.update(flags=flags, size=usz, csize=csz, name=b[14:14 + nl].decode('utf-8'))
                elif t == b'segm':
                    e['segs'] = [struct.unpack_from('<IIII', b, i) for i in range(0, len(b), 16)]
            self.entries[e['name']] = e

    def find(self, name):
        e = self.entries.get(name)
        if e is None:
            low = name.lower()
            e = next((v for k, v in self.entries.items() if k.lower() == low), None)
        return e

    def read(self, name):
        e = self.find(name)
        if e is None:
            raise KeyError(name)
        out = bytearray()
        for method, off, usz, csz in e['segs']:
            self.f.seek(off)
            d = self.f.read(csz)
            out += zstd.decompress(d) if method == 2 else d
        if len(out) != e['size']:
            raise ValueError('bad size for ' + name)
        return bytes(out)


def krkr_text(d):
    """Decode a script file: FEFE mode 0/1/2, UTF-16 BOM or UTF-8."""
    if d[:2] == b'\xfe\xfe' and d[3:5] == b'\xff\xfe':
        mode = d[2]
        a = struct.unpack('<%dH' % ((len(d) - 5) // 2), d[5:5 + (len(d) - 5) // 2 * 2])
        if mode == 1:
            return ''.join(chr(((c & 0xAAAA) >> 1) | ((c & 0x5555) << 1)) for c in a)
        if mode == 0:
            return ''.join(chr(c ^ (((c & 0xfe) << 8) ^ 1) if c >= 0x20 else c) for c in a)
        raise ValueError('unsupported FEFE mode %d' % mode)
    if d[:2] == b'\xff\xfe':
        return d[2:].decode('utf-16-le')
    return d.decode('utf-8-sig')


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    pck, cmd = Xpck(argv[1]), argv[2]
    if cmd == 'list':
        pat = argv[3] if len(argv) > 3 else '*'
        for n, e in pck.entries.items():
            if fnmatch.fnmatch(n.lower(), pat.lower()):
                print('%10d  %s' % (e['size'], n))
    elif cmd == 'text':
        sys.stdout.write(krkr_text(pck.read(argv[3])))
    elif cmd == 'dump':
        pat, out = argv[3], argv[4]
        os.makedirs(out, exist_ok=True)
        for n in pck.entries:
            if fnmatch.fnmatch(n.lower(), pat.lower()):
                with open(os.path.join(out, n), 'wb') as f:
                    f.write(pck.read(n))
                print(n)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
