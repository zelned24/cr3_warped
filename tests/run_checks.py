"""Run host checks against the production modules and the supplied disc assets.

Requires the ziglang Python package for C regressions, plus the original
extracted assets at the location configured by tools/cb3.py.
This does not certify gameplay or performance on a Nintendo 3DS.
"""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    zig = importlib.util.find_spec("ziglang")
    if zig is None or zig.origin is None:
        raise SystemExit("Install the host compiler: python -m pip install ziglang==0.16.0")
    compiler = Path(zig.origin).parent / ("zig.exe" if os.name == "nt" else "zig")
    checks = [
        ["tests/e2e/run_e2e_tests.py"],
        ["tests/test_story.py"],
        ["tests/test_level_sd.py"],
        ["tests/test_ui.py"],
        ["tests/test_frame_lifecycle.py"],
        ["tests/test_entities.py"],
        ["tests/test_motion.py"],
        ["tests/test_all_levels_collision.py", "--cc", str(compiler), "cc"],
        ["tests/test_collision_adversarial.py", "--cc", str(compiler), "cc"],
        ["tests/test_world_graphics.py"],
        ["tests/test_world_runtime.py"],
        ["tests/test_actor_surfaces.py"],
        ["tests/test_object_graphics.py"],
        ["tests/test_initial_graphics.py"],
        ["tools/test_audio.py", "--cc", str(compiler), "cc"],
        ["tools/test_level_loader.py", "--cc", str(compiler), "cc"],
        ["tools/test_geometry_export.py"],
        ["tools/test_scene.py"],
        ["tools/test_svtx.py"],
        ["tools/test_tpag.py"],
    ]
    registered = {arguments[0] for arguments in checks}
    # Include the VM/native-reference, archive, ADPCM and UI regressions as
    # they are added. Each module remains isolated in its own process.
    checks.extend([path.relative_to(ROOT).as_posix()]
                  for path in sorted((ROOT / "tests").glob("test_*.py"))
                  if path.relative_to(ROOT).as_posix() not in registered)
    failed = []
    environment = os.environ.copy()
    environment["PYTHONPATH"] = os.pathsep.join(
        [str(ROOT / "tools"), str(ROOT / "tests")]
        + ([environment["PYTHONPATH"]] if environment.get("PYTHONPATH") else []))
    for arguments in checks:
        print("Running " + arguments[0], flush=True)
        result = subprocess.run([sys.executable] + arguments, cwd=ROOT, env=environment)
        if result.returncode:
            failed.append((arguments[0], result.returncode))
    if failed:
        for path, status in failed:
            print(f"FAILED: {path} (exit {status})", flush=True)
        raise SystemExit(f"{len(failed)} of {len(checks)} registered host checks failed.")
    print(f"All {len(checks)} registered host checks passed. Hardware validation remains separate.")


if __name__ == "__main__":
    main()
