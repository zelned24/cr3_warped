"""Compile real NDSP streaming/ADPCM modules and verify exact original PCM."""
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cb3
import audio_assets
from psx_adpcm import decode_stream


def main():
    rng = random.Random(94244)
    def blocks(count, loop=None, repeat=False):
        output = bytearray()
        for i in range(count):
            flags = (4 if i == loop else 0) | (1 | (2 if repeat else 0) if i == count-1 else 0)
            output += bytes([(i % 5)*16 + i % 13, flags]) + rng.randbytes(14)
        return bytes(output)

    fixtures = [(blocks(1), 7, None, None),
                (blocks(100, loop=2, repeat=True), 3, None, None),
                (blocks(100, repeat=True), 2, 16, None),
                (blocks(1, loop=0, repeat=True), 0xffffffff, None, 8960)]
    archive = cb3.load_level(11, cache=False)
    samples = {e.name: e for e in audio_assets.audio_entries(archive) if e.type == 12}
    real = [samples[name] for name in ("jmp0A", "spn0A", "tnt0A")]
    fixtures += [(entry.items[0], 0, None, None) for entry in real]
    with tempfile.TemporaryDirectory(prefix="crash-audio-stream-") as directory:
        tmp = Path(directory)
        sdk = (ROOT / "tools/tests_audio/3ds.h").read_text()
        sdk = sdk.replace("#define NDSP_WBUF_FREE 0", "#define NDSP_WBUF_FREE 0\n"
            "#define NDSP_WBUF_QUEUED 1\n#define NDSP_WBUF_PLAYING 2\n#define NDSP_WBUF_DONE 3")
        sdk += "\nvoid ndspChnSetPaused(int ch, bool paused);\n"
        (tmp / "3ds.h").write_text(sdk)
        arguments = []
        for index, (raw, repeats, external_loop, prefix) in enumerate(fixtures):
            pcm = decode_stream(raw, repeats=400 if repeats == 0xffffffff else repeats,
                                initial_loop_byte=external_loop).pcm
            if prefix:
                pcm = pcm[:prefix*2]
                if len(pcm) != prefix*2:
                    raise AssertionError("Insufficient loop oracle output")
            source = tmp / (str(index) + ".adpcm")
            expected = tmp / (str(index) + ".pcm")
            source.write_bytes(struct.pack("<II", repeats, 0xffffffff if external_loop is None else external_loop) + raw)
            expected.write_bytes(pcm)
            arguments += [str(source), str(expected)]
        executable = tmp / "stream_test.exe"
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O3", "-UNDEBUG",
            "-Wall", "-Wextra", "-Werror", "-I" + str(tmp), "-I" + str(ROOT / "source"),
            str(ROOT / "source/audio.c"), str(ROOT / "source/audio_stream.c"),
            str(ROOT / "tests/test_audio_stream.c"), "-lm", "-o", str(executable)], check=True)
        subprocess.run([str(executable)] + arguments, check=True)


if __name__ == "__main__":
    main()
