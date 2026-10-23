"""Compare allocation-free production decoding with tools/nsf.py and disc pages.

The supplied 42 NSF archives currently contain raw pages only. Compressed
coverage uses the Python format decoder as a differential fixture oracle.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cb3
import nsf

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "nsf_page.h"
#ifdef NDEBUG
#error Assertions are required
#endif
static uint8_t source[2*NSF_PAGE_BYTES+16],output[NSF_PAGE_BYTES+16],expected[NSF_PAGE_BYTES];
int main(void) {
#ifdef _WIN32
    assert(_setmode(_fileno(stdin),_O_BINARY)!=-1);
#endif
    size_t consumed=0xaced1234u;
    source[0]=0x34;source[1]=0x12;
    assert(nsf_page_decode(source,NSF_PAGE_BYTES,source,NSF_PAGE_BYTES,&consumed)==-1);
    assert(consumed==0xaced1234u&&source[0]==0x34&&source[1]==0x12);
    assert(nsf_page_decode(source,NSF_PAGE_BYTES,output,NSF_PAGE_BYTES,(size_t *)source)==-1);
    assert(nsf_page_decode(source,NSF_PAGE_BYTES,output,NSF_PAGE_BYTES,(size_t *)output)==-1);
    assert(nsf_page_decode(NULL,NSF_PAGE_BYTES,output,NSF_PAGE_BYTES,&consumed)==-1);
    assert(nsf_page_decode(source,NSF_PAGE_BYTES,NULL,NSF_PAGE_BYTES,&consumed)==-1);
    assert(nsf_page_decode(source,NSF_PAGE_BYTES,output,NSF_PAGE_BYTES,NULL)==-1);
    assert(nsf_page_decode(source+1,NSF_PAGE_BYTES,source,NSF_PAGE_BYTES,&consumed)==-1);
    source[1]=0x34;source[2]=0x12;
    assert(nsf_page_decode(source+1,NSF_PAGE_BYTES,output,NSF_PAGE_BYTES,&consumed)==0);
    assert(consumed==NSF_PAGE_BYTES&&output[0]==0x34&&output[1]==0x12);
    uint32_t header[4];unsigned pages=0;
    while(fread(header,sizeof(header),1,stdin)==1) {
        uint32_t size=header[0],used=header[1],capacity=header[2];
        int should_pass=header[3]!=0;
        assert(size<=sizeof(source));
        assert(fread(source,1,size,stdin)==size);
        if(should_pass)assert(fread(expected,1,NSF_PAGE_BYTES,stdin)==NSF_PAGE_BYTES);
        memset(output,0xa5,sizeof(output));consumed=0xaced1234u;
        int status=nsf_page_decode(source,size,output,capacity,&consumed);
        assert(status==(should_pass?NSF_PAGE_OK:NSF_PAGE_INVALID));
        if(should_pass) {
            assert(consumed==used);
            assert(memcmp(output,expected,NSF_PAGE_BYTES)==0);
        } else {
            assert(consumed==0xaced1234u);
            for(size_t i=0;i<NSF_PAGE_BYTES;i++)assert(output[i]==0xa5);
        }
        for(size_t i=NSF_PAGE_BYTES;i<sizeof(output);i++)assert(output[i]==0xa5);
        pages++;
    }
    assert(feof(stdin)&&!ferror(stdin));
    printf("PASS %u bounded page cases\n",pages);
    return 0;
}
'''


def packet(source, expected=None, used=0, capacity=nsf.CHUNK_SIZE):
    result = struct.pack("<IIII", len(source), used, capacity, expected is not None) + source
    return result + (expected if expected is not None else b"")


def compressed(length, stream, skip=0, tail=None):
    if tail is None:
        tail = bytes((i * 37 + 9) & 255 for i in range(nsf.CHUNK_SIZE - length))
    return struct.pack("<HHII", 0x1235, 0, length, skip) + stream + bytes(skip) + tail


def backref(distance, span):
    return bytes((0x80 | (distance >> 5), (distance & 31) << 3 | (7 if span == 64 else span - 3)))


def main():
    valid = [compressed(0, b""), compressed(3, b"\x03ABC", skip=17),
             compressed(3, b"\x00\x03ABC"),
             compressed(66, b"\x02AB" + backref(2, 64))]
    for span in [*range(3, 10), 64]:
        valid.append(compressed(3 + span, b"\x03ABC" + backref(3, span)))
    prefix = bytes(range(64))
    valid.append(compressed(nsf.CHUNK_SIZE, bytes((64,)) + prefix + backref(64, 64) * 1023))
    literal = bytes(i & 255 for i in range(nsf.CHUNK_SIZE))
    stream = b"".join(bytes((len(literal[i:i+127]),)) + literal[i:i+127]
                      for i in range(0, len(literal), 127))
    valid.append(compressed(nsf.CHUNK_SIZE, stream))
    prefix = literal[:4095]
    stream = b"".join(bytes((len(prefix[i:i+127]),)) + prefix[i:i+127]
                      for i in range(0, len(prefix), 127))
    valid.append(compressed(4095 + 64, stream + backref(4095, 64)))
    cases = []
    for source in valid:
        expected, used = nsf.decompress_page(source, 0)
        assert len(expected) == nsf.CHUNK_SIZE and used == len(source)
        cases.append(packet(source + b"next", expected, used))
    raw = b"\x34\x12" + bytes(nsf.CHUNK_SIZE - 2)
    cases.append(packet(raw + b"next", raw, len(raw)))
    bad = [b"", b"\x35", b"\x00\x00", raw[:-1],
           struct.pack("<HHII", 0x1235, 0, nsf.CHUNK_SIZE + 1, 0),
           compressed(3, b"\x03AB", tail=b""),
           compressed(3, b"\x04ABCD"),
           compressed(3, b"\x80", tail=b""),
           compressed(3, backref(1, 3)),
           compressed(4, b"\x01A" + backref(2, 3)),
           compressed(4, b"\x01A" + backref(0, 3)),
           compressed(4, b"\x01A" + backref(1, 4)),
           struct.pack("<HHII", 0x1235, 0, 0, 0xFFFFFFFF) + raw,
           compressed(0, b"")[:-1],
           struct.pack("<HHII", 0x1235, 0, 1, 0) + bytes(256)]
    bad += [valid[1][:size] for size in range(2, 12)]
    cases.extend(packet(source) for source in bad)
    cases.append(packet(raw, capacity=nsf.CHUNK_SIZE - 1))
    with tempfile.TemporaryDirectory(prefix="crash3-nsf-page-") as folder:
        folder = Path(folder)
        source = folder / "test.c"
        source.write_text(HARNESS)
        executable = folder / "test.exe"
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11",
                        "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
                        "-I" + str(ROOT / "source"), str(ROOT / "source/nsf_page.c"),
                        str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], input=b"".join(cases), check=True,
                       stdout=subprocess.DEVNULL)
        levels = cb3.all_level_ids()
        assert len(levels) == 42, "All 42 original NSF archives are required"
        counts = {nsf.MAGIC_PAGE: 0, nsf.MAGIC_COMPRESSED: 0}
        for level in levels:
            archive = Path(cb3.level_paths(level)[0]).read_bytes()
            offset, cases = 0, []
            while offset < len(archive):
                magic, = struct.unpack_from("<H", archive, offset)
                counts[magic] += 1
                if magic == nsf.MAGIC_COMPRESSED:
                    expected, used = nsf.decompress_page(archive, offset)
                else:
                    assert magic == nsf.MAGIC_PAGE
                    expected, used = archive[offset:offset + nsf.CHUNK_SIZE], nsf.CHUNK_SIZE
                assert len(expected) == nsf.CHUNK_SIZE
                cases.append(packet(archive[offset:offset + used + 4], expected, used))
                offset += used
            assert offset == len(archive)
            subprocess.run([str(executable)], input=b"".join(cases), check=True,
                           stdout=subprocess.DEVNULL)
        print(f"PASS all {len(levels)} NSF archives: {counts[nsf.MAGIC_PAGE]} original raw pages, "
              f"{counts[nsf.MAGIC_COMPRESSED]} original compressed pages; "
              f"{len(valid)} Python-oracle compressed fixtures; malformed input preserves output")


if __name__ == "__main__":
    main()
