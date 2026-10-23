#ifndef MODEL_TEXTURE_CACHE_H
#define MODEL_TEXTURE_CACHE_H
#include "model_gpu_texture.h"

#define MODEL_TEXTURE_CACHE_SLOTS 8u
#define MODEL_TEXTURE_CACHE_PIXELS (256u*128u)
enum {MODEL_TEXTURE_CACHE_DONE=0,MODEL_TEXTURE_CACHE_PENDING=1,
      MODEL_TEXTURE_CACHE_INVALID=-156,MODEL_TEXTURE_CACHE_BUSY=-157,
      MODEL_TEXTURE_CACHE_FULL=-158};
typedef struct {uint64_t generation;uint32_t slot;} model_texture_handle;
typedef struct {
    model_texture_handle handle;
    model_gpu_texture_lease texture;
} model_texture_cache_lease;
typedef struct {
    model_gpu_texture gpu;
    model_material_uv material;
    nsf_resource_ticket ticket;
    uint64_t generation;
    uint32_t phase;
    int state,has_ticket,error;
} model_texture_cache_slot;
typedef struct {
    model_texture_cache_slot slots[MODEL_TEXTURE_CACHE_SLOTS];
    model_texture decoder;
    uint16_t pixels[MODEL_TEXTURE_CACHE_PIXELS];
    uint64_t generation;
    uint32_t active_slot;
    int opened,closing,error;
} model_texture_cache;
/* Stable static initially{0} owner:65536 CPU staging bytes plus eight fixed
 * 131072-byte linear GPU textures. Initialize while citro3d is alive; no hot
 * allocation. A failed init still requires close for partial allocations.
 * One loading/uploading job owns shared staging; other misses return BUSY.
 * Existing READY hits remain available while a different slot loads. Cache is
 * bound to one unchanged NSF session; close it before replacing the archive.
 */
int model_texture_cache_init(model_texture_cache *owner);
/* Key:actual texture EID, CLUT, page mode and inclusive UV rectangle. Corner
 * order is not a texture key. No automatic eviction: FULL requires explicitly
 * retiring an unleased resident. A handle is an identity, not a GPU lease.
 * Returns0 READY,1 pending; negative leaves handle unchanged. */
int model_texture_cache_request(model_texture_cache *owner,const model_material_uv *material,
    model_texture_handle *handle);
int model_texture_cache_poll(model_texture_cache *owner,uint32_t pixel_budget);
int model_texture_cache_acquire(model_texture_cache *owner,model_texture_handle handle,
    C3D_Tex **texture,model_texture_cache_lease *lease);
/* Release only after actual submitted GPU work is quiescent. */
int model_texture_cache_release(model_texture_cache *owner,model_texture_cache_lease lease);
int model_texture_cache_uv(const model_texture_cache *owner,model_texture_cache_lease lease,
    uint8_t u,uint8_t v,float *s,float *t);
/* Eviction cancels unfinished CPU/NSF work and preserves allocated GPU storage.
 * Refuses live GPU leases; cleanup failures retain remaining owners for retry.
 * A failed job blocks shared staging until explicitly evicted or closed. */
int model_texture_cache_evict(model_texture_cache *owner,model_texture_handle handle);
int model_texture_cache_close(model_texture_cache *owner);
#endif
