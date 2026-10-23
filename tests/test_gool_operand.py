"""Exhaustive Python operand contract checks; no host/target compilation."""
from collections import Counter
from pathlib import Path
import hashlib
import sys
from types import SimpleNamespace
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import gool_operand as operand
import initial_graphics


def js_i32(x):
    x&=0xffffffff
    return x if x<0x80000000 else x-0x100000000


def reference(raw):
    # Shift/sign rules from the reference, with native signed frame offsets.
    # Deliberately independent of the bit-width arithmetic in decode().
    if raw&0x800==0:return ('constant',raw&0x3ff,int(raw&0x400!=0))
    if raw&0x400:
        if raw&0x200:return ('variable',raw&0x1ff,0)
        return ('indirect',raw&0x3f,(raw>>6)&7)
    if raw&0x200==0:return ('literal',js_i32(raw<<23)>>15,0)
    if raw&0x100==0:return ('literal',js_i32(raw<<24)>>20,0)
    if raw&0x80==0:
        return ('frame',js_i32(raw<<25)>>25,0)
    return ('boolean',int(raw&0x10!=0),0)


class GoolOperand(unittest.TestCase):
    def test_pinned_reference(self):
        source=(ROOT/'tools/reference/crashwarped_script.ts').read_bytes()
        self.assertEqual(hashlib.sha256(source).hexdigest(),
            'c23246b0518b8c36dbddcdc62af8176831c41238f6219ab48828559a9ea05ad1')

    def test_all_4096_encodings(self):
        counts=Counter();invalid=[]
        for raw in range(4096):
            with self.subTest(raw=hex(raw)):
                try:expected=reference(raw)
                except ValueError:
                    invalid.append(raw)
                    with self.assertRaises(ValueError):operand.decode(raw)
                else:
                    actual=operand.decode(raw)
                    self.assertEqual(tuple(actual),expected)
                    counts[actual.kind]+=1
                simple=raw|0x800
                if simple&0x400==0:simple=(simple&~0x80)|0x300
                self.assertEqual(operand.simple(raw),simple)
        self.assertEqual(invalid,[])
        self.assertEqual(sum(counts.values()),4096)
        self.assertEqual(counts['frame'],128)
        print('4096 operands checked, including native frame header offsets:',dict(counts))

    def test_boundaries_and_dynamic_values(self):
        expected={0x800:0,0x8ff:65280,0x900:-65536,0x9ff:-256,
                  0xa7f:2032,0xa80:-2048,0xaff:-16}
        for raw,value in expected.items():self.assertEqual(operand.decode(raw).value,value)
        base=SimpleNamespace(items=[b'',b'',bytes.fromhex('ffffffff00000080')])
        alt=SimpleNamespace(items=[b'',b'',bytes.fromhex('78563412')])
        self.assertEqual(initial_graphics.constant(0,base,alt),-1)
        self.assertEqual(initial_graphics.constant(1,base,alt),-2147483648)
        self.assertEqual(initial_graphics.constant(0x400,base,alt),0x12345678)
        for raw in (2,0x401,0xe1f,0xe2a,0xc2a,0xb03,0xb7f,0xb00):
            self.assertIsNone(initial_graphics.constant(raw,base,alt))

    def test_invalid_input(self):
        for raw in (-1,4096,1<<32,'0'):
            with self.assertRaises(ValueError):operand.decode(raw)
            with self.assertRaises(ValueError):operand.simple(raw)


if __name__=='__main__':unittest.main()
