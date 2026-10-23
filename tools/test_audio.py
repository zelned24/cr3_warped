"""Compile real audio.c against an NDSP ownership/lifecycle test double.

Run: python tools/test_audio.py [--cc COMPILER...]
Requires CC (compiler command) or the installed ziglang Python package.
This validates CPU-side ownership; it does not measure audible output.
"""
import argparse
import os
from pathlib import Path
import random
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOCK = ROOT / "tools" / "tests_audio"

def decoder_harness():
    # Match the independently tested extraction decoder, including reserved
    # headers, clipping, negative predictor rounding, and persistent history.
    from psx_adpcm import History, decode_block

    rng = random.Random(94244)
    vectors = []
    for initial in (History(), History(32767, -32768),
                    History(-32768, 32767), History(-1, 1)):
        history = initial
        for header in range(256):
            data = bytes([header, rng.randrange(256)]) + rng.randbytes(14)
            decoded = decode_block(data, history)
            fields = (data, decoded.samples,
                      (history.recent, history.older),
                      (decoded.history.recent, decoded.history.older))
            vectors.append("{" + ",".join(
                "{" + ",".join(map(str, values)) + "}" for values in fields) + "}")
            history = decoded.history
    return r'''
static void test_adpcm_decoder(void)
{
    static const struct {
        uint8_t data[16]; int16_t pcm[28];
        AudioAdpcmHistory initial, final;
    } vectors[] = {
''' + ",\n".join(vectors) + r'''
    };
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        int16_t pcm[30];
        pcm[0] = 12345; pcm[29] = -12345;
        AudioAdpcmHistory history = vectors[i].initial;
        uint8_t flags = 0;
        assert(audio_decode_adpcm_block(vectors[i].data, 16, pcm + 1, 28,
                                        &history, &flags));
        assert(memcmp(pcm + 1, vectors[i].pcm, sizeof(vectors[i].pcm)) == 0);
        assert(history.recent == vectors[i].final.recent);
        assert(history.older == vectors[i].final.older);
        assert(flags == vectors[i].data[1]);
        assert(pcm[0] == 12345 && pcm[29] == -12345);
    }
    uint8_t data[16] = {0}, flags = 0xa5;
    int16_t pcm[28], untouched[28];
    memset(pcm, 0x55, sizeof(pcm)); memcpy(untouched, pcm, sizeof(pcm));
    AudioAdpcmHistory history = {123, -456};
    assert(!audio_decode_adpcm_block(NULL, 16, pcm, 28, &history, &flags));
    assert(!audio_decode_adpcm_block(data, 15, pcm, 28, &history, &flags));
    assert(!audio_decode_adpcm_block(data, 17, pcm, 28, &history, &flags));
    assert(!audio_decode_adpcm_block(data, 16, NULL, 28, &history, &flags));
    assert(!audio_decode_adpcm_block(data, 16, pcm, 27, &history, &flags));
    assert(!audio_decode_adpcm_block(data, 16, pcm, 28, NULL, &flags));
    assert(!audio_decode_adpcm_block(data, 16, pcm, 28, &history, NULL));
    assert(history.recent == 123 && history.older == -456 && flags == 0xa5);
    assert(memcmp(pcm, untouched, sizeof(pcm)) == 0);
    puts("PASS ADPCM: 1024 differential blocks, clipping, flags, history and bounds");
}
int main(void) { test_adpcm_decoder(); return audio_lifecycle_main(); }
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    default_cc = shlex.split(os.environ["CC"]) if os.environ.get("CC") else [sys.executable, "-m", "ziglang", "cc"]
    parser.add_argument("--cc", nargs="+", default=default_cc, help="Host C compiler command")
    args = parser.parse_args()
    compiler = args.cc
    with tempfile.TemporaryDirectory(prefix="crash3-audio-") as tmp:
        exe = Path(tmp) / ("audio_test.exe" if os.name == "nt" else "audio_test")
        harness = Path(tmp) / "audio_test.c"
        original = (MOCK / "audio_test.c").read_text(encoding="utf-8")
        harness.write_text(original.replace("int main(void)", "int audio_lifecycle_main(void)")
                           + decoder_harness(), encoding="utf-8")
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                       "-I" + str(MOCK), "-I" + str(ROOT / "source"),
                       str(ROOT / "source" / "audio.c"), str(harness),
                       "-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    main()
