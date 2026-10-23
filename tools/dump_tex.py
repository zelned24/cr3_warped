"""Dump decoded PSX texture pages of Crash Bandicoot: Warped to PNG.

Two kinds of picture come out of it:

* **page** -- the whole 64 KiB chunk decoded at one depth with one CLUT
  (a diagnostic: see ``docs/assets/TPAG.md`` §2 and §9, a page mixes 4 and
  8 bpp regions and many CLUTs, so only the regions authored with the chosen
  parameters look right);
* **region** -- one UV rectangle, decoded with its own CLUT / texpage.  This
  is the exact picture the runtime will sample.

Usage::

    python tools/dump_tex.py --level 0x0B --page Md1bT --out tools/tex_dump
    python tools/dump_tex.py --level 0x0B --page Cr10T --out tools/tex_dump \
        --uv-clut 0x145 --uv-page 0x62
    python tools/dump_tex.py --level 0x0B --page Md1bT --clut 0x20b \
        --uv-bytes 3f4041023f7f6200007f007f --uv-zoom 4

The UV defaults describe BA10G UV 0 (level 0x0B): bytes
``60 3f 45 01 7f 3f 62 00 7f 20 7f 20`` -> CLUT ``0x145``, texpage ``0x62``,
32x32 region at u 96..127 / v 32..63.  ``--uv-clut``/``--uv-page``/
``--uv-bounds`` override single fields of that record.

No Pillow: the PNG writer is ``dump_rawd.write_png`` (same code path as the
rest of the pipeline).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import cb3
import tpag
from dump_rawd import write_png

#: BA10G (level 0x0B) UV 0, 12 bytes as stored by the TGEO item.
DEFAULT_UV_HEX = "603f45017f3f62007f207f20"


def parse_hex_u16(text):
    return int(text, 0) & 0xFFFF


def parse_bounds(text):
    parts = [int(p, 0) for p in text.replace("x", ",").split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("bounds need min_u,max_u,min_v,max_v")
    if not (0 <= parts[0] <= parts[1] <= 255 and 0 <= parts[2] <= parts[3] <= 255):
        raise argparse.ArgumentTypeError("bounds out of the 0..255 UV range")
    return tuple(parts)


def parse_hex_bytes(text):
    text = text.replace(" ", "").replace(":", "")
    raw = bytes.fromhex(text)
    if len(raw) != 12:
        raise argparse.ArgumentTypeError("a UV record is 12 bytes, got %d" % len(raw))
    return raw


def describe(uv):
    """Human readable decode parameters for one UV dict."""
    cx, cy = tpag.clut_address(uv["clut"])
    w = uv["max_u"] - uv["min_u"] + 1
    h = uv["max_v"] - uv["min_v"] + 1
    return ("clut=0x%04X (vram %d,%d) texPage=0x%04X depth=%dbpp blend=%d "
            "texX=%d slot=%d bounds=u%d..%d v%d..%d (%dx%d)"
            % (uv["clut"], cx, cy, uv["tex_page"], uv["depth"], uv["blend"],
               uv["tex_x"], uv["page_slot"], uv["min_u"], uv["max_u"],
               uv["min_v"], uv["max_v"], w, h))


def scan_level_uvs(archive):
    """Yield ``(entry_name, page_name, uv)`` for every TGEO UV of an archive.

    Throw-away read-only sweep used only for ``--stats``: it reads the two
    counts and the name/UV items of a type-2 (TGEO) entry exactly like
    ``parseTGEO`` (``crashwarped_bin.ts:411``) but builds no geometry.  Full
    TGEO decoding belongs to ``tools/tgeo.py``.
    """
    import struct

    def u32(blob, off):
        return struct.unpack_from("<I", blob, off)[0]

    for entry in archive.entries:
        if entry.type != 2 or not entry.items:
            continue
        head = entry.items[0]
        if len(head) < 0x48:
            continue
        tex_count = u32(head, 0x40)
        uv_count = u32(head, 0x34)
        if not (0 < tex_count <= 8) or not uv_count:
            continue
        names = [cb3.decode_chunk_name(u32(head, 0x0C + 4 * i))
                 for i in range(tex_count)]
        if len(entry.items) < 4:
            continue
        blob = entry.items[3]
        if not blob or len(blob) < uv_count * 12:
            continue
        for i in range(uv_count):
            uv = tpag.parse_uv(blob, i * 12)
            slot = uv["page_slot"]
            page = names[slot] if slot < len(names) else None
            yield entry.name, page, uv


def print_budget(level_ids):
    """Atlas rectangles per (page, texpage, clut) key, without decoding pixels."""
    import collections

    grand = {"keys": 0, "pixels": 0, "regions": 0}
    print("%-6s %5s %7s %9s %12s %12s"
          % ("level", "pages", "atlas", "regions", "RGBA5551 B", "RGBA8 B"))
    for level_id in level_ids:
        try:
            archive = cb3.load_level(level_id)
        except FileNotFoundError:
            continue
        boxes = {}
        regions = 0
        for _name, page, uv in scan_level_uvs(archive):
            regions += 1
            if page is None:
                continue
            key = tpag.make_key(page, uv)
            box = boxes.get(key)
            if box is None:
                boxes[key] = list(uv["bounds"])
            else:
                box[0] = min(box[0], uv["min_u"])
                box[1] = max(box[1], uv["max_u"])
                box[2] = min(box[2], uv["min_v"])
                box[3] = max(box[3], uv["max_v"])
        pixels = sum((b[1] - b[0] + 1) * (b[3] - b[2] + 1)
                     for b in boxes.values())
        print("0x%02X   %5d %7d %9d %12d %12d"
              % (level_id, len(archive.texture_pages), len(boxes), regions,
                 pixels * 2, pixels * 4))
        grand["keys"] += len(boxes)
        grand["pixels"] += pixels
        grand["regions"] += regions
    print("%-6s %5s %7d %9d %12d %12d"
          % ("total", "", grand["keys"], grand["regions"],
             grand["pixels"] * 2, grand["pixels"] * 4))
    return 0


def print_stats(level_ids):
    """Depth / X-base / texpage / CLUT census over the whole disc."""
    import collections

    depths = collections.Counter()
    xbase = collections.defaultdict(collections.Counter)
    texpages = collections.Counter()
    pages_used = collections.Counter()
    pages = 0
    regions = 0
    for level_id in level_ids:
        try:
            archive = cb3.load_level(level_id)
        except FileNotFoundError:
            continue
        pages += len(archive.texture_pages)
        for _name, page, uv in scan_level_uvs(archive):
            regions += 1
            depths[uv["depth"]] += 1
            xbase[uv["depth"]][uv["tex_page"] & 0xF] += 1
            texpages["0x%04X" % uv["tex_page"]] += 1
            pages_used[page] += 1
    print("levels=%d pages=%d uv_records=%d"
          % (len(level_ids), pages, regions))
    print("depth: " + "  ".join("%d bpp=%d" % (d, depths[d])
                                for d in sorted(depths)))
    for depth in sorted(xbase):
        print("  %2d bpp xbase: %s" % (depth, dict(sorted(xbase[depth].items()))))
    print("texpage: " + "  ".join("%s=%d" % (t, n)
                                  for t, n in texpages.most_common(10)))
    print("top pages: " + "  ".join("%s=%d" % (p, n)
                                    for p, n in pages_used.most_common(10)))
    for depth in sorted(depths):
        w, h = tpag.page_dimensions(depth)
        cost = tpag.texture_cost(w, h)
        print("page at %2d bpp: %dx%d  RGBA8=%d B  RGBA5551=%d B"
              % (depth, w, h, cost["rgba8"], cost["rgba5551"]))
    return 0


def report(image, label):
    """Print the numbers the coordinator asks for on every dump."""
    cost = tpag.texture_cost(image.width, image.height)
    hist = image.alpha_histogram()
    print("  %s: %dx%d %dbpp pixels=%d"
          % (label, image.width, image.height, image.depth, cost["pixels"]))
    print("    colors=%d (rgba distinct=%d) alpha=%s"
          % (image.unique_colors(), image.unique_colors(True),
             " ".join("0x%02X:%d" % (a, n) for a, n in sorted(hist.items()))))
    print("    uniform=%s opa=%d xlu=%d transparent=%d"
          % (image.is_uniform(), image.opaque_pixels, image.xlu_pixels,
             image.transparent_pixels))
    print("    cost RGBA8=%d B RGBA5551=%d B"
          % (cost["rgba8"], cost["rgba5551"]))


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Decode Crash Warped type-1 texture pages to PNG.")
    ap.add_argument("--level", default="0x0B", help="level id, e.g. 0x0B")
    ap.add_argument("--page", help="page name, e.g. Cr10T (default: first page)")
    ap.add_argument("--out", default=os.path.join(HERE, "tex_dump"),
                    help="output directory")
    ap.add_argument("--depth", type=int, default=4, choices=(4, 8, 16),
                    help="depth for the full-page picture (default 4)")
    ap.add_argument("--clut", type=lambda s: int(s, 0),
                    help="CLUT for the full-page picture "
                         "(default: --uv-clut, else 0)")
    ap.add_argument("--tex-x", type=lambda s: int(s, 0), default=0,
                    help="X base for the full-page picture (default 0)")
    ap.add_argument("--zoom", type=int, default=1, help="page zoom (default 1)")
    ap.add_argument("--no-page", action="store_true", help="skip the page PNG")
    ap.add_argument("--no-uv", action="store_true", help="skip the region PNG")
    ap.add_argument("--uv-bytes", type=parse_hex_bytes,
                    help="12-byte UV record in hex (default: BA10G UV 0)")
    ap.add_argument("--uv-clut", type=lambda s: int(s, 0),
                    help="override the UV CLUT, e.g. 0x145")
    ap.add_argument("--uv-page", type=lambda s: int(s, 0),
                    help="override the UV texpage word, e.g. 0x62")
    ap.add_argument("--uv-bounds", type=parse_bounds,
                    help="override the region: min_u,max_u,min_v,max_v")
    ap.add_argument("--uv-zoom", type=int, default=8,
                    help="region zoom (default 8)")
    ap.add_argument("--palette", type=lambda s: int(s, 0),
                    help="print the CLUT at this word address (e.g. 0x145)")
    ap.add_argument("--list", action="store_true",
                    help="list the pages of the level and exit")
    ap.add_argument("--stats", action="store_true",
                    help="census of depths/CLUTs/texpages over every level")
    ap.add_argument("--budget", action="store_true",
                    help="atlas rectangle sizes per level (no pixels decoded)")
    ap.add_argument("--all-levels", action="store_true",
                    help="with --stats/--budget: scan the 42 level archives")
    args = ap.parse_args(argv)

    level = int(args.level, 0)
    if args.stats:
        return print_stats(cb3.all_level_ids())
    if args.budget:
        targets = cb3.all_level_ids() if args.all_levels else [level]
        return print_budget(targets)
    arc = cb3.load_level(level)
    pages = arc.texture_pages
    if args.list:
        stats = {}
        for _entry, page_name, uv in scan_level_uvs(arc):
            st = stats.setdefault(page_name, {"n": 0, "depth": {}, "clut": {}})
            st["n"] += 1
            st["depth"][uv["depth"]] = st["depth"].get(uv["depth"], 0) + 1
            st["clut"][uv["clut"]] = st["clut"].get(uv["clut"], 0) + 1
        print("level 0x%02X: %d texture pages" % (level, len(pages)))
        print("  %-6s %-10s %6s %-14s %5s %s"
              % ("page", "cid", "UVs", "depth 4/8/16", "clut", "top clut"))
        for p in pages:
            st = stats.get(p.name, {"n": 0, "depth": {}, "clut": {}})
            top = sorted(st["clut"].items(), key=lambda kv: -kv[1])[:1]
            print("  %-6s 0x%08X %6d  %5d/%5d/%2d %5d  %s"
                  % (p.name, p.cid, st["n"],
                     st["depth"].get(4, 0), st["depth"].get(8, 0),
                     st["depth"].get(16, 0), len(st["clut"]),
                     ("0x%04X (%d)" % (top[0][0], top[0][1])) if top else "-"))
        return 0
    if not pages:
        print("level 0x%02X has no type-1 texture pages" % level, file=sys.stderr)
        return 1
    page = pages[0]
    if args.page:
        page = arc.texture(args.page)
        if page is None:
            print("page %r not in level 0x%02X; available: %s"
                  % (args.page, level, ", ".join(p.name for p in pages)),
                  file=sys.stderr)
            return 1

    print("level 0x%02X page %s (cid 0x%08X, %d B chunk)"
          % (level, page.name, page.cid, len(page.data)))

    # -- palette dump ---------------------------------------------------
    if args.palette is not None:
        cx, cy = tpag.clut_address(args.palette)
        words = tpag.read_palette(page.data, args.palette, args.depth)
        print("CLUT 0x%04X -> VRAM (%d,%d), %d entries:"
              % (args.palette, cx, cy, len(words)))
        for i, raw in enumerate(words):
            r, g, b, a = tpag.unpack_rgba5551(raw)
            print("  [%3d] 0x%04x  rgb(%3d,%3d,%3d) alpha=0x%02X"
                  % (i, raw, r, g, b, a))
        return 0

    os.makedirs(args.out, exist_ok=True)

    # -- full page ------------------------------------------------------
    clut_page = args.clut
    if clut_page is None:
        clut_page = args.uv_clut if args.uv_clut is not None else 0
    if not args.no_page:
        image = tpag.decode_page(page.data, depth=args.depth, clut=clut_page,
                                 tex_x=args.tex_x,
                                 name="%s_%dbpp" % (page.name, args.depth))
        path = os.path.join(args.out, "%s_page_%dbpp_clut%04x.png"
                            % (page.name, args.depth, clut_page))
        write_png(path, image.width, image.height, image.rgba, args.zoom)
        print("  page  %s" % path)
        if args.clut is None:
            print("        (palette 0x%04X inherited/default; a page mixes "
                  "depths and CLUTs -- use --clut or --uv-clut)" % clut_page)
        report(image, "page")

    # -- one UV region --------------------------------------------------
    if args.no_uv:
        return 0
    base = tpag.parse_uv(args.uv_bytes or bytes.fromhex(DEFAULT_UV_HEX))
    clut = args.uv_clut if args.uv_clut is not None else base["clut"]
    tex_page = args.uv_page if args.uv_page is not None else base["tex_page"]
    bounds = args.uv_bounds or base["bounds"]
    uv = tpag.make_uv(clut, tex_page, bounds)
    if not (args.uv_bytes or args.uv_clut is not None or
            args.uv_page is not None or args.uv_bounds):
        print("  (default UV = BA10G UV 0 of level 0x0B)")
    print("  uv    %s" % describe(uv))
    image = tpag.decode_texture(page.data, uv)
    path = os.path.join(args.out, "%s_uv_%04x_%d-%d_%d-%d.png"
                        % (page.name, clut, uv["min_u"], uv["max_u"],
                           uv["min_v"], uv["max_v"]))
    write_png(path, image.width, image.height, image.rgba, args.uv_zoom)
    print("  region %s" % path)
    report(image, "region")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
