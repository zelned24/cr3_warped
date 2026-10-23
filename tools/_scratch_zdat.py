import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cb3

def parse_db(buf, start):
    series_count = struct.unpack_from('<H', buf, start + 0xC)[0]
    offs = start + 0x10
    out = []
    for _ in range(series_count):
        key, rel, fmt, stride, count = struct.unpack_from('<HHBBH', buf, offs)
        base = start + 0xC + rel
        valtype = fmt & 0x1F
        value_sizes = []
        valoffs = base
        if fmt & 0x40:
            for k in range(count):
                value_sizes.append(struct.unpack_from('<H', buf, valoffs)[0]); valoffs += 2
        else:
            vs = struct.unpack_from('<H', buf, valoffs)[0]; valoffs += 2
            value_sizes = [vs] * count
        times = []
        if fmt & 0x20:
            for k in range(count):
                times.append(struct.unpack_from('<H', buf, valoffs)[0]); valoffs += 2
        valoffs = (valoffs + 3) & ~3
        vals = []
        for k in range(count):
            sub = []
            for s in range(value_sizes[k]):
                if valtype == 1: v = buf[valoffs]
                elif valtype == 0x11: v = struct.unpack_from('<b', buf, valoffs)[0]
                elif valtype == 0x12: v = struct.unpack_from('<h', buf, valoffs)[0]
                elif valtype == 2: v = struct.unpack_from('<H', buf, valoffs)[0]
                elif valtype in (3, 4, 5, 0x13): v = struct.unpack_from('<i', buf, valoffs)[0]
                elif valtype == 6:
                    v = struct.unpack_from('<3h', buf, valoffs)
                elif valtype == 7:
                    v = struct.unpack_from('<3i', buf, valoffs)
                else: v = None
                sub.append(v); valoffs += stride
            vals.append(sub)
        out.append(dict(key=key, fmt=fmt, stride=stride, count=count, size=value_sizes, times=times, vals=vals))
        offs += 8
    return out

a = cb3.load_level(0x0B)
for e in a.of_type(7)[:1]:
    items = e.items
    offs = [0]
    for it in items:
        offs.append(offs[-1] + len(it))
    info = struct.unpack_from('<4I', items[0], 0x184)
    print(e.name, 'info/db/obj/hash', info)
    for i in range(info[1]):
        st = offs[2 + i]
        print(' DB', i, 'start', st, 'first words', struct.unpack_from('<4I', items[2 + i], 0))
        for s in parse_db(items[2 + i], 0):
            print('   key %04x fmt %02x stride %d count %d sizes %s times %s' % (
                s['key'], s['fmt'], s['stride'], s['count'], s['size'][:6], s['times'][:4]))
            print('       ', str(s['vals'])[:160])
