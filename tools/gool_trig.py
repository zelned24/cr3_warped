"""Host model of the extracted integer table; native replay is its oracle.

This model shares the immutable production table. Independent tests validate
that table and compiled VM stores against the original executable bytes.
"""
from functools import lru_cache
from pathlib import Path
import re


@lru_cache(maxsize=1)
def quarter_wave():
    source=(Path(__file__).resolve().parents[1]/'source/gool_trig_table.h').read_text()
    body=source.split('gool_trig_quarter[1025] = {',1)[1].split('}',1)[0]
    values=tuple(map(int,re.findall(r'-?\d+',body)))
    if len(values)!=1025 or values[0]!=0 or values[-1]!=4096:
        raise ValueError('malformed extracted integer sine table')
    return values


def sine(angle):
    phase=angle&4095
    offset=phase&1023
    if phase&1024:offset=1024-offset
    value=quarter_wave()[offset]
    return -value if phase&2048 else value
