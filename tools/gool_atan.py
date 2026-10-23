"""Integer atan host model; original MIPS replay independently checks the C VM."""
from functools import lru_cache
from pathlib import Path
import re


def signed(v):
    v &= 0xffffffff
    return v-0x100000000 if v&0x80000000 else v


@lru_cache(maxsize=1)
def table():
    text=(Path(__file__).resolve().parents[1]/'source/gool_atan_table.h').read_text()
    values=tuple(map(int,re.findall(r'-?\d+',text.split('{',1)[1].split('}',1)[0])))
    if len(values)!=1025:raise ValueError('atan ratio table extent')
    return values


def atan2(y,x):
    a=(-y if y<0 else y)&0xffffffff
    b=(-x if x<0 else x)&0xffffffff
    if not a and not b:return 0
    swapped=signed(a)>=signed(b)
    small,large=(b,a) if swapped else (a,b)
    numerator,denominator=(signed(small),signed(large)>>10) if small&0x7fe00000 else (signed(small<<10),signed(large))
    if not denominator or (numerator==-2147483648 and denominator==-1):
        raise ValueError('native atan division trap')
    index=abs(numerator)//abs(denominator)
    if (numerator<0)!=(denominator<0):index=-index
    if not 0<=index<=1024:raise ValueError('native atan table range')
    angle=table()[index]
    if swapped:angle=1024-angle
    if x<0:angle=2048-angle
    return -angle if y<0 else angle
