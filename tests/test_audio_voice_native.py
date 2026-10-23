"""Compare bounded C voice controls with original SCUS-94244 MIPS instructions.

The replay models integer instructions and delay slots over fixture RAM.
Only level-dependent spatial-mode lookup is supplied at its known boundary;
no PS1 hardware timing, SPU execution, or full game reachability is claimed.
"""
import ctypes as ct
from pathlib import Path
import random
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from psx_native import Image, ORIGINAL_SHA256
from test_gool_stack_native import replay, MASK
import cb3


class Template(ct.Structure):
    _fields_ = [("bytes", ct.c_uint8 * 76)]


class Control(ct.Structure):
    _fields_ = [("source_operand", ct.c_uint16), ("target_member", ct.c_uint8),
                ("selector", ct.c_uint8), ("options", ct.c_uint32)]


class Play(ct.Structure):
    _fields_ = [("volume_operand", ct.c_uint16), ("sample_operand", ct.c_uint16)]


class Environment(ct.Structure):
    _fields_ = [("master_volume", ct.c_int16), ("ramp_ticks", ct.c_int32),
                ("spatial_mode", ct.c_uint8)]


class NativeAudioVoice(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = Image.parse((Path(cb3.ISO) / "SCUS_942.44").read_bytes())
        if cls.image.sha256 != ORIGINAL_SHA256:
            raise AssertionError("Wrong original executable")
        cls.temporary = tempfile.TemporaryDirectory(prefix="crash-audio-voice-")
        extension = ".dll" if os.name == "nt" else ".so"
        path = Path(cls.temporary.name) / ("voice" + extension)
        command = [sys.executable, "-m", "ziglang", "cc", "-shared", "-std=c11",
                   "-O2", "-Wall", "-Wextra", "-Werror"]
        command += ["-Wl,--export-all-symbols"] if os.name == "nt" else ["-fPIC"]
        subprocess.run(command + [str(ROOT / "source/audio_voice.c"), "-o", str(path)], check=True)
        cls.lib = ct.CDLL(str(path))
        cls.lib.audio_voice_template_reset.argtypes = [ct.POINTER(Template), ct.c_uint]
        cls.lib.audio_voice_template_control.argtypes = [ct.POINTER(Template), ct.POINTER(Environment),
            ct.POINTER(Control), ct.c_void_p, ct.c_size_t, ct.POINTER(ct.c_int32)]
        cls.lib.audio_voice_decode_control.argtypes = [ct.c_uint32, ct.POINTER(Control)]
        cls.lib.audio_voice_decode_play.argtypes = [ct.c_uint32, ct.POINTER(Play)]

    @classmethod
    def tearDownClass(cls):
        if os.name == "nt":
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        del cls.lib
        cls.temporary.cleanup()

    def native(self, raw, selector=None, options=0, source=b"", master=16383, ticks=1, mode=5):
        template = 0x8006cf78
        gp = 0x800b0000
        memory = {template + i: int.from_bytes(raw[i:i+4], "little") for i in range(0, 76, 4)}
        memory.update({0x80068f1c: 0x1000, gp+0x28c: 0, gp+0x278: master & 65535,
                       gp+0x188: ticks & MASK})
        source_address = 0x800c0000 if source else 0
        for i in range(0, len(source), 4):
            memory[source_address+i] = int.from_bytes(source[i:i+4].ljust(4, b"\0"), "little")
        for i in range(15):
            memory[0x8001060c+i*4] = self.image.word(0x8001060c+i*4)
        registers = {29: 0x801f0000, 28: gp, 4: 0, 5: (selector or 0) | options,
                     6: source_address, 7: 0x800d0000}
        def spatial(registers, memory):
            registers[2] = mode
        registers = replay(self.image, 0x80033050 if selector is None else 0x80033c54,
                           registers, memory, hooks={0x80033000: spatial})
        output = b"".join(memory[template+i].to_bytes(4, "little") for i in range(0, 76, 4))
        return output, memory[gp+0x188], registers[2]

    def test_320_native_resets_preserve_untouched_bytes_and_flags(self):
        rng = random.Random(0x9424400)
        for i in range(320):
            raw = rng.randbytes(76)
            mode = i % 16
            state = Template.from_buffer_copy(raw)
            expected, _, _ = self.native(raw, mode=mode)
            self.assertEqual(self.lib.audio_voice_template_reset(ct.byref(state), mode), 0)
            self.assertEqual(bytes(state.bytes), expected)

    def test_2912_template_controls_match_native_bytes_and_results(self):
        rng = random.Random(0x9424442)
        selectors = (0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13)
        count = 0
        for selector in selectors:
            for i in range(224):
                raw = rng.randbytes(76)
                mode = i % 16
                options = ((i >> 1) & 7) << 29
                source = rng.randbytes(12)
                if selector == 8 and i % 2:
                    source = (1).to_bytes(4, "little") + source[4:]
                if selector == 6 and i % 2:
                    source = bytes(4) + source[4:]
                if selector == 9 or selector == 13 and i % 2:
                    source = b""
                master = (-32768, -1, 0, 1, 16383, 32767)[i % 6]
                ticks = (-2147483648, -1, 1, 2, 60, 2147483647)[i % 6]
                expected, expected_ticks, answer = self.native(raw, selector, options, source,
                                                               master, ticks, mode)
                state = Template.from_buffer_copy(raw)
                environment = Environment(master, ticks, mode)
                control = Control(0, 0, selector, options)
                value = ct.c_int32(123456)
                buffer = ct.create_string_buffer(source) if source else None
                status = self.lib.audio_voice_template_control(ct.byref(state), ct.byref(environment),
                    ct.byref(control), buffer, len(source), ct.byref(value))
                self.assertEqual(status, 0, (selector, i))
                self.assertEqual(bytes(state.bytes), expected, (selector, i))
                self.assertEqual(environment.ramp_ticks & MASK, expected_ticks)
                self.assertEqual(value.value & MASK, answer)
                count += 1
        self.assertEqual(count, 2912)

    def test_512_control_wrapper_requests_match_native_registers(self):
        rng = random.Random(0x9424478)
        obj, source, top = 0x80070000, 0x80071000, 0x80072000
        for i in range(512):
            member = i % 64
            instruction = 0x42000000 | ((i % 16) << 20) | ((i & 3) << 18) | (member << 12) | 0xe25
            memory = {obj+0xd4: rng.getrandbits(32), obj+0xbc: top,
                      obj+0x40+member*4: rng.getrandbits(32), top-4: rng.getrandbits(32)}
            memory[obj+0xbc] = top
            # source operand 0xE25 references object member37 directly.
            registers = replay(self.image, 0x800378f8, {4: obj, 5: instruction, 29: 0x801f0000},
                               memory, stops=(0x80033c54,))
            control = Control()
            self.assertEqual(self.lib.audio_voice_decode_control(instruction, ct.byref(control)), 0)
            self.assertEqual(control.source_operand, 0xe25)
            self.assertEqual(registers[6], obj+0xd4)
            self.assertEqual(registers[7], obj)
            self.assertEqual(control.target_member, member)
            self.assertEqual(registers[4], 0 if member == 0 else memory[top-4] if member == 31
                             else memory[obj+0x40+member*4])
            self.assertEqual(registers[5], control.selector | control.options)

    def test_invalid_and_service_requests_are_transactional(self):
        raw = bytes(range(76))
        for selector, target, source_size, expected in ((2, 0, 12, -2), (14, 0, 4, -2),
                (1, 1, 4, -2), (0, 0, 3, -1), (1, 0, 1, -1), (3, 0, 11, -1)):
            state = Template.from_buffer_copy(raw)
            environment = Environment(16383, 1, 5)
            before_environment = bytes(environment)
            control = Control(0, target, selector, 0)
            answer = ct.c_int32(23)
            source = ct.create_string_buffer(bytes(12))
            self.assertEqual(self.lib.audio_voice_template_control(ct.byref(state), ct.byref(environment),
                ct.byref(control), source, source_size, ct.byref(answer)), expected)
            self.assertEqual(bytes(state.bytes), raw)
            self.assertEqual(bytes(environment), before_environment)
            self.assertEqual(answer.value, 23)
        state = Template.from_buffer_copy(raw)
        self.assertEqual(self.lib.audio_voice_template_reset(ct.byref(state), 16), -1)
        self.assertEqual(bytes(state.bytes), raw)

    def test_512_play_wrappers_match_native_signed_volume_and_pop_order(self):
        rng = random.Random(0x9424441)
        obj, top = 0x80070000, 0x80072000
        for i in range(512):
            stacked = bool(i & 1)
            upper, lower = (0xe1f, 0xe1f) if stacked else (0xe26, 0xe25)
            instruction = 0x41000000 | (upper << 12) | lower
            volume = rng.getrandbits(32)
            memory = {obj+0xd8: volume, obj+0xd4: rng.getrandbits(32),
                      obj+0xbc: top, top-4: volume, top-8: rng.getrandbits(32)}
            request = Play()
            self.assertEqual(self.lib.audio_voice_decode_play(instruction, ct.byref(request)), 0)
            self.assertEqual((request.volume_operand, request.sample_operand), (upper, lower))
            captured = []
            backend_result = rng.getrandbits(32)
            def backend(registers, memory):
                captured.append(tuple(registers[4:7]))
                registers[2] = backend_result
            replay(self.image, 0x8003780c, {4: obj, 5: instruction, 29: 0x801f0000},
                   memory, hooks={0x80033864: backend})
            signed_volume = ct.c_int16(volume & 65535).value & MASK
            self.assertEqual(captured, [(obj, top-8 if stacked else obj+0xd4, signed_volume)])
            self.assertEqual(memory[obj+0xbc], top-8 if stacked else top)
            self.assertEqual(memory[obj+0x12c], backend_result)


if __name__ == "__main__":
    unittest.main()
