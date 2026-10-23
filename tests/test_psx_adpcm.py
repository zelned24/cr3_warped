"""Pure Python sample decoder tests, no compilation or emulator."""
from pathlib import Path
import random
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from psx_adpcm import AdpcmError, History, decode_block, decode_stream, inspect_stream


def block(header=12, flags=0, byte=0):
    return bytes((header, flags)) + bytes((byte,)) * 14


class PsxAdpcm(unittest.TestCase):
    def test_nibble_order_signed_extension_and_flags_not_samples(self):
        decoded = decode_block(block(0, 0xFD, 0x87))
        self.assertEqual(decoded.samples, (28672, -32768) * 14)
        self.assertEqual(decoded.flags, 0xFD)
        self.assertTrue(decoded.loop_start)
        self.assertTrue(decoded.end)
        self.assertFalse(decoded.repeat)
        self.assertEqual(decoded.history, History(-32768, 28672))

    def test_all_five_filter_first_samples(self):
        for predictor, expected in enumerate((0, 60, 63, 43, 62)):
            self.assertEqual(decode_block(block(predictor << 4 | 12), History(64, 64)).samples[0], expected)

    def test_separate_arithmetic_shift_has_no_combined_rounding_bias(self):
        self.assertEqual(decode_block(block(0x2C), History(1, 2)).samples[0], -1)
        self.assertEqual(decode_block(block(0x1C), History(-1, 0)).samples[0], -1)

    def test_positive_and_negative_saturation_feed_clamped_history(self):
        positive = decode_block(block(0x20, byte=0x77), History(32767, -32768))
        negative = decode_block(block(0x20, byte=0x88), History(-32768, 32767))
        self.assertEqual(positive.samples[:2], (32767, 32767))
        self.assertEqual(negative.samples[:2], (-32768, -32768))
        self.assertTrue(all(-32768 <= sample <= 32767 for sample in positive.samples + negative.samples))

    def test_reserved_shifts_use_nine(self):
        expected = decode_block(block(9, byte=0x87)).samples
        for shift in (13, 14, 15):
            self.assertEqual(decode_block(block(shift, byte=0x87)).samples, expected)

    def test_reserved_predictors_have_no_feedback(self):
        expected = decode_block(block(12, byte=0x87)).samples
        for predictor in range(5, 16):
            self.assertEqual(decode_block(block(predictor << 4 | 12, byte=0x87), History(12000, -6000)).samples, expected)

    def test_every_shift_and_filter_matches_independent_scalar_reference(self):
        randomizer = random.Random(981721)
        coefficients = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))
        for predictor in range(16):
            for shift in range(16):
                payload = bytes(randomizer.randrange(256) for _ in range(14))
                history = History(randomizer.randrange(-32768, 32768), randomizer.randrange(-32768, 32768))
                samples, recent, older = [], history.recent, history.older
                a, b = coefficients[predictor] if predictor < 5 else (0, 0)
                for index in range(28):
                    n = (payload[index // 2] >> (index % 2 * 4)) & 15
                    signed = ((n ^ 8) - 8) * 4096
                    sample = signed // (2 ** (shift if shift <= 12 else 9))
                    sample += recent * a // 64 + older * b // 64
                    sample = min(32767, max(-32768, sample))
                    samples.append(sample)
                    older, recent = recent, sample
                decoded = decode_block(bytes((predictor << 4 | shift, 0)) + payload, history)
                self.assertEqual(decoded.samples, tuple(samples))
                self.assertEqual(decoded.history, History(recent, older))

    def test_history_is_carried_between_blocks(self):
        first, second = block(0x10, byte=0x11), block(0x1C, 1)
        a = decode_block(first)
        b = decode_block(second, a.history)
        decoded = decode_stream(first + second)
        self.assertEqual(struct.unpack('<56h', decoded.pcm), a.samples + b.samples)
        self.assertEqual(decoded.history, b.history)
        self.assertNotEqual(b.samples, decode_block(second).samples)

    def test_end_block_is_included_and_trailing_padding_is_reported(self):
        data = block(12, 1, 0x21) + block(0, 7, 0xFF)
        decoded = decode_stream(data)
        self.assertEqual(struct.unpack('<28h', decoded.pcm), (1, 2) * 14)
        self.assertEqual(decoded.info.consumed_bytes, 16)
        self.assertEqual(decoded.info.trailing_bytes, 16)
        self.assertTrue(decoded.ended)

    def test_repeat_bit_without_end_does_not_jump(self):
        decoded = decode_stream(block(12, 2, 0x11) + block(12, 1, 0x22), repeats=10)
        self.assertEqual(decoded.blocks, 2)
        self.assertEqual(decoded.repeats_done, 0)

    def test_loop_jumps_keep_predictor_history(self):
        data = block(0x18, 4, 0x11) + block(0x18, 3, 0x11)
        first = decode_stream(data)
        repeated = decode_stream(data, repeats=1)
        second = decode_stream(data, history=first.history)
        self.assertEqual(repeated.pcm, first.pcm + second.pcm)
        self.assertNotEqual(repeated.pcm, first.pcm * 2)
        self.assertEqual(repeated.blocks, 4)
        self.assertEqual(repeated.repeats_done, 1)
        self.assertEqual(repeated.info.loop_start_byte, 0)

    def test_external_loop_required_only_when_following_repeat(self):
        data = block(12, 3, 0x21)
        self.assertTrue(inspect_stream(data).external_loop_required)
        self.assertEqual(decode_stream(data).blocks, 1)
        with self.assertRaises(AdpcmError):
            decode_stream(data, repeats=1)
        self.assertEqual(decode_stream(data, repeats=2, initial_loop_byte=0).blocks, 3)

    def test_latest_start_marker_replaces_previous_repeat_address(self):
        data = block(12, 4, 0x11) + block(12, 4, 0x22) + block(12, 3, 0x33)
        decoded = decode_stream(data, repeats=1)
        self.assertEqual(decoded.blocks, 5)
        self.assertEqual(decoded.info.loop_start_sample, 28)
        self.assertEqual(struct.unpack('<140h', decoded.pcm)[84:112], (2,) * 28)

    def test_truncated_empty_or_unterminated_streams_are_rejected(self):
        for payload in (b'', bytes(15), bytes(17), block()):
            with self.assertRaises(AdpcmError):
                decode_stream(payload)
        self.assertFalse(decode_stream(block(), require_end=False).ended)
        for payload in (b'', bytes(15), bytes(17)):
            with self.assertRaises(AdpcmError):
                decode_block(payload)

    def test_budgets_addresses_and_initial_history_are_validated(self):
        for kwargs in ({'max_samples': 27}, {'max_samples': -1}, {'repeats': -1},
                       {'initial_loop_byte': -16}, {'initial_loop_byte': 1},
                       {'initial_loop_byte': 16}):
            with self.assertRaises(AdpcmError):
                decode_stream(block(12, 3), **kwargs)
        with self.assertRaises(AdpcmError):
            decode_stream(block(12, 7), repeats=100000, max_samples=100)
        for values in ((32768, 0), (0, -32769), (1.5, 0)):
            with self.assertRaises(AdpcmError):
                History(*values)


if __name__ == '__main__':
    unittest.main()
