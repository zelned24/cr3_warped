"""Resolve absent local TPAG names only when the supplied disc is unambiguous."""
import hashlib

import cb3


def build_pool():
    pool = {}
    for level_id in cb3.all_level_ids():
        archive = cb3.load_level(level_id, cache=False)
        for page in archive.texture_pages:
            identity = hashlib.sha256(page.data).hexdigest()
            variant = pool.setdefault(page.name, {}).setdefault(identity, {"data":page.data,"source_levels":[]})
            variant["source_levels"].append(level_id)
    return pool


def resolve(pool, archive, page_name):
    local = archive.texture(page_name)
    if local is not None:
        return local.data, {"resolution":"local_archive","source_level":archive.level_id,
                            "source_levels":[archive.level_id],"sha256":hashlib.sha256(local.data).hexdigest()}
    variants = pool.get(page_name,{})
    if len(variants) == 1:
        identity, variant = next(iter(variants.items()))
        return variant["data"], {"resolution":"unique_global_payload","source_level":variant["source_levels"][0],
                                "source_levels":variant["source_levels"],"sha256":identity}
    return None, {"resolution":"absent_from_disc" if not variants else "ambiguous_global_name",
                  "page":page_name,"distinct_payloads":len(variants),"source_level":None}
