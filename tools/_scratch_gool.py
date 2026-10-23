import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cb3, nsf

def parse_gool(entry):
    items = entry.items
    offs = [0]
    for it in items:
        offs.append(offs[-1] + len(it))
    buf = b''.join(items)
    category = buf[offs[0] + 5]
    stack = struct.unpack_from('<I', buf, offs[0] + 0xC)[0]
    maxsignal = struct.unpack_from('<I', buf, offs[0] + 0x10)[0]
    models = []
    if len(items) > 4:
        start = offs[5]
        end = offs[6]
        o = start
        while o < end:
            t = buf[o]
            key = o - start
            if t == 1:
                models.append((key, 'MESH', nsf.eid_str(struct.unpack_from('<I', buf, o + 0xC)[0]), nsf.eid_str(struct.unpack_from('<I', buf, o + 0x10)[0])))
                o += 0x14
            elif t == 2:
                cnt = struct.unpack_from('<H', buf, o + 2)[0]
                models.append((key, 'SPRITE', nsf.eid_str(struct.unpack_from('<I', buf, o + 4)[0]), cnt))
                o += 8 + 0x10 * cnt
            elif t == 5:
                cnt, rects = struct.unpack_from('<HH', buf, o + 2)
                models.append((key, 'QUAD', nsf.eid_str(struct.unpack_from('<I', buf, o + 4)[0]), cnt * rects))
                o += 0xC + 0x18 * cnt * rects
            elif t == 3:
                cnt = struct.unpack_from('<H', buf, o + 2)[0]
                models.append((key, 'TYPE3', cnt, ''))
                o += 8 + 0x14 * cnt
            else:
                models.append((key, 'UNK%02x' % t, '', ''))
                break
    return category, stack, maxsignal, models, [len(i) for i in items]

a = cb3.load_level(0x0B)
for e in a.of_type(11):
    cat, stack, msig, models, sizes = parse_gool(e)
    print(e.name, 'cat', cat, 'stack', stack, 'maxsig', msig, 'items', sizes)
    print('   models:', models[:30], '...' if len(models) > 30 else '')
