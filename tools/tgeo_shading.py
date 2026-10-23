"""Native TGEO attribute interpretations selected by the object draw mode.

An item-2 word alone is insufficient to choose RGB versus a packed normal.
SCUS-94244 selects a routine through object byte +0x122, not TGEO flags.
This module decodes inputs; it does not emulate GTE lighting or fog.
"""
import struct

DRAW_ROUTINES = (0x80040AB8, 0x8003FA9C, 0x8003EA9C, 0x8003DA9C,
                 0x8003E8C8, 0x8003FC18, 0x80040A9C, 0x8003CB24)
RGB_MODES = frozenset((0, 1, 2, 6, 7))
NORMAL_MODES = frozenset((3, 4))


class UnresolvedShading(ValueError):
    pass


def object_render_mode(member56):
    """Object +0x122 is byte 2 of the u32 at +0x120 (GOOL member 56)."""
    if not isinstance(member56, int) or not 0 <= member56 <= 0xffffffff:
        raise ValueError("object render word must be u32")
    return (member56 >> 16) & 255


def interpretation(render_mode):
    if render_mode is None:
        return "unresolved_object_render_mode"
    if not isinstance(render_mode, int) or isinstance(render_mode, bool):
        raise ValueError("render mode must be an integer byte or None")
    if render_mode in RGB_MODES:
        return "rgb8"
    if render_mode in NORMAL_MODES:
        return "normal_s8_shift5"
    if render_mode == 5:
        return "constant_debug_color"
    raise UnresolvedShading("unsupported/null native draw mode %r" % render_mode)


def attribute(word):
    """Preserve every bit; normal candidate is exactly s8(component) << 5.

    No normalization, axis swap, color gain or inferred light direction.
    The fourth byte is retained even though these native input paths use RGB.
    """
    if not isinstance(word, int) or not 0 <= word <= 0xffffffff:
        raise ValueError("TGEO attribute must be a complete u32")
    raw = struct.pack("<I", word)
    return {"word": word, "rgb8_candidate": list(raw[:3]),
            "normal_q12_candidate": [value * 32 for value in struct.unpack("<3b", raw[:3])],
            "fourth_byte": raw[3]}


def require_rgb(render_mode):
    """CWT1's RGB-only vertex layout cannot represent packed GTE normals."""
    kind = interpretation(render_mode)
    if kind != "rgb8":
        raise UnresolvedShading("CWT1 RGB export requires an explicit native RGB draw mode; got " + kind)
    return kind
