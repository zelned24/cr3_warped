#ifndef MODEL_GPU_TEXTURE_H
#define MODEL_GPU_TEXTURE_H
#include <citro3d.h>
#include "model_texture.h"

#define MODEL_GPU_TEXTURE_WIDTH 256u
#define MODEL_GPU_TEXTURE_HEIGHT 128u
#define MODEL_GPU_TEXTURE_BYTES (MODEL_GPU_TEXTURE_WIDTH*MODEL_GPU_TEXTURE_HEIGHT*4u)
enum {MODEL_GPU_TEXTURE_DONE=0,MODEL_GPU_TEXTURE_PENDING=1,
      MODEL_GPU_TEXTURE_INVALID=-153,MODEL_GPU_TEXTURE_BUSY=-154,
      MODEL_GPU_TEXTURE_MEMORY=-155};
typedef struct {uint64_t generation;} model_gpu_texture_lease;
typedef struct {
    C3D_Tex texture;
    model_material_uv material;
    const uint16_t *source;
    uint64_t generation;
    uint32_t width,height,cursor,references,transparent,opaque,stp;
    int initialized,active,ready,status;
} model_gpu_texture;
/* Stable initially{0} owner. Initialize a fixed131072-byte linear texture once,
 * while citro3d is initialized. No allocation/reallocation during begin/step.
 * Intended for a bounded caller-owned array; this API does not evict residents.
 */
int model_gpu_texture_init(model_gpu_texture *owner);
/* Capture a successfully decoded PS1 rectangle. Raw source output remains
 * immutable until step DONE or cancel; source decoder can close its page borrow
 * because its output is independently caller-owned. Replacement refuses every
 * live GPU/CPU lease. Completed texture is nearest/clamp RGBA8, bottom-up8x8
 * Morton tiles, edge-padded to256x128. STP pixels retain alpha128, opaque255,
 * raw0 alpha0. This alone does not implement the four PS1 blend equations.
 */
int model_gpu_texture_begin(model_gpu_texture *owner,const model_texture *source);
int model_gpu_texture_step(model_gpu_texture *owner,uint32_t texel_budget);
/* Each queued GPU consumer retains its own lease through GPU completion.
 * release must occur only after its actual C3D frame/command work is quiescent.
 * No implicit global sync or guessed frame-counter lifetime is provided. */
int model_gpu_texture_acquire(model_gpu_texture *owner,C3D_Tex **texture,
    model_gpu_texture_lease *lease);
int model_gpu_texture_release(model_gpu_texture *owner,model_gpu_texture_lease lease);
/* Actual source texel -> nearest texel-center coordinates in fixed texture.
 * Caller holds a lease; all triangle winding/material policy stays external. */
int model_gpu_texture_uv(const model_gpu_texture *owner,uint8_t u,uint8_t v,float *s,float *t);
int model_gpu_texture_cancel(model_gpu_texture *owner);
/* Requires all leases released after GPU quiescence, before citro3d shutdown. */
int model_gpu_texture_close(model_gpu_texture *owner);
#endif
