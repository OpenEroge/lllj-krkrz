#!/usr/bin/env python3
"""Minimal PSB (E-mote / pimg / scn) reader for debugging.
usage: psbdump.py FILE [--tree] [--res]
"""
import struct
import sys
import zlib


class PSB:
    def __init__(self, b):
        if b[:4] == b'mdf\0':
            b = zlib.decompress(b[8:])
        self.b = b
        assert b[:4] == b'PSB\0', b[:4]
        self.version, self.encrypt = struct.unpack_from('<HH', b, 4)
        f = struct.unpack_from('<9I', b, 8)
        (self.o_enc, self.o_names, self.o_strings, self.o_strdata, self.o_choff, self.o_chlen,
         self.o_chdata, self.o_entries, self.checksum) = f
        if self.version >= 4:
            self.o_xoff, self.o_xlen, self.o_xdata = struct.unpack_from('<3I', b, 44)
        self.names = self.load_names()
        self.choff = self.arr(self.o_choff)
        self.chlen = self.arr(self.o_chlen)
        if self.version >= 4:
            self.xoff = self.arr(self.o_xoff)
            self.xlen = self.arr(self.o_xlen)
        stroffs = self.arr(self.o_strings)
        self.strings = []
        for o in stroffs:
            p = self.o_strdata + o
            e = self.b.index(b'\0', p)
            self.strings.append(self.b[p:e].decode('utf-8', 'replace'))

    def uint(self, p, n):
        return int.from_bytes(self.b[p:p + n], 'little') if n else 0

    def arr(self, p):
        return self.arr_at(p)[0]

    def arr_at(self, p):
        t = self.b[p]; p += 1
        cn = t - 0x0c
        cnt = self.uint(p, cn); p += cn
        et = self.b[p]; p += 1
        en = et - 0x0c
        vals = [self.uint(p + i * en, en) for i in range(cnt)]
        return vals, p + cnt * en

    def load_names(self):
        charset, p = self.arr_at(self.o_names)
        data, p = self.arr_at(p)
        idx, p = self.arr_at(p)
        names = []
        for i in idx:
            chr_ = data[i]
            out = bytearray()
            while chr_ != 0:
                code = data[chr_]
                d = charset[code]
                out.append(chr_ - d)
                chr_ = code
            names.append(bytes(reversed(out)).decode('utf-8', 'replace'))
        return names

    def value(self, p, depth=0):
        t = self.b[p]; p += 1
        if t in (0, 1):
            return None
        if t in (2, 3):
            return t == 3
        if 4 <= t <= 0x0c:
            n = t - 4
            v = self.uint(p, n)
            if n and v & (1 << (n * 8 - 1)):
                v -= 1 << (n * 8)
            return v
        if 0x0d <= t <= 0x14:
            return self.arr_at(p - 1)[0]
        if 0x15 <= t <= 0x18:
            return self.strings[self.uint(p, t - 0x14)]
        if 0x19 <= t <= 0x1c:
            return ('RES', self.uint(p, t - 0x18))
        if 0x22 <= t <= 0x25:
            return ('XRES', self.uint(p, t - 0x21))
        if t == 0x1d:
            return 0.0
        if t == 0x1e:
            return struct.unpack_from('<f', self.b, p)[0]
        if t == 0x1f:
            return struct.unpack_from('<d', self.b, p)[0]
        if t == 0x20:
            offs, q = self.arr_at(p)
            return [self.value(q + o, depth + 1) for o in offs]
        if t == 0x21:
            keys, q = self.arr_at(p)
            offs, q = self.arr_at(q)
            return {self.names[k]: self.value(q + o, depth + 1) for k, o in zip(keys, offs)}
        raise ValueError('tag %#x at %d' % (t, p - 1))

    def root(self):
        return self.value(self.o_entries)

    def res(self, i, extra=False):
        if extra:
            o = self.o_xdata + self.xoff[i]
            return self.b[o:o + self.xlen[i]]
        o = self.o_chdata + self.choff[i]
        return self.b[o:o + self.chlen[i]]


def short(v, depth=0, maxd=3):
    if isinstance(v, dict):
        if depth >= maxd:
            return '{...%d}' % len(v)
        return '{' + ', '.join('%s: %s' % (k, short(x, depth + 1, maxd)) for k, x in list(v.items())[:40]) + '}'
    if isinstance(v, list):
        if depth >= maxd:
            return '[...%d]' % len(v)
        return '[' + ', '.join(short(x, depth + 1, maxd) for x in v[:12]) + (', ...' if len(v) > 12 else '') + ']'
    return repr(v)


if __name__ == '__main__':
    p = PSB(open(sys.argv[1], 'rb').read())
    print('version', p.version, 'encrypt', p.encrypt, 'names', len(p.names), 'res', len(p.choff))
    r = p.root()
    if '--tree' in sys.argv:
        print(short(r, 0, 4))
    if '--res' in sys.argv:
        def walk(v, path):
            if isinstance(v, dict):
                for k, x in v.items():
                    walk(x, path + '/' + k)
            elif isinstance(v, list):
                for i, x in enumerate(v):
                    walk(x, path + '[%d]' % i)
            elif isinstance(v, tuple):
                d = p.res(v[1], v[0] == 'XRES')
                print(path, v, len(d), d[:12])
        walk(r, '')
