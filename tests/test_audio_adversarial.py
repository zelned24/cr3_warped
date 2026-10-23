"""Adversarial stress-test harness for Crash Bandicoot 3DS Audio Subsystem (M2).

Runs an extensive, adversarial empirical test against:
1. High-frequency rapid BGM theme switching (11,500 transitions across all 8 themes).
2. Out-of-bounds theme and SFX enum boundary inputs (safe handling, no corruptions).
3. Concurrent stress: 20,000 steps testing SFX channels 0..3 alongside BGM looping on channel 7.
4. 100 consecutive audio_init() / audio_exit() full lifecycles verifying 0 linearAlloc memory leaks.
5. Level BGM mapping table validation across all 64 slots and authentic disc assignments.

Run: python tests/test_audio_adversarial.py [--cc COMPILER...]
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "source"
TESTS = ROOT / "tests"
MOCK_AUDIO = ROOT / "tools" / "tests_audio"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    default_cc = shlex.split(os.environ["CC"]) if os.environ.get("CC") else [sys.executable, "-m", "ziglang", "cc"]
    parser.add_argument("--cc", nargs="+", default=default_cc, help="Host C compiler command")
    args = parser.parse_args()
    compiler = args.cc

    t_start = time.time()
    with tempfile.TemporaryDirectory(prefix="crash3-audio-adversarial-") as tmp:
        tmp_dir = Path(tmp)
        # Create citro3d mock header required by level.c
        (tmp_dir / "citro3d.h").write_text(
            "#define GPU_TRIANGLES 0\n"
            "static inline void C3D_DrawArrays(int p, int f, int n) { (void)p; (void)f; (void)n; }\n"
        )

        exe = tmp_dir / ("audio_adversarial.exe" if os.name == "nt" else "audio_adversarial")

        compile_cmd = compiler + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
            "-DLEVELS_FROM_SD",
            "-I" + str(tmp_dir),
            "-I" + str(MOCK_AUDIO),
            "-I" + str(SOURCE),
            str(SOURCE / "audio.c"),
            str(SOURCE / "level.c"),
            str(TESTS / "test_audio_adversarial.c"),
            "-lm",
            "-o", str(exe)
        ]

        print("Compiling adversarial audio stress test harness...", flush=True)
        subprocess.run(compile_cmd, check=True)

        print("Executing adversarial audio stress test harness...", flush=True)
        subprocess.run([str(exe)], check=True)

    elapsed = time.time() - t_start
    print(f"PASS: Adversarial Audio Suite completed in {elapsed:.3f} seconds.")


if __name__ == "__main__":
    main()
