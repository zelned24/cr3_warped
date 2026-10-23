"""Production translation units required by the native GOOL host regressions.

The native bridge modules reference the real ZDAT/model/NSF owners.  Those
owners are production code; only the libctru NSF worker is replaced by the
explicit-failure host stub in ``host_nsf_stream.c``.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

EXTRA_SOURCES = (
    "audio_native.c",
    "audio_voice.c",
    "runtime_archive.c",
    "zdat_native.c",
    "zdat_resources.c",
    "zdat_path_native.c",
    "zdat_lookup.c",
    "zdat_runtime.c",
    "zone_transition.c",
    "model_resources.c",
    "nsf_resource.c",
)


def all_sources():
    """Return gool_*.c plus the native owners and the host NSF backend stub."""
    gool = [str(path) for path in sorted((ROOT / "source").glob("gool_*.c"))]
    extra = [str(ROOT / "source" / name) for name in EXTRA_SOURCES]
    return gool + extra + [str(ROOT / "tests" / "host_nsf_stream.c")]
