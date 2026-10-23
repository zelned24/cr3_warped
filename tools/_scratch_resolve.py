import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cb3, nsf

def gool(entry):
    items = entry.items
    if len(items) <= 4:
        return None, []
    buf = b''.join(items)
    offs = [0]
    for it in items:
        offs.append(offs[-1] + len(it))
    msig = struct.unpack_from('<I', buf, offs[0] + 0x10)[0]
    objidx = [struct.unpack_from('<h', buf, offs[3] + 2 * msig + 2 * i)[0]
              for i in range((len(items[3]) - 2 * msig) // 2)]
    models = []
    o = offs[5]
    while o < offs[6]:
        t = buf[o]
        if t == 1:
            models.append(('MESH', nsf.eid_str(struct.unpack_from('<I', buf, o + 0xC)[0]),
                           nsf.eid_str(struct.unpack_from('<I', buf, o + 0x10)[0])))
            o += 0x14
        elif t == 2:
            c = struct.unpack_from('<H', buf, o + 2)[0]
            models.append(('SPRITE', nsf.eid_str(struct.unpack_from('<I', buf, o + 4)[0]), c))
            o += 8 + 0x10 * c
        elif t == 5:
            c, r = struct.unpack_from('<HH', buf, o + 2)
            models.append(('QUAD', nsf.eid_str(struct.unpack_from('<I', buf, o + 4)[0]), c * r))
            o += 0xC + 0x18 * c * r
        elif t == 3:
            c = struct.unpack_from('<H', buf, o + 2)[0]
            models.append(('TYPE3', '', ''))
            o += 8 + 0x14 * c
        else:
            models.append(('UNK', '', '')); break
    return objidx, models

for lid in (0x0B, 0x13):
    a = cb3.load_level(lid)
    zdats = a.of_type(7)
    n = a.nsd
    t = 0x520 + 8 * n.num_list_entries
    cls = [nsf.eid_str(struct.unpack_from('<I', n.buf, t + 0x10 + 4 * i)[0]) for i in range(0x80)]
    tables = {}
    for e in a.of_type(11):
        tables[e.name] = gool(e)
    ok = bad = nogool = nomodel = 0
    for z in zdats:
        info = struct.unpack_from('<4I', z.items[0], 0x184)
        for i in range(info[2]):
            buf = z.items[2 + info[1] + i]
            sc = struct.unpack_from('<H', buf, 0xC)[0]
            o = 0x10
            keys = {}
            for _ in range(sc):
                key, rel, fmt, stride, cnt = struct.unpack_from('<HHBBH', buf, o)
                keys[key] = (rel, fmt)
                o += 8
            # crude: read file/id via direct scan of known keys
            def read_scalar(key):
                if key not in keys: return None
                rel, fmt = keys[key]
                base = 0xC + rel
                val = struct.unpack_from('<H', buf, base)[0]
                base = (base + 2 + 3) & ~3
                return struct.unpack_from('<i', buf, base)[0]
            fi = read_scalar(0xA9); oid = read_scalar(0xAA)
            name = cls[fi] if fi is not None and fi < len(cls) else '?'
            if name not in tables or tables[name][0] is None:
                nogool += 1; continue
            objidx, models = tables[name]
            if oid is None or oid >= len(objidx):
                nomodel += 1; continue
            mi = objidx[oid]
            if 0 <= mi < len(models):
                ok += 1
            else:
                bad += 1
    print(hex(lid), 'entities ok', ok, 'bad-index', bad, 'no-gool', nogool, 'id-out', nomodel)
