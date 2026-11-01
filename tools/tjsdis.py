#!/usr/bin/env python3
"""TJS2100 bytecode disassembler (format per krkrz common/tjs2/tjsByteCodeLoader.cpp).

usage: tjsdis.py file.tjs [out.txt]
       tjsdis.py --tree SRC_DIR OUT_DIR   (dump every bytecode .tjs under SRC_DIR)
"""
import os
import struct
import sys

def _acc(x):
    return [x, x + 'PD', x + 'PI', x + 'P']

OPS = ['NOP', 'CONST', 'CP', 'CL', 'CCL', 'TT', 'TF', 'CEQ', 'CDEQ', 'CLT',
       'CGT', 'SETF', 'SETNF', 'LNOT', 'NF', 'JF', 'JNF', 'JMP']
for n in ['INC', 'DEC', 'LOR', 'LAND', 'BOR', 'BXOR', 'BAND', 'SAR', 'SAL', 'SR',
          'ADD', 'SUB', 'MOD', 'DIV', 'IDIV', 'MUL']:
    OPS += _acc(n)
OPS += ['BNOT', 'TYPEOF', 'TYPEOFD', 'TYPEOFI', 'EVAL', 'EEXP', 'CHKINS',
        'ASC', 'CHR', 'NUM', 'CHS', 'INV', 'CHKINV',
        'INT', 'REAL', 'STR', 'OCTET',
        'CALL', 'CALLD', 'CALLI', 'NEW',
        'GPD', 'SPD', 'SPDE', 'SPDEH', 'GPI', 'SPI', 'SPIE',
        'GPDS', 'SPDS', 'GPIS', 'SPIS', 'SETP', 'GETP',
        'DELD', 'DELI', 'SRV', 'RET', 'ENTRY', 'EXTRY', 'THROW',
        'CHGTHIS', 'GLOBAL', 'ADDCI', 'REGMEMBER', 'DEBUGGER', 'CHKIN']
OP = {n: i for i, n in enumerate(OPS)}

SIZE = {}
for n in ['NOP', 'NF', 'RET', 'EXTRY', 'REGMEMBER', 'DEBUGGER']:
    SIZE[n] = 1
for n in ['CONST', 'CP', 'CEQ', 'CDEQ', 'CLT', 'CGT', 'CHKINS', 'CHKIN', 'CCL',
          'SETP', 'GETP', 'ENTRY', 'CHGTHIS', 'ADDCI']:
    SIZE[n] = 3
for n in ['TT', 'TF', 'SETF', 'SETNF', 'LNOT', 'BNOT', 'ASC', 'CHR', 'NUM', 'CHS',
          'CL', 'INV', 'CHKINV', 'TYPEOF', 'EVAL', 'EEXP', 'INT', 'REAL', 'STR',
          'OCTET', 'JF', 'JNF', 'JMP', 'SRV', 'THROW', 'GLOBAL']:
    SIZE[n] = 2
for n in ['LOR', 'LAND', 'BOR', 'BXOR', 'BAND', 'SAR', 'SAL', 'SR', 'ADD', 'SUB',
          'MOD', 'DIV', 'IDIV', 'MUL']:
    SIZE[n], SIZE[n + 'PD'], SIZE[n + 'PI'], SIZE[n + 'P'] = 3, 5, 5, 4
for n in ['INC', 'DEC']:
    SIZE[n], SIZE[n + 'PD'], SIZE[n + 'PI'], SIZE[n + 'P'] = 2, 4, 4, 3
for n in ['GPD', 'GPDS', 'SPD', 'SPDE', 'SPDEH', 'SPDS', 'GPI', 'GPIS', 'SPI',
          'SPIE', 'SPIS', 'DELD', 'TYPEOFD', 'DELI', 'TYPEOFI']:
    SIZE[n] = 4

# operand positions (1-based within instruction) that index the data area
DATA_OPS = {'CONST': [2], 'GPD': [3], 'GPDS': [3], 'SPD': [2], 'SPDE': [2],
            'SPDEH': [2], 'SPDS': [2], 'DELD': [3], 'TYPEOFD': [3], 'CALLD': [3]}
for n in ['INC', 'DEC', 'LOR', 'LAND', 'BOR', 'BXOR', 'BAND', 'SAR', 'SAL', 'SR',
          'ADD', 'SUB', 'MOD', 'DIV', 'IDIV', 'MUL']:
    DATA_OPS[n + 'PD'] = [3]
JUMPS = {'JF', 'JNF', 'JMP'}
CTX = ['TopLevel', 'Function', 'ExprFunction', 'Property', 'PropertySetter',
       'PropertyGetter', 'Class', 'SuperClassGetter']


class Reader:
    def __init__(self, b):
        self.b = b
        self.o = 0

    def i32(self):
        v = struct.unpack_from('<i', self.b, self.o)[0]
        self.o += 4
        return v

    def i16(self):
        v = struct.unpack_from('<h', self.b, self.o)[0]
        self.o += 2
        return v


def load(b):
    if b[:8] != b'TJS2100\x00':
        raise ValueError('not TJS2100 bytecode')
    r = Reader(b)
    r.o = 12
    assert b[12:16] == b'DATA'
    r.o = 20
    n = r.i32()
    bytes_ = list(struct.unpack_from('<%db' % n, b, r.o)) if n else []
    r.o += ((n + 3) >> 2) << 2
    n = r.i32()
    shorts = [r.i16() for _ in range(n)]
    r.o += (n & 1) << 1
    n = r.i32()
    ints = [r.i32() for _ in range(n)]
    n = r.i32()
    longs = list(struct.unpack_from('<%dq' % n, b, r.o)) if n else []
    r.o += 8 * n
    n = r.i32()
    doubles = list(struct.unpack_from('<%dd' % n, b, r.o)) if n else []
    r.o += 8 * n
    n = r.i32()
    strings = []
    for _ in range(n):
        ln = r.i32()
        strings.append(b[r.o:r.o + 2 * ln].decode('utf-16le', 'replace'))
        r.o += 2 * ln + ((ln & 1) << 1)
    n = r.i32()
    octets = []
    for _ in range(n):
        ln = r.i32()
        octets.append(b[r.o:r.o + ln])
        r.o += ((ln + 3) >> 2) << 2
    datasize = struct.unpack_from('<i', b, 16)[0]
    r.o = 12 + datasize
    assert b[r.o:r.o + 4] == b'OBJS', b[r.o:r.o + 4]
    r.o += 8
    toplevel = r.i32()
    objcount = r.i32()
    objs = []
    for _ in range(objcount):
        assert b[r.o:r.o + 4] == b'TJS2'
        r.o += 8
        o = {}
        for k in ['parent', 'name', 'ctx', 'maxvar', 'varreserve', 'maxframe',
                  'argc', 'unnamedbase', 'collapsebase', 'setter', 'getter', 'superget']:
            o[k] = r.i32()
        n = r.i32()
        r.o += 8 * n
        n = r.i32()
        o['code'] = [r.i16() for _ in range(n)]
        r.o += (n & 1) << 1
        n = r.i32()
        data = []
        for _ in range(n):
            t = r.i16()
            idx = r.i16()
            data.append((t, idx))
        o['data'] = data
        n = r.i32()
        r.o += 4 * n
        n = r.i32()
        o['props'] = [(r.i32(), r.i32()) for _ in range(n)]
        objs.append(o)

    def val(t, idx):
        if t == 0:
            return 'void'
        if t == 1:
            return 'null'
        if t in (2, 10):
            return '<obj#%d>' % idx
        if t == 3:
            return repr(strings[idx])
        if t == 4:
            return '<octet %d bytes>' % len(octets[idx])
        if t == 5:
            return repr(doubles[idx])
        if t == 6:
            return str(bytes_[idx])
        if t == 7:
            return str(shorts[idx])
        if t == 8:
            return str(ints[idx])
        if t == 9:
            return str(longs[idx])
        return '?'

    for o in objs:
        o['vals'] = [val(t, i) for t, i in o['data']]
        o['nm'] = strings[o['name']] if 0 <= o['name'] < len(strings) else ''
    return toplevel, objs, strings


def qualname(objs, i):
    parts = []
    while i >= 0:
        parts.append(objs[i]['nm'] or '<anon#%d>' % i)
        i = objs[i]['parent']
    return '.'.join(reversed(parts))


def disasm(o):
    code = o['code']
    vals = o['vals']
    out = []
    i = 0
    while i < len(code):
        c = code[i]
        if c < 0 or c >= len(OPS):
            out.append('%05d  ??? %d' % (i, c))
            i += 1
            continue
        n = OPS[c]
        com = ''
        if n in ('CALL', 'CALLD', 'CALLI', 'NEW'):
            st = 5 if n in ('CALLD', 'CALLI') else 4
            num = code[i + st - 1]
            if num == -1:
                size = st
                args = '...'
            elif num == -2:
                st += 1
                num = code[i + st - 1]
                size = st + num * 2
                a = []
                for j in range(num):
                    t, r = code[i + st + j * 2], code[i + st + j * 2 + 1]
                    a.append('%%%d' % r if t == 0 else ('%%%d*' % r if t == 1 else '*'))
                args = ', '.join(a)
            else:
                size = st + num
                args = ', '.join('%%%d' % code[i + st + k] for k in range(num))
            if n == 'CALLD':
                s = 'calld %%%d, %%%d.%s(%s)' % (code[i + 1], code[i + 2], vals[code[i + 3]], args)
            elif n == 'CALLI':
                s = 'calli %%%d, %%%d.[%%%d](%s)' % (code[i + 1], code[i + 2], code[i + 3], args)
            else:
                s = '%s %%%d, %%%d(%s)' % (n.lower(), code[i + 1], code[i + 2], args)
            out.append('%05d  %s' % (i, s))
            i += size
            continue
        size = SIZE.get(n, 1)
        ops = code[i + 1:i + size]
        parts = []
        for k, v in enumerate(ops, start=1):
            if k in DATA_OPS.get(n, []):
                parts.append('*' + (vals[v] if 0 <= v < len(vals) else '?%d' % v))
            elif n in JUMPS:
                parts.append('-> %05d' % (i + v))
            elif n == 'ENTRY' and k == 1:
                parts.append('catch-> %05d' % (i + v))
            else:
                parts.append('%%%d' % v)
        out.append('%05d  %s %s%s' % (i, n.lower(), ', '.join(parts), com))
        i += size
    return out


def dump(path):
    b = open(path, 'rb').read()
    top, objs, strings = load(b)
    lines = ['// toplevel=%d objects=%d' % (top, len(objs))]
    for idx, o in enumerate(objs):
        lines.append('')
        lines.append('==== #%d %s  [%s] parent=%d args=%d' % (
            idx, qualname(objs, idx), CTX[o['ctx']] if 0 <= o['ctx'] < len(CTX) else o['ctx'],
            o['parent'], o['argc']))
        for pn, po in o['props']:
            lines.append('  prop %s -> #%d' % (strings[pn], po))
        lines += ['  ' + l for l in disasm(o)]
    return '\n'.join(lines) + '\n'


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == '--tree':
        src, dst = sys.argv[2], sys.argv[3]
        ok = bad = 0
        for root, _, files in os.walk(src):
            for f in files:
                p = os.path.join(root, f)
                with open(p, 'rb') as fh:
                    if fh.read(8) != b'TJS2100\x00':
                        continue
                rel = os.path.relpath(p, src)
                q = os.path.join(dst, rel + '.dis')
                os.makedirs(os.path.dirname(q), exist_ok=True)
                try:
                    open(q, 'w').write(dump(p))
                    ok += 1
                except Exception as e:
                    bad += 1
                    print('FAIL', rel, e, file=sys.stderr)
        print('dumped', ok, 'failed', bad)
        return
    s = dump(sys.argv[1])
    if len(sys.argv) > 2:
        open(sys.argv[2], 'w').write(s)
    else:
        sys.stdout.write(s)


if __name__ == '__main__':
    main()
