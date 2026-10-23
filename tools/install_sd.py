"""Install the verified runtime package into an explicit SD or emulator SD root.

Only 3ds/crashwarped3ds runtime files are written. Saves in 3ds/crash3 and
all other applications are outside the installation scope.
"""
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PREFIX = "3ds/crashwarped3ds/"


def install(package, sd_root):
    sd_root = Path(sd_root).resolve(strict=True)
    if not sd_root.is_dir():
        raise ValueError("SD root must be an existing directory")
    target = (sd_root / "3ds/crashwarped3ds").resolve()
    if not target.is_relative_to(sd_root):
        raise ValueError("Installation target escapes SD root")
    with zipfile.ZipFile(package) as archive:
        checksums = {}
        for line in archive.read("SHA256SUMS.txt").decode("utf-8").splitlines():
            digest, name = line.split("  ", 1)
            parts = PurePosixPath(name)
            if not name.startswith(PREFIX) or parts.is_absolute() or ".." in parts.parts or "\\" in name or ":" in name:
                raise ValueError("Unexpected installation path: " + name)
            destination = (sd_root / Path(*parts.parts)).resolve()
            if not destination.is_relative_to(target):
                raise ValueError("Destination escapes runtime folder")
            payload = archive.read(name)
            if hashlib.sha256(payload).hexdigest() != digest:
                raise ValueError("Package hash mismatch: " + name)
            checksums[name] = (digest, payload, destination)
        # Validate the entire package before writing any destination.
        for name, (digest, payload, destination) in checksums.items():
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(payload)
            if hashlib.sha256(destination.read_bytes()).hexdigest() != digest:
                raise OSError("Installed file failed verification: " + name)
    return target, len(checksums)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sd-root", required=True, type=Path)
    parser.add_argument("--package", type=Path, default=ROOT / "dist/crashwarped3ds-prototype-sd.zip")
    arguments = parser.parse_args()
    target, count = install(arguments.package, arguments.sd_root)
    print(f"Installed and SHA-256 verified {count} runtime files in {target}")


if __name__ == "__main__":
    main()
