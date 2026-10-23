"""Retain exact native voice windows and identity; no execution/decompilation."""
import hashlib
import json
from pathlib import Path
import cb3
from psx_native import Image, ORIGINAL_SHA256


def evidence(image):
    if image.sha256 != ORIGINAL_SHA256:
        raise ValueError("Not the audited SCUS-94244 executable")
    windows = {
        "opcode_dispatch": (0x800398b8, 0x800398d0),
        "service_bridge": (0x80038dd8, 0x80038df8),
        "play_wrapper": (0x8003780c, 0x800378f8),
        "control_wrapper": (0x800378f8, 0x80037a00),
        "template_reset": (0x80033050, 0x8003314c),
        "sample_resolution": (0x80033760, 0x80033864),
        "voice_start": (0x80033864, 0x80033c54),
        "voice_control": (0x80033c54, 0x800342b8),
        "control_selector_table": (0x8001060c, 0x80010648),
    }
    return {
        "schema": "psx-audio-voice-native/1", "analysis": "offline-original-byte-windows",
        "source_sha256": image.sha256, "file_size": image.file_size,
        "payload_sha256": hashlib.sha256(image.data).hexdigest(),
        "load": image.load, "entry": image.entry,
        "dispatch_table": {hex(op): image.word(0x8005d120+op*4) for op in (0x41, 0x42)},
        "windows": {name: {"start": lo, "end": hi,
                            "file_offset": 2048+lo-image.load,
                            "hex": image.read(lo, hi-lo).hex()}
                    for name, (lo, hi) in windows.items()},
        "derived_contract": {
            "play": {"operand_order": ["upper-address", "lower-address"],
                     "volume": "signed16-at-upper-address",
                     "backend": 0x80033864, "return_store_object_offset": 0x12c},
            "control": {"operand_order": ["lower-address", "target-member-value"],
                        "target_zero": "template", "target_31": "pop-after-source",
                        "selector": "bits20..23;15 becomes selector0 plus0x80000000",
                        "option_bits": {"18": "0x40000000", "19": "0x20000000"},
                        "backend": 0x80033c54},
            "template": {"address": 0x8006cf78, "bytes": 76,
                         "default_volume": 0x3fff, "default_pitch": 0x1000,
                         "unsupported_services": ["active-voice-selection", "selector2-world-transform",
                                                  "selector14-bank-query"]}},
        "validation": "tests/test_audio_voice_native.py replays original wrappers and template controls",
        "limitations": ["Fixture replay does not emulate SPU, GTE, timing, or whole-game reachability.",
                        "Spatial mode is supplied at native0x80033000 boundary.",
                        "No fabricated event mapping, pitch conversion, ADSR, streaming or NDSP integration."]}


if __name__ == "__main__":
    report = evidence(Image.parse((Path(cb3.ISO) / "SCUS_942.44").read_bytes()))
    output = Path(__file__).resolve().parents[1] / "docs/evidence/psx_audio_voice_native.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, separators=(",", ":")) + "\n", encoding="utf-8")
    print("Native audio voice evidence: identity verified; 9 exact original windows retained")
