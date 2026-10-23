"""Export original GOOL SPRITE/QUAD_LIST frame UVs and pixels for all archives."""
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image, ImageDraw

import cb3
import tpag
from export_object_graphics import discover_pairs
from graphics_texture_pool import build_pool, resolve

ROOT = Path(__file__).resolve().parents[1]


def main():
    output = ROOT / "game_data/objects"
    texture_dir = output / "textures"
    texture_dir.mkdir(parents=True, exist_ok=True)
    contacts = ROOT / "capturas/objects"
    contacts.mkdir(parents=True, exist_ok=True)
    manifest = {"schema":"cbwarped-sprite-inventory/1","runtime_integrated":False,"levels":[]}
    cache = {}
    pool = build_pool()
    for level_id in cb3.all_level_ids():
        archive = cb3.load_level(level_id, cache=False)
        _, errors, declarations = discover_pairs(archive)
        records = []
        thumbnails = []
        for declaration in declarations:
            if declaration["kind"] not in (2,5):
                continue
            record = dict(declaration)
            raw = archive.entry(record["gool"]).items[5]
            page_data, provenance = resolve(pool,archive,record["texture"])
            record["texture_provenance"] = provenance
            record["regions"] = []
            stride = 16 if record["kind"] == 2 else 24
            offset = record["offset"] + (8 if record["kind"] == 2 else 12)
            for frame in range(record["frames"]):
                for rectangle in range(record["rectangles"]):
                    region = {"frame":frame,"rectangle":rectangle,"color":list(raw[offset:offset+3]),
                              "code":raw[offset+3],"uv":tpag.parse_uv(raw, offset+4)}
                    if record["kind"] == 5:
                        region["xy_rect"] = list(struct.unpack_from("<4h",raw,offset+16))
                    region["texture_provenance"] = provenance
                    if page_data is None:
                        region["error"] = provenance["resolution"]
                    else:
                        key = (provenance["sha256"], raw[offset+4:offset+16])
                        try:
                            if key not in cache:
                                image = tpag.decode_texture(page_data,region["uv"])
                                identity = hashlib.sha256(struct.pack("<II",image.width,image.height)+image.rgba).hexdigest()
                                path = texture_dir / (identity + ".png")
                                if not path.exists():
                                    Image.frombytes("RGBA",(image.width,image.height),image.rgba).save(path)
                                cache[key] = {"file":"textures/"+path.name,"width":image.width,"height":image.height,
                                              "blend":image.blend}
                            region.update(cache[key])
                            if frame == 0 and rectangle == 0:
                                thumbnails.append((record["gool"],record["offset"],output/region["file"]))
                        except (ValueError,IndexError,struct.error) as error:
                            region["error"] = str(error)
                    record["regions"].append(region)
                    offset += stride
            records.append(record)
        manifest["levels"].append({"id":level_id,"records":records,"gool_errors":errors})
        if thumbnails:
            width = 1024
            sheet = Image.new("RGB",(width,((len(thumbnails)+7)//8)*128),(12,16,24))
            draw = ImageDraw.Draw(sheet)
            for index,(name,offset,path) in enumerate(thumbnails):
                x,y = (index%8)*128,(index//8)*128
                with Image.open(path) as image:
                    image = image.convert("RGBA")
                    image.thumbnail((108,104),Image.Resampling.NEAREST)
                    sheet.paste(image,(x+10,y),image)
                draw.text((x+4,y+107),"%s:%X"%(name,offset),fill=(230,230,230))
            sheet.save(contacts/("sprites_%02X.png"%level_id))
        print("sprites level%02X: %d declarations"%(level_id,len(records)),flush=True)
    records = [record for level in manifest["levels"] for record in level["records"]]
    regions = [region for record in records for region in record["regions"]]
    manifest["totals"] = {"archives":len(manifest["levels"]),"declarations":len(records),
        "frame_regions":len(regions),"decoded_regions":sum("file" in region for region in regions),
        "unresolved_regions":sum("error" in region for region in regions),
        "unique_global_regions":sum("file" in region and region["texture_provenance"]["resolution"]=="unique_global_payload" for region in regions),
        "absent_from_disc_regions":sum(region.get("error")=="absent_from_disc" for region in regions),
        "ambiguous_name_regions":sum(region.get("error")=="ambiguous_global_name" for region in regions)}
    (output/"sprite_manifest.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    print(json.dumps(manifest["totals"],indent=2))


if __name__ == "__main__":
    main()
