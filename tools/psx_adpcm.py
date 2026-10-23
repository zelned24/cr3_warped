"""Bounded SPU ADPCM decoding, without pitch, ADSR, resampling or mixing.

Independent implementation of the documented PS1 block format; not XA ADPCM.
The decoder preserves predictor history across blocks and loop jumps. Sources:
https://psx-spx.consoledev.net/soundprocessingunitspu/
https://github.com/stenzek/duckstation/blob/master/src/core/spu.cpp (DecodeBlock)
"""
from array import array
from dataclasses import dataclass
import sys

BLOCK_BYTES = 16
BLOCK_SAMPLES = 28
FILTERS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


class AdpcmError(ValueError):
    pass


@dataclass(frozen=True)
class History:
    recent: int = 0
    older: int = 0

    def __post_init__(self):
        if not all(isinstance(value, int) and -32768 <= value <= 32767
                   for value in (self.recent, self.older)):
            raise AdpcmError('predictor history must contain signed 16-bit samples')


@dataclass(frozen=True)
class Block:
    samples: tuple
    history: History
    flags: int
    filter: int
    shift: int

    @property
    def end(self):
        return bool(self.flags & 1)

    @property
    def repeat(self):
        return bool(self.flags & 2)

    @property
    def loop_start(self):
        return bool(self.flags & 4)


def decode_block(data, history=History()):
    """Decode exactly 16 bytes. Flags are metadata, not samples.

    Reserved shifts 13..15 use shift 9; predictors 5..15 use zero feedback.
    Unknown flag bits are retained. Each signed predictor term is shifted
    independently before summation; clamping precedes history update.
    """
    data = bytes(data)
    if len(data) != BLOCK_BYTES:
        raise AdpcmError('SPU block must contain exactly 16 bytes')
    if not isinstance(history, History):
        raise AdpcmError('history must be a History value')
    shift, predictor = data[0] & 15, data[0] >> 4
    effective_shift = shift if shift <= 12 else 9
    positive, negative = FILTERS[predictor] if predictor < len(FILTERS) else (0, 0)
    recent, older = history.recent, history.older
    output = []
    for byte in data[2:]:
        for nibble in (byte & 15, byte >> 4):
            signed = nibble if nibble < 8 else nibble - 16
            sample = (signed << 12) >> effective_shift
            sample += (recent * positive) >> 6
            sample += (older * negative) >> 6
            sample = min(32767, max(-32768, sample))
            output.append(sample)
            older, recent = recent, sample
    return Block(tuple(output), History(recent, older), data[1], predictor, shift)


@dataclass(frozen=True)
class StreamInfo:
    blocks: int
    samples: int
    consumed_bytes: int
    trailing_bytes: int
    end_flags: int | None
    loop_start_byte: int | None
    loop_start_sample: int | None
    external_loop_required: bool


def inspect_stream(data, *, require_end=True):
    """Inspect one stored pass, stopping after the first end block.

    Trailing aligned bytes are reported, never decoded as a second sample.
    No end marker is accepted only when the caller explicitly opts out.
    """
    data = memoryview(data).cast('B')
    if not data or len(data) % BLOCK_BYTES:
        raise AdpcmError('stream must be nonempty and 16-byte aligned in length')
    loop = None
    for offset in range(0, len(data), BLOCK_BYTES):
        flags = data[offset + 1]
        if flags & 4:
            loop = offset
        if flags & 1:
            consumed = offset + BLOCK_BYTES
            return StreamInfo(consumed // 16, consumed // 16 * 28,
                              consumed, len(data) - consumed, flags,
                              loop, None if loop is None else loop // 16 * 28,
                              bool(flags & 2) and loop is None)
    if require_end:
        raise AdpcmError('stream has no end marker within its bounded payload')
    return StreamInfo(len(data) // 16, len(data) // 16 * 28,
                      len(data), 0, None, loop,
                      None if loop is None else loop // 16 * 28, False)


@dataclass(frozen=True)
class DecodedStream:
    pcm: bytes                 # little-endian signed PCM16 mono
    history: History
    blocks: int
    ended: bool
    repeats_done: int
    info: StreamInfo


def decode_stream(data, *, history=History(), repeats=0, initial_loop_byte=None,
                  max_samples=4_000_000, require_end=True):
    """Decode a finite number of passes. No history reset at loop boundaries.

    repeats is the number of extra repeat jumps, not extra whole-file copies.
    A missing in-stream repeat address must be supplied explicitly if a jump
    is requested; no implicit jump to byte zero is invented.
    """
    data = memoryview(data).cast('B')
    info = inspect_stream(data, require_end=require_end)
    if not isinstance(repeats, int) or repeats < 0:
        raise AdpcmError('repeats must be a nonnegative integer')
    if not isinstance(max_samples, int) or max_samples < 0:
        raise AdpcmError('max_samples must be a nonnegative integer')
    if initial_loop_byte is not None and (not isinstance(initial_loop_byte, int)
            or initial_loop_byte < 0 or initial_loop_byte % 16
            or initial_loop_byte + 16 > info.consumed_bytes):
        raise AdpcmError('external loop address must select a bounded complete block')
    output = array('h')
    position, blocks, jumps, ended = 0, 0, 0, False
    loop = initial_loop_byte
    while position < info.consumed_bytes:
        if len(output) + 28 > max_samples:
            raise AdpcmError('decoded sample budget exceeded')
        block = decode_block(data[position:position + 16], history)
        history = block.history
        output.extend(block.samples)
        blocks += 1
        if block.loop_start:
            loop = position
        if block.end:
            ended = True
            if block.repeat and jumps < repeats:
                if loop is None:
                    raise AdpcmError('repeat jump requires an explicit loop address')
                position = loop
                jumps += 1
                continue
            break
        position += 16
    if output.itemsize != 2:
        raise AdpcmError('host signed-short representation is not 16 bits')
    if sys.byteorder != 'little':
        output.byteswap()
    return DecodedStream(output.tobytes(), history, blocks, ended, jumps, info)
