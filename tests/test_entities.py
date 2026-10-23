"""Run real props.c and char.c against host regressions (requires ziglang)."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="crash3-entities-") as folder:
        executable = Path(folder) / "entities.exe"
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11",
                        "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
                        "-I" + str(ROOT / "source"), str(ROOT / "tests/test_entities.c"),
                        "-lm", "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
