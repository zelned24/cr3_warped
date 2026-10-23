#ifndef MODEL_PRESENT_H
#define MODEL_PRESENT_H
#include "model_draw.h"

enum {MODEL_PRESENT_DONE=0,MODEL_PRESENT_PENDING=1,MODEL_PRESENT_DRAIN=3,
      MODEL_PRESENT_INVALID=-177,MODEL_PRESENT_VERTEX_LIMIT=-178};
typedef struct {
    model_draw *draw;
    model_texture_cache *cache;
    const model_packet_binding *bindings;
    uint32_t binding_count,draw_mode,prior_vertices;
    C3D_Mtx viewport;
    float depth;
} model_present_controls;
typedef struct {
    model_packets packets;
    model_present_controls controls;
    model_packet_command command;
    model_draw_packet_passes plan;
    model_material_uv material;
    model_texture_handle texture;
    uint32_t word,pass,triangles,passes,frame_vertices;
    int opened,status,phase;
} model_present;
/* Stable initially{0} owner. Compose actual DMA traversal, immutable texture
 * provenance, async residency, E1/TPage updates, opaque/STP planning and PICA
 * recording. Inputs/storage stay alive and exclude writes/eviction through
 * completion. Draw queue/cache are caller-owned; no allocation or frame begin.
 * Each budget unit performs one tag/word read, command decode, cache operation
 * or pass record; pixel_budget bounds a cache decoder/upload poll.
 * DRAIN preserves the exact unaccepted command/pass. Submit and retire the
 * caller's draw queue under real C3D frame ownership, begin its next chunk,
 * then resume. Retire is required before leased textures become evictable.
 * prior_vertices includes earlier draws in this actual frame. frame_vertices
 * counts every accepted pass across drains and rejects a24999-vertex overrun
 * before recording either pass of a triangle. Caller must include later draws
 * in the remaining budget; resetting a queue does not reset this accounting.
 * DONE means queued, not GPU complete. Sticky errors retain prior accepted
 * queue entries and native list leases until close; close does not retire or
 * discard caller-owned GPU work. No native raster/rounding parity is claimed.
 */
int model_present_begin(model_present *owner,gool_domain *domain,const model_packet_span spans[2],
    uint32_t first_low24,const model_present_controls *controls);
int model_present_step(model_present *owner,uint32_t budget,uint32_t pixel_budget);
int model_present_close(model_present *owner);
#endif
