import struct, sys, json

def parse(b):
    p = [0]
    def u8():
        v = b[p[0]]; p[0] += 1; return v
    def u32():
        v = struct.unpack_from('<I', b, p[0])[0]; p[0] += 4; return v
    def key():
        n = u8(); s = b[p[0]:p[0]+n].decode('latin1'); p[0] += n; return s
    def val():
        t = u8(); lo, hi = t & 15, t >> 4
        if lo == 2:
            return hi - 8 if hi else struct.unpack('<i', struct.pack('<I', u32()))[0]
        if lo == 8:
            n = hi - 1 if hi else u32()
            s = b[p[0]:p[0]+n].decode('latin1'); p[0] += n; return s
        if lo == 9:
            n = hi - 1 if hi else u32()
            return [val() for _ in range(n)]
        if lo == 10:
            n = hi - 1 if hi else u32()
            return {key(): val() for _ in range(n)}
        raise ValueError('unknown tag %02x at %d' % (t, p[0] - 1))
    v = val()
    assert p[0] == len(b), (p[0], len(b))
    return v

if __name__ == '__main__':
    print(json.dumps(parse(open(sys.argv[1], 'rb').read()), indent=1))
