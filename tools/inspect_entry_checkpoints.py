"""Read existing CBWB/LMD1 entry identities without generating assets or code.

The unit tests compare these bytes against raw NSD records. This tool
never infers facing/camera orientation from the opaque checkpoint record index.
"""
import argparse
import json
from pathlib import Path
import struct


def checkpoints(blob):
    if len(blob) < 16 or blob[:4] != b"CBWB":
        raise ValueError("Invalid CBWB header")
    count = struct.unpack_from("<I", blob, 8)[0]
    if count > 64 or 16 + count * 12 > len(blob):
        raise ValueError("Invalid CBWB directory")
    result = []
    for i in range(count):
        lid, offset, size = struct.unpack_from("<III", blob, 16 + i * 12)
        if offset < 16 + count * 12 or size < 20 or offset + size > len(blob):
            raise ValueError("Invalid level record")
        data = blob[offset:offset + size]
        magic, inner_id, models, colors, metadata = struct.unpack_from("<4sIIII", data)
        if magic != b"CBW3" or inner_id != lid:
            raise ValueError("Invalid CBW3 header")
        row = {"level": lid, "has_checkpoint": False}
        if metadata:
            if metadata < 20 or metadata + 36 + models * 4 + colors * 4 != size:
                raise ValueError("Invalid LMD1 bounds")
            tag, valid, x, y, z, eid, reserved, index, model_count = struct.unpack_from("<4sIiiiIIII", data, metadata)
            if tag != b"LMD1" or valid not in (0, 1) or model_count != models:
                raise ValueError("Invalid LMD1 header")
            if valid:
                row.update(has_checkpoint=True, raw_position=[x, y, z],
                           position=[v / 4096 for v in (x, y, z)],
                           zdat_eid=eid, reserved=reserved, record_index=index)
        result.append(row)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("blob", type=Path)
    args = parser.parse_args()
    print(json.dumps(checkpoints(args.blob.read_bytes()), indent=2))


if __name__ == "__main__":
    main()
