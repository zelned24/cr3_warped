"""GOOL operands with native signed frame offsets (SCUS-94244 0x80038D00).

Frame offsets 0..2 expose the three original header words.
Variables/frames/indirect references remain dynamic until an execution context
provides them. This module does not implement an entity VM.
"""
from typing import NamedTuple


class Operand(NamedTuple):
    kind:str
    value:int
    base:int=0


def signed(value,bits):
    value&=(1<<bits)-1
    return value-(1<<bits) if value&(1<<(bits-1)) else value


def decode(raw):
    if not isinstance(raw,int) or not 0<=raw<4096:
        raise ValueError('operand must fit in 12 bits')
    if not raw&0x800:return Operand('constant',raw&0x3ff,int(bool(raw&0x400)))
    if raw&0x400:
        if raw&0x200:return Operand('variable',raw&0x1ff)
        return Operand('indirect',raw&0x3f,(raw>>6)&7)
    if not raw&0x200:return Operand('literal',signed(raw,9)*256)
    if not raw&0x100:return Operand('literal',signed(raw,8)*16)
    if not raw&0x80:
        return Operand('frame',signed(raw,7))
    return Operand('boolean',int(bool(raw&0x10)))


def simple(raw):
    if not isinstance(raw,int) or not 0<=raw<4096:
        raise ValueError('operand must fit in 12 bits')
    raw|=0x800
    return raw if raw&0x400 else (raw&~0x80)|0x300
