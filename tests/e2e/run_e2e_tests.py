"""Crash Bandicoot 3: Warped (3DS Port) — Unified E2E Test Suite Runner

Discovers, compiles, and executes all 4 E2E testing tiers using the host compiler:
  Tier 1: Feature Coverage (Rendering, Collision, Warp Room, Levels, Audio, Touch UI, Saves)
  Tier 2: Boundary & Corner Cases (Zero/Max, Void Boundaries, Corrupt Saves, Memory Limits)
  Tier 3: Cross-Feature Combinations (Pairwise integration scenarios)
  Tier 4: Real-World Application Scenarios (End-to-End player journeys)

Usage:
  python tests/e2e/run_e2e_tests.py [--tier {1,2,3,4}] [--cc COMPILER]
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
E2E_DIR = ROOT / "tests" / "e2e"
SOURCE_DIR = ROOT / "source"
MOCK_DIR = E2E_DIR / "mock_3ds"


def get_compiler(custom_cc=None):
    if custom_cc:
        return [custom_cc]
    return [sys.executable, "-m", "ziglang", "cc"]


def build_and_run_tier(tier_num, source_file, extra_sources=None, compiler=None):
    if compiler is None:
        compiler = get_compiler()

    if extra_sources is None:
        extra_sources = []

    print(f"\n========================================================")
    print(f" [TIER {tier_num}] Compiling and Executing: {source_file.name}")
    print(f"========================================================")

    with tempfile.TemporaryDirectory(prefix=f"crash3-e2e-tier{tier_num}-") as temp_dir:
        exe_name = f"test_tier{tier_num}.exe" if os.name == "nt" else f"test_tier{tier_num}"
        exe_path = Path(temp_dir) / exe_name

        cmd = compiler + [
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-UNDEBUG",
            "-O2",
            '-DSTORY_SAVE_ROOT="."',
            f"-I{E2E_DIR}",
            f"-I{MOCK_DIR}",
            f"-I{SOURCE_DIR}",
            str(source_file),
        ] + [str(s) for s in extra_sources] + [
            "-lm",
            "-o",
            str(exe_path)
        ]

        t0 = time.time()
        res = subprocess.run(cmd, cwd=temp_dir, capture_output=True, text=True)
        compile_time = time.time() - t0

        if res.returncode != 0:
            print(f"[FAIL] Tier {tier_num} Compilation Failed (in {compile_time:.2f}s):")
            print(res.stderr)
            print(res.stdout)
            return False

        t1 = time.time()
        run_res = subprocess.run([str(exe_path)], cwd=temp_dir, capture_output=True, text=True)
        run_time = time.time() - t1

        print(run_res.stdout)
        if run_res.stderr:
            print(run_res.stderr)

        if run_res.returncode != 0:
            print(f"[FAIL] Tier {tier_num} Execution Failed with exit code {run_res.returncode}")
            return False

        print(f"[PASS] Tier {tier_num} Succeeded (compile: {compile_time:.2f}s, run: {run_time:.2f}s)")
        return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tier", type=int, choices=[1, 2, 3, 4], help="Run a specific tier (1, 2, 3, or 4)")
    parser.add_argument("--cc", help="Custom C compiler executable")
    args = parser.parse_args()

    compiler = get_compiler(args.cc)

    tiers = {
        1: (E2E_DIR / "test_tier1_features.c", [SOURCE_DIR / "story.c", SOURCE_DIR / "bottom_ui.c"]),
        2: (E2E_DIR / "test_tier2_boundaries.c", [SOURCE_DIR / "story.c", SOURCE_DIR / "bottom_ui.c"]),
        3: (E2E_DIR / "test_tier3_combinations.c", [SOURCE_DIR / "story.c", SOURCE_DIR / "bottom_ui.c"]),
        4: (E2E_DIR / "test_tier4_scenarios.c", [SOURCE_DIR / "story.c", SOURCE_DIR / "bottom_ui.c"]),
    }

    selected = [args.tier] if args.tier else [1, 2, 3, 4]

    results = {}
    for t in selected:
        if t in tiers:
            source, extras = tiers[t]
            if not source.exists():
                print(f"[WARN] Tier {t} source file {source} does not exist yet. Skipping.")
                continue
            success = build_and_run_tier(t, source, extras, compiler)
            results[t] = success

    print("\n========================================================")
    print("                 E2E TEST SUITE SUMMARY                 ")
    print("========================================================")
    all_passed = True
    for t in selected:
        status = "PASSED" if results.get(t, False) else "FAILED / NOT RUN"
        if not results.get(t, False):
            all_passed = False
        print(f"  Tier {t}: {status}")

    print("========================================================")
    if all_passed and len(results) == len(selected):
        print("  OVERALL RESULT: ALL SELECTED TIERS PASSED (100% PASS)")
        print("========================================================\n")
        sys.exit(0)
    else:
        print("  OVERALL RESULT: SOME TIERS FAILED OR NOT RUN")
        print("========================================================\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
