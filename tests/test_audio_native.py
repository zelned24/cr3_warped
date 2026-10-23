"""Original3780C/378F8 ABI replay with explicit33864/33C54 backend hooks.

Compares native operand order, stack pops, signed volume, control flags and
result stores. Backend hooks establish the call ABI, not SPU/INST execution.
"""
import ctypes as C
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from psx_native import Image, ORIGINAL_SHA256
from test_gool_stack_native import replay
import cb3

HARNESS = r'''
#include "audio_native.h"
#include <string.h>
static uint32_t *capture;
int gool_native_registry_read(void *c,uint32_t a,uint32_t *v){gool_native_registry *r=c;return gool_domain_read32(r->domain,a,v);}
int gool_native_registry_write(void *c,uint32_t a,uint32_t v){gool_native_registry *r=c;return gool_domain_write32(r->domain,a,v);}
static int play(void *c,uint32_t n,uint32_t s,int16_t v,int32_t *result){
 (void)c;capture[0]=n;capture[1]=s;capture[2]=(uint32_t)(int32_t)v;*result=0x12345678;return 0;
}
static int control(void *c,uint32_t t,uint32_t f,uint32_t s,uint32_t n,int32_t *result){
 (void)c;capture[0]=t;capture[1]=f;capture[2]=s;capture[3]=n;*result=0x12345678;return 0;
}
int fixture(uint8_t *object,uint32_t instruction,uint32_t *args){
 gool_domain domain={0};gool_domain_lease lease;capture=args;
 int e=gool_domain_map_mutable(&domain,0x81200000,object,4096,&lease);if(e)return e;
 gool_native_actor *actors[1]={0};gool_native_registry r={.domain=&domain,.actors=actors,.count=1};
 audio_native_services services={.play=play,.control=control};
 e=audio_native_apply(&r,0x81200000,instruction,&services);
 int cleanup=gool_domain_release(&domain,lease);return e?e:cleanup;
}
'''


def main():
    image = Image.parse((Path(cb3.ISO) / "SCUS_942.44").read_bytes())
    assert image.sha256 == ORIGINAL_SHA256
    assert image.word(0x8005d120 + 0x41 * 4) == 0x800398b8
    assert image.word(0x8005d120 + 0x42 * 4) == 0x800398c4
    rng = random.Random(0x3780c)
    node = 0x81200000
    with tempfile.TemporaryDirectory(prefix="crash-audio-native-") as folder:
        path = Path(folder)
        (path / "fixture.c").write_text(HARNESS)
        library = path / ("fixture.dll" if os.name == "nt" else "fixture.so")
        command = [sys.executable, "-m", "ziglang", "cc", "-shared", "-O2", "-Wall", "-Wextra", "-Werror"]
        command += ["-Wl,--export-all-symbols"] if os.name == "nt" else ["-fPIC"]
        subprocess.run(command + ["-I", str(ROOT / "source"), str(path / "fixture.c"),
            str(ROOT / "source/audio_native.c"), str(ROOT / "source/audio_voice.c"),
            str(ROOT / "source/gool_domain.c"), "-o", str(library)], check=True)
        lib = C.CDLL(str(library))
        lib.fixture.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32)]
        class Template(C.Structure):
            _fields_ = [("bytes", C.c_uint8 * 76)]
        class Environment(C.Structure):
            _fields_ = [("master", C.c_int16), ("ramp", C.c_int32), ("mode", C.c_uint8)]
        Reader = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32))
        Writer = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32)
        Bank = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32))
        Resource = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.POINTER(C.c_uint32))
        Volume = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_int16), C.POINTER(C.c_int16))
        Random = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_int32))
        lib.audio_voice_template_reset_native.argtypes = [C.POINTER(Template), C.POINTER(Environment), C.c_void_p, Reader]
        lib.audio_voice_select_native.argtypes = [C.c_void_p, Reader, Volume, Random, C.c_uint32, C.POINTER(C.c_int32)]
        lib.audio_voice_resolve_native.argtypes = [C.c_void_p, Reader, Writer, Bank, Resource, C.c_uint32, C.c_uint32, C.POINTER(C.c_int32)]
        try:
            for case in range(512):
                state, sample, descriptor = 0x8006cf78, 0x800c0000, 0x800d0000
                memory = {state+i: rng.getrandbits(32) for i in range(0, 76, 4)}
                memory[sample] = (rng.getrandbits(32) | 1) if case & 1 else descriptor
                memory[descriptor+4] = rng.getrandbits(32) | 1
                initial_memory = dict(memory)
                bank_handle = 0x800e0000 if case % 3 == 0 else 0
                resource_handle = [0, 1, 0xffffffff, 0xfffffff2, 0xffffff02, 0xffffff01,
                    0xc0000002, 0xc0012346, 0x800e1000, 0x80000000][case % 10]
                reread_reference, reread_flags = rng.getrandbits(32), rng.getrandbits(32)
                expected_calls = []

                def native_bank(registers, native_memory):
                    expected_calls.append(("bank", registers[4]))
                    native_memory[sample] = reread_reference
                    registers[2] = bank_handle

                def native_resource(registers, native_memory):
                    expected_calls.append(("resource", *registers[4:7]))
                    native_memory[state] = reread_flags
                    registers[2] = resource_handle

                registers = replay(image, 0x80033760, {4: state, 5: sample, 29: 0x801f0000}, memory,
                    hooks={0x80015414: native_bank, 0x800142b4: native_resource})
                actual_memory, actual_calls = dict(initial_memory), []

                @Reader
                def read_resource(context, address, word):
                    if address not in actual_memory:
                        return -93
                    word[0] = actual_memory[address]
                    return 0

                @Writer
                def write_resource(context, address, word):
                    actual_memory[address] = word
                    return 0

                @Bank
                def bank(context, eid, word):
                    actual_calls.append(("bank", eid))
                    actual_memory[sample] = reread_reference
                    word[0] = bank_handle
                    return 0

                @Resource
                def resource(context, address, mode, create, word):
                    actual_calls.append(("resource", address, mode, create))
                    actual_memory[state] = reread_flags
                    word[0] = resource_handle
                    return 0

                result = C.c_int32(-999)
                assert lib.audio_voice_resolve_native(None, read_resource, write_resource, bank, resource,
                    state, sample, C.byref(result)) == 0
                assert result.value & 0xffffffff == registers[2], (case, result.value, registers[2])
                assert actual_calls == expected_calls, (case, actual_calls, expected_calls)
                assert all(actual_memory[address] == memory[address] for address in initial_memory), case
            for case in range(2048):
                first = case % 25
                voices = bytearray(rng.randbytes(24 * 80))
                uniform = case % 5 == 0
                common = rng.randrange(256)
                for channel in range(24):
                    flags = rng.getrandbits(32)
                    if case % 3:
                        flags |= 8
                    struct.pack_into("<I", voices, channel * 80 + 4, flags)
                    if uniform:
                        voices[channel * 80 + 0x1d] = common
                template_priority = common if uniform else rng.randrange(256)
                memory = {0x8006c7d8+i: int.from_bytes(voices[i:i+4], "little") for i in range(0, len(voices), 4)}
                memory[0x8006cf90] = template_priority << 8
                memory[0x800b028c] = first
                levels = [(rng.randrange(-32768, 32768), rng.randrange(-32768, 32768)) for _ in range(24)]
                if case % 7 == 0:
                    levels = [(100, -100)] * 24
                random_value = rng.randrange(100)
                expected_calls = []

                def spu(registers, native_memory):
                    pointer = registers[4]
                    mask = native_memory[pointer]
                    channel = mask.bit_length()-1
                    expected_calls.append(("volume", mask))
                    left, right = levels[channel]
                    native_memory[pointer+8] = (left & 65535) | ((right & 65535) << 16)

                def native_random(registers, native_memory):
                    expected_calls.append(("random", registers[4]))
                    registers[2] = random_value

                registers = replay(image, 0x8003359c, {28: 0x800b0000, 29: 0x801f0000}, memory,
                    hooks={0x80052844: spu, 0x80032160: native_random}, max_steps=8192)
                actual_calls = []

                @Reader
                def read_voice(context, address, word):
                    if address not in memory:
                        return -93
                    word[0] = memory[address]
                    return 0

                @Volume
                def volume(context, mask, left, right):
                    actual_calls.append(("volume", mask))
                    left[0], right[0] = levels[mask.bit_length()-1]
                    return 0

                @Random
                def random_voice(context, upper, value):
                    actual_calls.append(("random", upper))
                    value[0] = random_value
                    return 0

                selected = C.c_int32(-999)
                assert lib.audio_voice_select_native(None, read_voice, volume, random_voice, first, C.byref(selected)) == 0
                assert selected.value & 0xffffffff == registers[2], (case, first, selected.value, registers[2])
                assert actual_calls == expected_calls, (case, actual_calls, expected_calls)
            for level in [*range(256), 0xffffffff, 0x80000000]:
                initial = rng.randbytes(76)
                template = 0x8006cf78
                memory = {template+i: int.from_bytes(initial[i:i+4], "little") for i in range(0, 76, 4)}
                memory.update({0x80061044: 0x800c0000, 0x800c0008: level})
                memory.update({0x8001057c+i*4: image.word(0x8001057c+i*4) for i in range(35)})
                registers = replay(image, 0x80033050, {29: 0x801f0000}, memory)
                expected = b"".join(memory[template+i].to_bytes(4, "little") for i in range(0, 76, 4))
                reads = []

                @Reader
                def read(context, address, word):
                    reads.append(address)
                    if address not in memory:
                        return -93
                    word[0] = memory[address]
                    return 0

                state = Template.from_buffer_copy(initial)
                environment = Environment(1234, 17, 0)
                assert lib.audio_voice_template_reset_native(C.byref(state), C.byref(environment), None, read) == 0
                assert bytes(state.bytes) == expected, level
                assert environment.master == 1234 and environment.ramp == 17
                assert environment.mode == ((int.from_bytes(expected[:4], "little") >> 9) & 15)
                assert reads == [0x80061044, 0x800c0008]
            for case in range(2048):
                data = bytearray(rng.randbytes(4096))
                struct.pack_into("<I", data, 0xbc, node + 0x400)
                source = (0xe00 + rng.randrange(512)) if case % 4 else 0xe1f
                if case & 1:
                    volume = (0xe00 + rng.randrange(512)) if case % 3 else 0xe1f
                    instruction = 0x41000000 | (volume << 12) | source
                    entry = 0x8003780c
                else:
                    member = [0, 1, 31, 63][(case // 2) % 4]
                    instruction = 0x42000000 | (rng.randrange(16) << 20) | (rng.randrange(4) << 18) | (member << 12) | source
                    entry = 0x800378f8
                memory = {node + i: int.from_bytes(data[i:i+4], "little") for i in range(0, len(data), 4)}
                expected = []

                def backend(registers, native_memory):
                    expected.extend(registers[4:7] if instruction >> 24 == 0x41 else registers[4:8])
                    registers[2] = 0x12345678

                replay(image, entry, {4: node, 5: instruction, 29: 0x801f0000}, memory,
                       hooks={0x80033864: backend, 0x80033c54: backend})
                original = b"".join(memory[node+i].to_bytes(4, "little") for i in range(0, len(data), 4))
                output = (C.c_uint8 * len(data)).from_buffer_copy(data)
                args = (C.c_uint32 * 4)()
                assert lib.fixture(output, instruction, args) == 0, (case, hex(instruction))
                assert bytes(output) == original, (case, hex(instruction), "object/stack effects")
                assert list(args)[:len(expected)] == expected, (case, hex(instruction), list(args), expected)
        finally:
            if os.name == "nt":
                import _ctypes
                _ctypes.FreeLibrary(lib._handle)
    print("PASS audio native: 512 actual33760 sample classifications, 2048 actual3359C voice selections, 258 actual33000/33050 resets and 2048 original3780C/378F8 ABI comparisons; SPU/INST playback remains external")


if __name__ == "__main__":
    main()
