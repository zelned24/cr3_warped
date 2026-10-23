#ifndef MODEL_TRIANGLES_H
#define MODEL_TRIANGLES_H
#include "model_vertices.h"
#include "model_pose.h"

enum {MODEL_TRIANGLES_DONE=0,MODEL_TRIANGLES_PENDING=1,
      MODEL_TRIANGLES_INVALID=-148,MODEL_TRIANGLES_FORMAT=-149,
      MODEL_TRIANGLES_CAPACITY=-150};
typedef struct {
    uint32_t vertex,attribute,attribute_flags,attribute_index;
    uint32_t vertex_flags,vertex_command,attribute_stored;
} model_triangle_corner;
typedef struct {
    uint32_t flags;
    model_triangle_corner corners[3];
} model_triangle;
typedef struct {
    model_resources *bank;
    model_resource_lease lease;
    model_triangle *output;
    model_triangle_corner last[3];
    uint32_t stored[256],valid[8];
    uint32_t entry,commands,command_bytes,cursor,attributes,attribute_bytes;
    uint32_t attribute_count,uv_count,animation_count,declared_triangles,copy_count;
    uint32_t uvs,animations,texture_names,texture_count;
    uint32_t vertex_count,next_vertex,count,capacity;
    int opened,active,retained,status;
} model_triangles;

/* TGEO topology adapted from retained MIT noclip.website buildModel and local
 * tools/tgeo.py. Capture TGEO from a successfully completed vertex decoder,
 * independently retaining its generation. Caller supplies stable output.
 * Vertex cache misses and out-of-range vertex/attribute/UV references reject;
 * no fallback vertex0, replacement color or inferred shading is permitted.
 * Stores source command order and unpermuted corners, including each original
 * attribute word/flags. Material grouping, reverse traversal, FLIPPED winding,
 * animated UV resolution and RGB-versus-normal shading belong to rendering.
 * Header triangle count is metadata; bounded sentinel traversal determines the
 * actual count. initially{0}; close mandatory after acquisition errors too.
 * Do not alias output/owner with the vertex decoder, its output, bank or domain.
 */
int model_triangles_begin(model_triangles *owner,const model_vertices *vertices,
    model_triangle *output,uint32_t capacity);
/* One budget unit is one command group (three words for a new triangle,
 * otherwise one). Completed groups are not replayed. Missing sentinel fails.
 * Partial output remains on sticky failure; consumers require DONE. */
int model_triangles_step(model_triangles *owner,uint32_t budget);
/* Quiesce consumers of owner metadata before close. Caller owns output bytes. */
int model_triangles_close(model_triangles *owner);

typedef struct {
    uint32_t texture_eid;
    uint16_t clut,texpage;
    uint8_t u[4],v[4],min_u,max_u,min_v,max_v;
    uint8_t depth,blend,texture_index;
} model_material_uv;
typedef struct {
    uint32_t flags;
    uint16_t start;
    uint8_t offset,frame_shift,count_mask,mipmapped,indirect;
} model_material_animation;
typedef struct {
    model_material_uv material;
    uint32_t index,native_address,animation_lookups;
    int textured;
} model_material_selection;
/* Original3CCDC..3CD34 resolver over actual retained item3/item4 bytes.
 * Read actual scratch1C only for animated commands. Nonzero native selector
 * is a record-end pointer, converted explicitly to zero-based index-1.
 * Selection0 is untextured, indexUINT32_MAX, no fabricated material. */
int model_material_resolve(const model_triangles *owner,uint32_t triangle,uint32_t scratch,
    model_material_selection *output);
/* Read actual zero-based TGEO UV/animation records after topology DONE.
 * UV0 in a nonanimated triangle command means untextured; other command UVs
 * are one-based and must subtract1 before model_material_read. Animated
 * command indices are already zero-based. Pure lookups leave output unchanged
 * on failure and borrow this owner's retained TGEO only for the call.
 * EIDs/CLUT/page bits and all four corners remain intact. Animation metadata
 * does not choose time, mip distance or dereference engine-owned indirection.
 */
int model_material_read(const model_triangles *owner,uint32_t index,model_material_uv *output);
int model_material_animation_read(const model_triangles *owner,uint32_t index,
    model_material_animation *output);
/* Resolve an attribute at the actual native command point. Ordinary corners
 * retain a raw TGEO word; STORED_COLORS corners retain an explicit cache index
 * token in attribute and must read scratch+140+4*index. attribute_index is kept
 * separately because attribute-only commands encode corner2's index atbit2.
 * attribute_stored preserves native selection:bit27 for vertex commands,
 * bit0 for attribute-only corners0/1 and bit1 for attribute-only corner2.
 * Caller owns the actual mapped scratch and its command-order population;
 * this function never creates/zeros a cache or substitutes a table value.
 * Output unchanged on failure. */
int model_material_attribute_read(const model_triangles *owner,uint32_t triangle,uint32_t corner,
    uint32_t scratch,uint32_t *output);
/* Original3D290/3E340 ordered SW scratch140+slot*8 and+4 for a newly decoded
 * vertex. LOAD_VERTEX preserves existing cache words. Invoke in original
 * command order before resolving that command's STORED_COLORS operand.
 * vertex_command records the original item1 byte offset; command_offset must
 * equal it, so strip-carried corners cannot masquerade as a new command.
 * Caller supplies mapped actual scratch and excludes concurrent consumers.
 * Repeating a store after domain failure is not automatic: the first word
 * may have been written. No scratch allocation or implicit clearing occurs.
 */
int model_material_vertex_cache_store(const model_triangles *owner,const model_pose *pose,
    uint32_t triangle,uint32_t corner,uint32_t command_offset,uint32_t scratch);
#endif
