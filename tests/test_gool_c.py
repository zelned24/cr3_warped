"""Compile and execute the existing GOOL C harnesses against production code.

Assertions stay enabled. The real CGA archive checks use all exported archives
by default; --archive can select particular archives for a focused run.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

from host_native_sources import all_sources


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", action="append", type=Path,
                        help="Real CGA archive to validate; repeat to select several")
    args = parser.parse_args()
    archives = ([path.resolve() for path in args.archive] if args.archive else
                sorted((ROOT / "game_data/runtime").glob("lv_*.cga")))
    if not archives or any(not path.is_file() for path in archives):
        raise SystemExit("GOOL data regressions require readable exported CGA archives.")
    reference = ROOT / "game_data/runtime/lv_0B.cga"
    if not reference.is_file():
        raise SystemExit("Instance and receive regressions require game_data/runtime/lv_0B.cga.")
    compiler = [sys.executable, "-m", "ziglang"]
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
             "-I" + str(ROOT / "source")]
    harnesses = sorted((ROOT / "tests").glob("test_gool_*.c"))
    harnesses.append(ROOT / "tests/test_runtime_archive.c")
    production = [Path(path) for path in all_sources()]
    failed = []
    with tempfile.TemporaryDirectory(prefix="crash3-gool-c-") as folder:
        folder = Path(folder)
        objects = []
        for source in production:
            output = folder / (source.stem + ".o")
            subprocess.run(compiler + ["cc"] + flags + ["-c", str(source),
                           "-o", str(output)], check=True, timeout=120)
            objects.append(str(output))
        library = folder / "libgool_tests.a"
        subprocess.run(compiler + ["ar", "rcs", str(library)] + objects,
                       check=True, timeout=120)
        for harness in harnesses:
            executable = folder / (harness.stem + ".exe")
            print("Running C harness " + harness.name, flush=True)
            try:
                subprocess.run(compiler + ["cc"] + flags + [str(harness),
                               str(library), "-lm", "-o", str(executable)],
                               check=True, timeout=120)
                arguments = []
                if harness.stem in ("test_gool_data", "test_runtime_archive"):
                    arguments = [str(path) for path in archives]
                elif harness.stem in ("test_gool_instance", "test_gool_receive"):
                    arguments = [str(reference)]
                subprocess.run([str(executable)] + arguments, cwd=folder,
                               check=True, timeout=120)
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
                failed.append(harness.name)
                print(f"FAILED {harness.name}: {error}", flush=True)
    if failed:
        raise SystemExit(f"{len(failed)} of {len(harnesses)} C harnesses failed: "
                         + ", ".join(failed))
    print(f"PASS: {len(harnesses)} production GOOL/archive C harnesses; "
          f"{len(archives)} real archives validated")


if __name__ == "__main__":
    main()
