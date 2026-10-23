/* Crash Bandicoot: Warped (PSX) -> 3DS port - level data handling.
 *
 * The level geometry is decoded on the host (tools/export_levels.py) out of
 * Naughty Dog's NSF archives and stored in a compact blob that is embedded
 * into the .3dsx (or, in the LEVELS_FROM_SD build, loaded from the SD card
 * at start-up - see source/level_sd.c):
 *
 *   'CBW3' u32 id, u32 modelCount, u32 paletteCount, u32 metadataOffset
 *   per model:
 *       i32 originX, originY, originZ      (raw; divide by 16)
 *       u32 vcount, tcount, qcount
 *       vertices : i16 x, i16 y, i16 z, u16 colourIndex
 *       triangles: u16 a, u16 b, u16 c, u16 flags
 *       quads    : u16 a, u16 b, u16 c, u16 d, u16 flags
 *   optional LMD1 metadata: original NSD checkpoint and model sky flags
 *   palette   : u8 r, u8 g, u8 b, u8 pad
 *
 * A level is made of world models. The current implementation expands all
 * of them into a linear-RAM GPU buffer and culls models at draw time.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <3ds.h>
#include <citro3d.h>

#include "level.h"
#include "render_frustum.h"
#include "level_sd.h"
#include "levels_bin.h"

#define MAX_MODELS    160

/* CBWB records are packed: a ten-byte quad can put the next model header
 * at address 2 mod 4.  Read bytes explicitly rather than dereferencing
 * misaligned integer pointers (undefined in C, unsafe on ARM11). */
static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_i32(const uint8_t *p)
{
    uint32_t v = read_u32(p);
    return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX - v);
}

static int16_t read_i16(const uint8_t *p)
{
    uint16_t v = read_u16(p);
    return v <= INT16_MAX ? (int16_t)v : (int16_t)(-1 - (int32_t)(UINT16_MAX - v));
}

/* ------------------------------------------------------------------ */
/* active blob                                                         */
/* ------------------------------------------------------------------ */

/* The level blob can live in two different places:
 *
 *   - embedded in the .3dsx (default build, source/levels_bin.s uses
 *     .incbin on game_data/levels.bin),
 *   - in a file on the SD card (source/level_sd.c).
 *
 * The SD copy is preferred when it is present and valid.  In the
 * LEVELS_FROM_SD build the embedded copy is not linked into the
 * executable at all (the Makefile leaves build/levels_bin.o out), so the
 * file on the SD card is the only possible source.
 *
 * level_blob_ptr() hides that difference from the rest of this file; the
 * blob format, the world scale and the physics are identical in both
 * cases.
 */
static const uint8_t *level_blob_ptr(uint32_t *out_size)
{
    if (level_sd_available()) {
        *out_size = level_sd_blob_size();
        return level_sd_blob();
    }
#ifdef LEVELS_FROM_SD
    /* this build has no embedded copy */
    *out_size = 0;
    return NULL;
#else
    *out_size = levels_bin_size;
    return (const uint8_t *)levels_bin;
#endif
}

/* ------------------------------------------------------------------ */
/* blob directory                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t id;
    uint32_t offset;
    uint32_t size;
} blob_entry;

static blob_entry s_entries[64];
static int s_entry_count;
static const uint8_t *s_parsed_blob;  /* blob the directory was parsed from */
static uint32_t s_parsed_size;

static void parse_blob(void)
{
    uint32_t size = 0;
    const uint8_t *blob = level_blob_ptr(&size);
    if (!blob || size < 16 || memcmp(blob, "CBWB", 4) != 0) {
        s_entry_count = 0;
        s_parsed_blob = NULL;
        s_parsed_size = 0;
        return;
    }
    /* the directory is parsed once per blob; it is parsed again when the
     * active blob changes (embedded -> SD card copy) */
    if (s_entry_count && blob == s_parsed_blob && size == s_parsed_size)
        return;
    s_parsed_blob = blob;
    s_parsed_size = size;
    s_entry_count = 0;

    uint32_t count = read_u32(blob + 8);
    if (count > 64)
        count = 64;
    /* the directory has to fit into the blob */
    if ((uint64_t)16 + (uint64_t)count * 12 > (uint64_t)size)
        count = (uint32_t)((size - 16) / 12);
    uint32_t valid = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *p = blob + 16 + i * 12;
        s_entries[i].id     = read_u32(p);
        s_entries[i].offset = read_u32(p + 4);
        s_entries[i].size   = read_u32(p + 8);
        /* every level must live inside the blob (no read past the end) */
        if (s_entries[i].offset < 16 || s_entries[i].size < 20 ||
            (uint64_t)s_entries[i].offset + (uint64_t)s_entries[i].size > (uint64_t)size)
            break;
        valid++;
    }
    s_entry_count = (int)valid;
}

int level_count(void)
{
    parse_blob();
    return s_entry_count;
}

const level_info *level_get(int index)
{
    static level_info info;
    parse_blob();
    if (index < 0 || index >= s_entry_count)
        return NULL;
    uint32_t size = 0;
    const uint8_t *blob = level_blob_ptr(&size);
    if (!blob)
        return NULL;
    info.id   = s_entries[index].id;
    info.data = blob + s_entries[index].offset;
    info.size = s_entries[index].size;
    return &info;
}

int level_find(uint32_t id)
{
    parse_blob();
    for (int i = 0; i < s_entry_count; i++)
        if (s_entries[i].id == id)
            return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* level state                                                         */
/* ------------------------------------------------------------------ */

static const uint8_t *s_level;       /* current level blob                  */
static const uint8_t *s_palette;
static uint32_t s_palette_n;
static int s_spawn_valid;
static float s_disc_spawn[3];
static uint32_t s_spawn_zdat_eid, s_spawn_record_index;

typedef struct {
    uint32_t offset;                 /* record offset inside the blob */
    uint32_t vcount, tcount, qcount;
    uint32_t first_vert;             /* offset in s_vbo */
    uint32_t vert_count;             /* vertices belonging to this model */
    float ox, oy, oz;                /* origin, cm                     */
    float minx, miny, minz;
    float maxx, maxy, maxz;
    int is_skybox;
} model_rec;

static model_rec s_models[MAX_MODELS];
static int s_model_count;

static level_vertex *s_vbo;          /* expanded geometry, in linear RAM  */
static uint32_t s_vbo_verts;

static int s_zone = -1;
static int s_zone_switches = 0;
static int s_alloc_fail = 0;

/* ------------------------------------------------------------------ */

static float decode_axis(int16_t v)
{
    return (float)v;
}

static void model_scan(model_rec *m, const uint8_t *mp)
{
    m->offset = (uint32_t)(mp - s_level);
    m->ox = (float)read_i32(mp + 0) / 16.0f;
    m->oy = (float)read_i32(mp + 4) / 16.0f;
    m->oz = (float)read_i32(mp + 8) / 16.0f;
    m->vcount = read_u32(mp + 12);
    m->tcount = read_u32(mp + 16);
    m->qcount = read_u32(mp + 20);
    m->minx = m->miny = m->minz = 1e30f;
    m->maxx = m->maxy = m->maxz = -1e30f;

    const uint8_t *v = mp + 24;
    for (uint32_t i = 0; i < m->vcount; i++) {
        int16_t x = read_i16(v + i * 8 + 0);
        int16_t y = read_i16(v + i * 8 + 2);
        int16_t z = read_i16(v + i * 8 + 4);
        float wx = m->ox + decode_axis(x);
        float wy = m->oy + decode_axis(y);
        float wz = m->oz + decode_axis(z);
        if (wx < m->minx) m->minx = wx;
        if (wy < m->miny) m->miny = wy;
        if (wz < m->minz) m->minz = wz;
        if (wx > m->maxx) m->maxx = wx;
        if (wy > m->maxy) m->maxy = wy;
        if (wz > m->maxz) m->maxz = wz;
    }
}

/* Decode all models into a contiguous linear-RAM buffer. */
static void level_build_all(level_vertex *buffer)
{
    if (s_vbo) linearFree(s_vbo);
    s_vbo = buffer;
    s_vbo_verts = 0;

    uint32_t n = 0;
    level_vertex *out = s_vbo;

    for (int mi = 0; mi < s_model_count; mi++) {
        model_rec *m = &s_models[mi];
        m->first_vert = n;
        const uint8_t *mp = s_level + m->offset;
        const uint8_t *vdata = mp + 24;
        const uint8_t *tdata = vdata + m->vcount * 8;
        const uint8_t *qdata = tdata + m->tcount * 8;

        /* triangles */
        for (uint32_t i = 0; i < m->tcount; i++) {
            uint16_t idx[3];
            for (int k = 0; k < 3; k++)
                idx[k] = read_u16(tdata + i * 8 + k * 2);
            if (idx[0] >= m->vcount || idx[1] >= m->vcount || idx[2] >= m->vcount)
                continue;
            for (int k = 0; k < 3; k++) {
                const uint8_t *vp = vdata + idx[k] * 8;
                float px = m->ox + decode_axis(read_i16(vp + 0));
                float py = m->oy + decode_axis(read_i16(vp + 2));
                float pz = m->oz + decode_axis(read_i16(vp + 4));
                uint16_t ci = read_u16(vp + 6);
                if (ci >= s_palette_n)
                    ci = 0;
                out[n].x = px; out[n].y = py; out[n].z = pz;
                out[n].r = s_palette[ci * 4 + 0] / 255.0f;
                out[n].g = s_palette[ci * 4 + 1] / 255.0f;
                out[n].b = s_palette[ci * 4 + 2] / 255.0f;
                n++;
            }
        }

        /* quads -> two triangles */
        for (uint32_t i = 0; i < m->qcount; i++) {
            uint16_t idx[4];
            for (int k = 0; k < 4; k++)
                idx[k] = read_u16(qdata + i * 10 + k * 2);
            if (idx[0] >= m->vcount || idx[1] >= m->vcount ||
                idx[2] >= m->vcount || idx[3] >= m->vcount)
                continue;
            level_vertex qv[4];
            for (int k = 0; k < 4; k++) {
                const uint8_t *vp = vdata + idx[k] * 8;
                qv[k].x = m->ox + decode_axis(read_i16(vp + 0));
                qv[k].y = m->oy + decode_axis(read_i16(vp + 2));
                qv[k].z = m->oz + decode_axis(read_i16(vp + 4));
                uint16_t ci = read_u16(vp + 6);
                if (ci >= s_palette_n)
                    ci = 0;
                qv[k].r = s_palette[ci * 4 + 0] / 255.0f;
                qv[k].g = s_palette[ci * 4 + 1] / 255.0f;
                qv[k].b = s_palette[ci * 4 + 2] / 255.0f;
            }

            /* Triangle 1: (0, 1, 2) */
            out[n++] = qv[0];
            out[n++] = qv[1];
            out[n++] = qv[2];

            /* Triangle 2: (2, 1, 3) */
            out[n++] = qv[2];
            out[n++] = qv[1];
            out[n++] = qv[3];
        }
        m->vert_count = n - m->first_vert;
    }

    s_vbo_verts = n;
    s_zone = 0;
}

int level_load(uint32_t id)
{
    int idx = level_find(id);
    if (idx < 0)
        return -1;
    const level_info *info = level_get(idx);
    if (!info || info->size < 20 || memcmp(info->data, "CBW3", 4) != 0)
        return -1;

    const uint8_t *data = info->data;
    uint32_t palette_n = read_u32(data + 12);
    uint32_t model_count = read_u32(data + 8);
    if (read_u32(data + 4) != id)
        return -1;
    if (!palette_n || model_count == 0 || model_count > MAX_MODELS ||
        (uint64_t)palette_n * 4 + 20 > info->size)
        return -1;
    uint32_t palette_offset = info->size - palette_n * 4;
    uint32_t metadata_offset = read_u32(data + 16);
    uint32_t geometry_end = metadata_offset ? metadata_offset : palette_offset;
    if (geometry_end < 20 || geometry_end > palette_offset)
        return -1;
    const uint8_t *metadata = metadata_offset ? data + metadata_offset : NULL;
    if (metadata &&
        (palette_offset - metadata_offset != 36 + model_count * 4 ||
         memcmp(metadata, "LMD1", 4) || read_u32(metadata + 4) > 1 ||
         read_u32(metadata + 32) != model_count))
        return -1;
    if (metadata)
        for (uint32_t i = 0; i < model_count; i++)
            if (read_u32(metadata + 36 + i * 4) > 1)
                return -1;

    /* Validate every record before model_scan touches its vertices. Malformed
     * input leaves the previous level and its GPU buffer usable. */
    uint32_t offset = 20;
    uint64_t expanded_verts = 0;
    for (uint32_t i = 0; i < model_count; i++) {
        if (offset > geometry_end || geometry_end - offset < 24)
            return -1;
        const uint8_t *mp = data + offset;
        uint32_t vc = read_u32(mp + 12);
        uint32_t tc = read_u32(mp + 16);
        uint32_t qc = read_u32(mp + 20);
        uint64_t bytes = 24ull + (uint64_t)vc * 8 +
                         (uint64_t)tc * 8 + (uint64_t)qc * 10;
        if (vc > 65536 || bytes > geometry_end - offset)
            return -1;
        expanded_verts += (uint64_t)tc * 3 + (uint64_t)qc * 6;
        offset += (uint32_t)bytes;
    }
    if (offset != geometry_end || !expanded_verts ||
        expanded_verts > SIZE_MAX / sizeof(level_vertex))
        return -1;
    level_vertex *buffer = linearAlloc((size_t)expanded_verts * sizeof(level_vertex));
    if (!buffer) {
        s_alloc_fail++;
        return -1;
    }

    s_level = data;
    s_palette_n = palette_n;
    s_palette = data + palette_offset;
    s_model_count = (int)model_count;
    s_spawn_valid = metadata && read_u32(metadata + 4);
    for (int axis = 0; axis < 3; axis++)
        s_disc_spawn[axis] = s_spawn_valid ?
            (float)read_i32(metadata + 8 + axis * 4) / 4096.0f : 0.0f;
    s_spawn_zdat_eid = s_spawn_valid ? read_u32(metadata + 20) : 0;
    s_spawn_record_index = s_spawn_valid ? read_u32(metadata + 28) : 0;
    offset = 20;
    for (uint32_t i = 0; i < model_count; i++) {
        model_rec *m = &s_models[i];
        model_scan(m, data + offset);
        m->is_skybox = metadata ? (int)read_u32(metadata + 36 + i * 4) : 0;
        offset += 24 + m->vcount * 8 + m->tcount * 8 + m->qcount * 10;
    }

    level_build_all(buffer);
    return 0;
}

int level_activate_zone(int index)
{
    if (!s_vbo || index < 0 || index >= s_model_count)
        return -1;
    if (s_zone != index) {
        s_zone = index;
        s_zone_switches++;
    }
    return 0;
}

void level_unload(void)
{
    if (s_vbo) { linearFree(s_vbo); s_vbo = NULL; }
    s_vbo_verts = 0;
    s_model_count = 0;
    s_spawn_valid = 0;
    s_spawn_zdat_eid = s_spawn_record_index = 0;
    s_zone = -1;
}

int level_vertex_count(void)
{
    return (int)s_vbo_verts;
}

const level_vertex *level_vertices(void)
{
    return s_vbo;
}

int level_model_count(void)
{
    return s_model_count;
}

const level_model_info *level_model(int index)
{
    static level_model_info info;
    if (index < 0 || index >= s_model_count)
        return NULL;
    const model_rec *m = &s_models[index];
    info.first = m->first_vert;
    info.count = m->vert_count;
    info.ox = m->ox; info.oy = m->oy; info.oz = m->oz;
    info.minx = m->minx; info.miny = m->miny; info.minz = m->minz;
    info.maxx = m->maxx; info.maxy = m->maxy; info.maxz = m->maxz;
    info.is_skybox = m->is_skybox;
    return &info;
}

int level_render_visible(float px, float py, float pz, float radius)
{
    return level_render_visible_frustum(px,py,pz,radius,NULL);
}

int level_render_visible_frustum(float px,float py,float pz,float radius,
                                 const double clip[4][4])
{
    if (!s_vbo || s_model_count <= 0)
        return 0;

    float r2 = radius * radius;
    int drawn = 0;

    for (int i = 0; i < s_model_count; i++) {
        const model_rec *m = &s_models[i];
        if (m->vert_count == 0 || m->is_skybox)
            continue;

        /* Closest point on model bounding box to (px, py, pz) */
        float cx = px < m->minx ? m->minx : (px > m->maxx ? m->maxx : px);
        float cy = py < m->miny ? m->miny : (py > m->maxy ? m->maxy : py);
        float cz = pz < m->minz ? m->minz : (pz > m->maxz ? m->maxz : pz);
        float dx = px - cx, dy = py - cy, dz = pz - cz;

        if (dx * dx + dy * dy + dz * dz <= r2) {
            if (clip) {
                float low[3]={m->minx,m->miny,m->minz};
                float high[3]={m->maxx,m->maxy,m->maxz};
                if (!render_frustum_visible(clip,low,high)) continue;
            }
            C3D_DrawArrays(GPU_TRIANGLES, m->first_vert, m->vert_count);
            drawn += m->vert_count;
        }
    }

    return drawn;
}

int level_model_at(float x, float z)
{
    for (int i = 0; i < s_model_count; i++) {
        const model_rec *m = &s_models[i];
        if (m->is_skybox)
            continue;
        if (x >= m->minx && x <= m->maxx && z >= m->minz && z <= m->maxz)
            return i;
    }
    return -1;
}

float level_ground_height(float x, float z, float max_y)
{
    if (!s_vbo || s_model_count <= 0 ||
        !isfinite(x) || !isfinite(z) || !isfinite(max_y))
        return -1000000;

    float best_y = -1000000.0f;

    for (int mi = 0; mi < s_model_count; mi++) {
        const model_rec *m = &s_models[mi];
        if (m->vert_count == 0 || m->is_skybox)
            continue;

        /* Quick check against model bounding box with 64cm margin */
        if (x < m->minx - 64.0f || x > m->maxx + 64.0f ||
            z < m->minz - 64.0f || z > m->maxz + 64.0f)
            continue;

        for (uint32_t vi = m->first_vert; vi + 2 < m->first_vert + m->vert_count; vi += 3) {
            const level_vertex *v0 = &s_vbo[vi + 0];
            const level_vertex *v1 = &s_vbo[vi + 1];
            const level_vertex *v2 = &s_vbo[vi + 2];

            /* Fast triangle bounding box rejection */
            float tminx = v0->x < v1->x ? (v0->x < v2->x ? v0->x : v2->x) : (v1->x < v2->x ? v1->x : v2->x);
            float tmaxx = v0->x > v1->x ? (v0->x > v2->x ? v0->x : v2->x) : (v1->x > v2->x ? v1->x : v2->x);
            if (x < tminx - 4.0f || x > tmaxx + 4.0f)
                continue;

            float tminz = v0->z < v1->z ? (v0->z < v2->z ? v0->z : v2->z) : (v1->z < v2->z ? v1->z : v2->z);
            float tmaxz = v0->z > v1->z ? (v0->z > v2->z ? v0->z : v2->z) : (v1->z > v2->z ? v1->z : v2->z);
            if (z < tminz - 4.0f || z > tmaxz + 4.0f)
                continue;

            /* 2D cross products for barycentric coordinates on XZ plane */
            float d0 = (v1->x - v0->x) * (z - v0->z) - (v1->z - v0->z) * (x - v0->x);
            float d1 = (v2->x - v1->x) * (z - v1->z) - (v2->z - v1->z) * (x - v1->x);
            float d2 = (v0->x - v2->x) * (z - v2->z) - (v0->z - v2->z) * (x - v2->x);

            int has_neg = (d0 < -0.05f) || (d1 < -0.05f) || (d2 < -0.05f);
            int has_pos = (d0 > 0.05f) || (d1 > 0.05f) || (d2 > 0.05f);
            if (has_neg && has_pos)
                continue;

            /* Triangle plane normal */
            float e1x = v1->x - v0->x, e1y = v1->y - v0->y, e1z = v1->z - v0->z;
            float e2x = v2->x - v0->x, e2y = v2->y - v0->y, e2z = v2->z - v0->z;
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;

            float nlen2 = nx * nx + ny * ny + nz * nz;
            if (nlen2 < 1e-6f)
                continue;
            float nlen = sqrtf(nlen2);
            if (fabsf(ny) / nlen < 0.25f) /* not a vertical wall */
                continue;

            /* Height on plane: nx*(x - v0->x) + ny*(y - v0->y) + nz*(z - v0->z) = 0 */
            float y = v0->y - (nx * (x - v0->x) + nz * (z - v0->z)) / ny;
            if (y <= max_y && y > best_y)
                best_y = y;
        }
    }

    return best_y;
}

int level_height_below(float x, float z, float max_y)
{
    float y = level_ground_height(x, z, max_y);
    return y > -999999.0f ? (int)roundf(y) : -1000000;
}

typedef struct { float x, y, z; } collision_point;

/* Clip to the body's vertical interval before projecting onto XZ. This avoids
 * treating tall wall triangles as infinitely tall or blocking under bridges. */
static int clip_height(const collision_point *in, int count,
                       collision_point *out, float height, int above)
{
    int n = 0;
    for (int i = 0; i < count; i++) {
        collision_point a = in[i], b = in[(i + 1) % count];
        int ia = above ? a.y >= height : a.y <= height;
        int ib = above ? b.y >= height : b.y <= height;
        if (ia) out[n++] = a;
        if (ia != ib) {
            float t = (height - a.y) / (b.y - a.y);
            collision_point p = {a.x + t * (b.x-a.x), height,
                                 a.z + t * (b.z-a.z)};
            out[n++] = p;
        }
    }
    return n;
}

static int resolve_walls(float *x, float *z, float old_x, float old_z,
                         float low, float high, float radius)
{
    int hit = 0;
    for (int mi = 0; mi < s_model_count; mi++) {
        const model_rec *m = &s_models[mi];
        if (m->is_skybox || *x < m->minx-radius || *x > m->maxx+radius ||
            *z < m->minz-radius || *z > m->maxz+radius ||
            high < m->miny || low > m->maxy) continue;
        for (uint32_t vi=m->first_vert; vi+2<m->first_vert+m->vert_count; vi+=3) {
            const level_vertex *v = s_vbo + vi;
            float ax=v[1].x-v[0].x, ay=v[1].y-v[0].y, az=v[1].z-v[0].z;
            float bx=v[2].x-v[0].x, by=v[2].y-v[0].y, bz=v[2].z-v[0].z;
            float nx=ay*bz-az*by, ny=az*bx-ax*bz, nz=ax*by-ay*bx;
            float n2=nx*nx+ny*ny+nz*nz;
            /* Complement of the floor-query slope test: surfaces with less
             * than 1/4 vertical normal component are walls, regardless of winding. */
            if (n2 < 1e-6f || ny*ny >= n2*0.0625f) continue;
            collision_point p[8], q[8];
            for(int j=0;j<3;j++) {
                p[j].x=v[j].x; p[j].y=v[j].y; p[j].z=v[j].z;
            }
            int count=clip_height(p,3,q,low,1);
            if(count<2) continue;
            count=clip_height(q,count,p,high,0);
            if(count<2) continue;
            float best=FLT_MAX, cx=0, cz=0, edge_x=0, edge_z=0;
            int pos=0, neg=0;
            for(int j=0;j<count;j++) {
                collision_point a=p[j], b=p[(j+1)%count];
                float ex=b.x-a.x, ez=b.z-a.z;
                float side=ex*(*z-a.z)-ez*(*x-a.x);
                if(side>0.001f) pos=1;
                if(side< -0.001f) neg=1;
                float len=ex*ex+ez*ez;
                if(len<1e-8f) continue;
                float t=((*x-a.x)*ex+(*z-a.z)*ez)/len;
                if(t<0) t=0;
                if(t>1) t=1;
                float px=a.x+t*ex, pz=a.z+t*ez;
                float dd=(*x-px)*(*x-px)+(*z-pz)*(*z-pz);
                if(dd<best) { best=dd; cx=px; cz=pz; edge_x=ex; edge_z=ez; }
            }
            if(best==FLT_MAX) continue;
            /* Polygon area distinguishes a vertical line from a projected
             * sloping wall. For points inside, move out through nearest edge. */
            float area=0;
            for(int j=0;j<count;j++)
                area+=p[j].x*p[(j+1)%count].z-p[(j+1)%count].x*p[j].z;
            int inside=fabsf(area)>0.01f && !(pos && neg);
            if(!inside && best>=radius*radius) continue;
            float vx=*x-cx, vz=*z-cz, len=sqrtf(best);
            if(inside) { vx=-vx; vz=-vz; }
            if(len<0.0001f) {
                vx=-edge_z; vz=edge_x;
                len=sqrtf(vx*vx+vz*vz);
                if(vx*(old_x-cx)+vz*(old_z-cz)<0) {vx=-vx;vz=-vz;}
            }
            if(len<0.0001f) continue;
            *x=cx+vx/len*(radius+0.01f);
            *z=cz+vz/len*(radius+0.01f);
            hit=1;
        }
    }
    return hit;
}

int level_move_horizontal(float *x, float *z, float foot_y,
                          float dx, float dz, float radius,
                          float body_height, float step_height)
{
    if(!x || !z || !s_vbo || !isfinite(*x) || !isfinite(*z) ||
       !isfinite(foot_y) || !isfinite(dx) || !isfinite(dz) ||
       !isfinite(radius) || !isfinite(body_height) || !isfinite(step_height) ||
       radius<=0 || body_height<=step_height || step_height<0) return 0;
    float distance=sqrtf(dx*dx+dz*dz);
    /* Bound work for corrupt input rather than permit an unbounded loop. */
    if(!isfinite(distance) || distance>10000.0f) return 0;
    int steps=(int)ceilf(distance/fmaxf(radius*0.45f,0.5f));
    if(steps<1) steps=1;
    int hit=0;
    for(int s=0;s<steps;s++) {
        float old_x=*x, old_z=*z;
        *x+=dx/steps; *z+=dz/steps;
        for(int pass=0;pass<3;pass++) {
            int contact=resolve_walls(x,z,old_x,old_z,foot_y+step_height,
                                      foot_y+body_height,radius);
            hit|=contact;
            if(!contact) break;
        }
    }
    return hit;
}

int level_height(float x, float z)
{
    return level_height_below(x, z, FLT_MAX);
}

int level_entry_checkpoint_get(level_entry_checkpoint *out)
{
    if (!out || !s_vbo || !s_spawn_valid || s_model_count <= 0)
        return -1;
    *out = (level_entry_checkpoint){s_disc_spawn[0],s_disc_spawn[1],s_disc_spawn[2],
                                   s_spawn_zdat_eid,s_spawn_record_index};
    return 0;
}

float level_ground_near(float x,float z,float y,float max_drop,float max_rise)
{
    if (!isfinite(x) || !isfinite(z) || !isfinite(y) ||
        !isfinite(max_drop) || !isfinite(max_rise) || max_drop < 0 || max_rise < 0 ||
        !isfinite(y+max_rise) || !isfinite(y-max_drop))
        return -1000000.0f;
    float ground=level_ground_height(x,z,y+max_rise);
    return ground > -999999.0f && ground >= y-max_drop ? ground : -1000000.0f;
}

int level_spawn_point(float *out_x, float *out_y, float *out_z)
{
    if (!out_x || !out_y || !out_z || !s_vbo || s_model_count <= 0)
        return -1;

    if (s_spawn_valid) {
        *out_x = s_disc_spawn[0];
        *out_y = s_disc_spawn[1];
        *out_z = s_disc_spawn[2];
        return 0;
    }

    /* NSF/model order is storage order. A sky model or a wall in the first
     * three slots must not force the caller into the level-bounds midpoint. */
    for (int mi = 0; mi < s_model_count; mi++) {
        const model_rec *m = &s_models[mi];
        if (m->is_skybox)
            continue;
        for (uint32_t vi = m->first_vert; vi + 2 < m->first_vert + m->vert_count; vi += 3) {
            const level_vertex *v0 = &s_vbo[vi + 0];
            const level_vertex *v1 = &s_vbo[vi + 1];
            const level_vertex *v2 = &s_vbo[vi + 2];

            float e1x = v1->x - v0->x, e1y = v1->y - v0->y, e1z = v1->z - v0->z;
            float e2x = v2->x - v0->x, e2y = v2->y - v0->y, e2z = v2->z - v0->z;
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;

            float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
            if (nlen > 1e-4f && fabsf(ny) / nlen > 0.45f) {
                *out_x = (v0->x + v1->x + v2->x) / 3.0f;
                *out_y = (v0->y + v1->y + v2->y) / 3.0f;
                *out_z = (v0->z + v1->z + v2->z) / 3.0f;
                return 0;
            }
        }
    }
    return -1;
}

void level_bounds(float *minx, float *miny, float *minz,
                  float *maxx, float *maxy, float *maxz)
{
    *minx = *miny = *minz = 1e30f;
    *maxx = *maxy = *maxz = -1e30f;
    for (int i = 0; i < s_model_count; i++) {
        const model_rec *m = &s_models[i];
        if (m->is_skybox)
            continue;
        if (m->minx < *minx) *minx = m->minx;
        if (m->miny < *miny) *miny = m->miny;
        if (m->minz < *minz) *minz = m->minz;
        if (m->maxx > *maxx) *maxx = m->maxx;
        if (m->maxy > *maxy) *maxy = m->maxy;
        if (m->maxz > *maxz) *maxz = m->maxz;
    }
}

/* The Crash level code letter, e.g. 0x0A -> 'a'. */
int level_debug(int what)
{
    switch (what) {
    case 0: return s_vbo ? 1 : 0;
    case 1: return (int)((uintptr_t)s_vbo & 0xFFFFFF);
    case 2: return s_zone_switches;
    case 3: return s_alloc_fail;
    case 4: return (int)s_vbo_verts;
    }
    return -1;
}

char level_code(uint32_t id)
{
    static const char *chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (id < 10)
        return chars[id];
    if (id < 36)
        return chars[10 + (id - 10)];
    return '?';
}

BgmTheme level_get_bgm_theme(int level_id)
{
    if (level_id < 0 || level_id >= 64)
        return BGM_NONE;

    static const BgmTheme s_level_bgm[64] = {
        /* Hub & Boss Arenas */
        [0x02] = BGM_WARP_ROOM,   /* Warp Room Hub */
        [0x03] = BGM_BOSS,        /* Dingodile */
        [0x04] = BGM_BOSS,        /* Dr. N. Tropy */
        [0x05] = BGM_BOSS,        /* Dr. N. Gin */
        [0x06] = BGM_BOSS,        /* Tiny Tiger */
        [0x07] = BGM_BOSS,        /* Dr. Neo Cortex */

        /* Playable Levels (0x0A .. 0x27) */
        [0x0A] = BGM_ORIENTAL,    /* Orient Express */
        [0x0B] = BGM_MEDIEVAL,    /* Toad Village */
        [0x0C] = BGM_ORIENTAL,    /* Bone Yard (Prehistoric fallback) */
        [0x0D] = BGM_WARP_ROOM,   /* Tell No Tales (Jet-ski fallback) */
        [0x0E] = BGM_EGYPTIAN,    /* Under Pressure (Underwater fallback) */
        [0x0F] = BGM_MEDIEVAL,    /* Gee Wiz */
        [0x10] = BGM_ORIENTAL,    /* Dino Might! (Prehistoric fallback) */
        [0x11] = BGM_ORIENTAL,    /* Midnight Run */
        [0x12] = BGM_EGYPTIAN,    /* Tomb Time */
        [0x13] = BGM_FUTURE,      /* Bye Bye Blimps (Biplane fallback) */
        [0x14] = BGM_FUTURE,      /* Road Crash (Motorcycle fallback) */
        [0x15] = BGM_FUTURE,      /* Hog Ride (Motorcycle fallback) */
        [0x16] = BGM_ARABIAN,     /* Hang'em High */
        [0x17] = BGM_FUTURE,      /* Mad Bombers (Biplane fallback) */
        [0x18] = BGM_EGYPTIAN,    /* Tomb Wader */
        [0x19] = BGM_WARP_ROOM,   /* Makin' Waves (Jet-ski fallback) */
        [0x1A] = BGM_ARABIAN,     /* High Time */
        [0x1B] = BGM_FUTURE,      /* Future Frenzy */
        [0x1C] = BGM_EGYPTIAN,    /* Deep Trouble (Underwater fallback) */
        [0x1D] = BGM_MEDIEVAL,    /* Double Header */
        [0x1E] = BGM_EGYPTIAN,    /* Sphynxinator */
        [0x1F] = BGM_FUTURE,      /* Rings of Power (Biplane fallback) */
        [0x20] = BGM_FUTURE,      /* Orange Asphalt (Motorcycle fallback) */
        [0x21] = BGM_WARP_ROOM,   /* Ski Crazed (Jet-ski fallback) */
        [0x22] = BGM_ARABIAN,     /* Flaming Passion */
        [0x23] = BGM_FUTURE,      /* Gone Tomorrow */
        [0x24] = BGM_EGYPTIAN,    /* Bug Lite */
        [0x25] = BGM_FUTURE,      /* Area 51? (Motorcycle & UFO fallback) */
        [0x26] = BGM_ORIENTAL,    /* Eggipus Rex (Prehistoric fallback) */
        [0x27] = BGM_WARP_ROOM,   /* Hot Coco (Jet-ski fallback) */

        /* Cinematics & Meta Screens */
        [0x28] = BGM_TITLE,       /* Opening Cutscene */
        [0x29] = BGM_WARP_ROOM,   /* Any% Ending */
        [0x2A] = BGM_TITLE,       /* 100% Ending */
        [0x3A] = BGM_WARP_ROOM,   /* Intermission Vortex */
        [0x3B] = BGM_NONE,        /* Game Over (Authentic silence) */
        [0x3C] = BGM_TITLE        /* Title Screen */
    };

    return s_level_bgm[level_id];
}

