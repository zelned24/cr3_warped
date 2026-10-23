"""Package the built SD variant and matching level blob for local deployment."""
import hashlib
from pathlib import Path
import zipfile
import cb3

ROOT = Path(__file__).resolve().parents[1]
README = """Crash Warped 3DS - native prototype

Extract the 3ds directory to the SD root. Launch
3ds/crashwarped3ds/crashwarped3ds.3dsx from Homebrew Launcher.
The SD executable requires the included levels.bin, world and characters.
Keep all directories together. original contains the unchanged NSF/NSD files;
runtime contains lossless GOOL/ZDAT archives and compact behavior catalogs.

Save: sdmc:/3ds/crash3/save.dat; legacy crash3_save.dat remains readable.
The package excludes save files. Preserve existing saves when updating.

Full campaign, GOOL integration, vehicles, bosses and original audio/video
remain incomplete. Physical Old 3DS XL validation remains pending.
See the repository root README.md for the master checklist and test results.
"""


def main():
    files = {
        "3ds/crashwarped3ds/crashwarped3ds.3dsx": ROOT / "crashwarped3ds_sd.3dsx",
        "3ds/crashwarped3ds/crashwarped3ds.smdh": ROOT / "crashwarped3ds_sd.smdh",
        "3ds/crashwarped3ds/levels.bin": ROOT / "game_data/levels.bin",
    }
    for folder in ("world", "characters"):
        for path in sorted((ROOT / "game_data" / folder).glob("*.cwt")):
            files["3ds/crashwarped3ds/" + folder + "/" + path.name] = path
    for level_id in cb3.all_level_ids():
        nsf_path, nsd_path = cb3.level_paths(level_id)
        for source in (Path(nsf_path), Path(nsd_path)):
            files["3ds/crashwarped3ds/original/" + source.name] = source
        runtime = ROOT / "game_data/runtime" / f"lv_{level_id:02X}.cga"
        files["3ds/crashwarped3ds/runtime/" + runtime.name] = runtime
        catalog = runtime.with_suffix(".glc")
        files["3ds/crashwarped3ds/runtime/" + catalog.name] = catalog
    if "3ds/crashwarped3ds/world/lv_0B.cwt" not in files:
        raise SystemExit("Missing original Toad Village texture export")
    for path in files.values():
        if not path.is_file():
            raise SystemExit("Missing build input: " + str(path))
    output = ROOT / "dist"
    output.mkdir(exist_ok=True)
    target = output / "crashwarped3ds-prototype-sd.zip"
    temporary = target.with_suffix(".zip.tmp")
    hashes = []
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, path in files.items():
            data = path.read_bytes()
            archive.writestr(name, data)
            hashes.append(hashlib.sha256(data).hexdigest() + "  " + name)
        archive.writestr("README.txt", README)
        archive.writestr("SHA256SUMS.txt", "\n".join(hashes) + "\n")
    # Read back every compressed member and verify exact payloads before delivery.
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip() is not None:
            raise SystemExit("Archive CRC verification failed")
        for name, path in files.items():
            if archive.read(name) != path.read_bytes():
                raise SystemExit("Archive content mismatch: " + name)
    temporary.replace(target)
    print(str(target))
    print("sha256 " + hashlib.sha256(target.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
