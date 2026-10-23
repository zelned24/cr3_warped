"""Reproduce the integer atan table from the audited original executable."""
import argparse
from pathlib import Path
import struct

import cb3
from psx_native import Image,ORIGINAL_SHA256

ROOT=Path(__file__).resolve().parents[1]


def table_source(image):
    if image.sha256!=ORIGINAL_SHA256:raise ValueError('wrong original executable')
    values=struct.unpack('<1025h',image.read(0x8005f6b8,2050))
    if values[0]!=0 or values[-1]!=512 or any(a>b for a,b in zip(values,values[1:])):
        raise ValueError('malformed original atan table')
    lines=['/* Original SCUS_942.44 atan ratio table at 0x8005F6B8. */',
           '#ifndef GOOL_ATAN_TABLE_H','#define GOOL_ATAN_TABLE_H','#include <stdint.h>',
           'static const int16_t gool_atan_ratio[1025] = {']
    lines+=['    '+', '.join(map(str,values[i:i+16]))+',' for i in range(0,1025,16)]
    return '\n'.join(lines+['};','#endif',''])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=Path(cb3.ISO)/'SCUS_942.44')
    args=parser.parse_args()
    image=Image.parse(args.exe.read_bytes())
    (ROOT/'source/gool_atan_table.h').write_text(table_source(image))
    print('Extracted original 1025-entry atan table; executable SHA verified.')


if __name__=='__main__':main()
