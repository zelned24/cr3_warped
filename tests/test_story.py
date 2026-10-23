"""Compile and execute the real story module in an isolated host directory.

Requires ziglang (python -m pip install ziglang) or --cc /path/to/host-gcc.
The audio callback is supplied by the harness; production story.c is unchanged.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="Host C compiler; defaults to python -m ziglang cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    compiler = [args.cc] if args.cc else [sys.executable, "-m", "ziglang", "cc"]
    with tempfile.TemporaryDirectory(prefix="crash3-story-test-") as folder:
        executable = Path(folder) / "test_story.exe"
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
            '-DSTORY_SAVE_ROOT="."', "-I" + str(root / "source"),
            str(root / "source/story.c"), str(root / "tests/test_story.c"),
            "-o", str(executable)], check=True)
        subprocess.run([str(executable)], cwd=folder, check=True)


if __name__ == "__main__":
    main()
