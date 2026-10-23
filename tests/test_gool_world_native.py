"""Replay original world timing and object UPDATE admission, not a scheduler.

The remaining tree mutation, activation, physics and rendering services must be
established before these contracts can drive a production world scheduler.
"""
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cb3
from psx_native import Image, ORIGINAL_SHA256
from test_gool_stack_native import replay


class NativeWorld(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = Image.parse((Path(cb3.ISO) / "SCUS_942.44").read_bytes())
        if cls.image.sha256 != ORIGINAL_SHA256:
            raise AssertionError("wrong original executable")
        cls.folder = tempfile.TemporaryDirectory(prefix="crash3-world-native-")
        cls.addClassCleanup(cls.folder.cleanup)
        folder = Path(cls.folder.name)
        source = folder / "probe.c"
        source.write_text(r'''
#include <inttypes.h>
#include <stdio.h>
#include "gool_world.h"
int main(void) {
    gool_world_input input;
    int count;
    while ((count=scanf("%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
                       " %" SCNu32 " %" SCNu32,
            &input.counter,&input.world_flags,&input.requested_update,
            &input.category,&input.status_100,&input.flags_ac))==6) {
        gool_world_decision result=gool_world_admit(input);
        printf("%" PRIu32 " %" PRIu32 " %d\n",result.tick,
               result.effective_request,result.run_update);
    }
    return count==EOF?0:1;
}
''')
        cls.executable = folder / "probe.exe"
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11",
                        "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
                        "-I" + str(ROOT / "source"), str(source),
                        str(ROOT / "source/gool_world.c"), "-o", str(cls.executable)],
                       check=True)

    def production(self, samples):
        text = "".join(" ".join(map(str, row)) + "\n" for row in samples)
        result = subprocess.run([str(self.executable)], input=text, text=True,
                                capture_output=True, check=True)
        values = [tuple(map(int, row.split())) for row in result.stdout.splitlines()]
        self.assertEqual(len(values), len(samples))
        return values

    def test_logic_tick_quantization_matches_original(self):
        timer = 0x80090000
        rng = random.Random(0x1D16C)
        samples = [0, 1, 33, 34, 35, 67, 68, 0xFFFFFFFF]
        samples += [rng.randrange(1 << 32) for _ in range(1000)]
        production = self.production([(sample, 0, 0, 0, 0, 0) for sample in samples])
        for elapsed, actual in zip(samples, production):
            memory = {0x80061A80: timer, timer + 0x10: elapsed}
            replay(self.image, 0x8001D16C, {29: 0x801F0000}, memory,
                   stops=(0x8001D1B0,), hooks={0x8002D8C8: lambda regs, ram: None})
            self.assertEqual(memory[0x80068EEC], elapsed // 34)
            self.assertEqual(actual[0], memory[0x80068EEC])

    def test_object_update_admission_matches_original(self):
        obj, behavior, header = 0x80070000, 0x80090000, 0x800A0000
        masks = {0x100: 0x20, 0x200: 0x100, 0x300: 0x80,
                 0x400: 0x400, 0x500: 0x80, 0x600: 0x80, 0x900: 0x400}
        rng = random.Random(0x1D674)
        samples, expected_results = [], []
        for _ in range(4000):
            category = rng.choice([*masks, 0, 0x700, 0xA00])
            world = rng.getrandbits(32)
            flags = rng.getrandbits(32)
            motion = rng.getrandbits(32)
            requested = rng.randrange(2)
            allowed = world & 8
            effective = requested
            if allowed:
                effective |= bool(flags & 0x40000)
                if (motion & 0x02000000 or flags & 0x8000) and world & 0x8000:
                    allowed = 1
                elif category in masks:
                    allowed = world & masks[category]
            expected = bool(effective and allowed)
            memory = {0x80068F1C: world, obj + 0x100: flags,
                      obj + 0xAC: motion, obj + 0xC: behavior,
                      behavior + 0x10: header, header + 4: category,
                      0x80068EEC: 1234, obj + 0xDC: 5678}
            replay(self.image, 0x8001D6A0, {16: obj, 17: requested}, memory,
                   stops=(0x8001D7FC, 0x8001D82C, 0x8001D904))
            self.assertEqual(memory[0x80068EE8], obj)
            self.assertEqual(memory[obj + 0xDC], 1234 if expected else 5678)
            samples.append((1234 * 34, world, requested, category, flags, motion))
            expected_results.append((1234, effective, int(expected)))
        self.assertEqual(self.production(samples), expected_results)


if __name__ == "__main__":
    unittest.main()
