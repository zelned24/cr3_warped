#ifndef MODEL_TEXTURE_H
#define MODEL_TEXTURE_H
#include "model_triangles.h"
#include "nsf_resource.h"

enum {MODEL_TEXTURE_DONE=0,MODEL_TEXTURE_PENDING=1,
      MODEL_TEXTURE_INVALID=-151,MODEL_TEXTURE_FORMAT=-152};
typedef struct {
    nsf_resource_view view;
    model_material_uv material;
    uint16_t *output;
    uint32_t width,height,count,decoded,capacity;
    int opened,active,status;
} model_texture;
/* Decode the inclusive UV rectangle from an explicitly READY original texture
 * page ticket. Acquires its own page borrow; ticket reference stays with caller.
 * The complete64KiB page includes the original16-byte header as pixel data,
 * matching tools/tpag.py and the retained MIT reference parseTexPage.
 * Supports native4/8/16bpp and actual CLUT lookup; preserves raw RGB555/STP
 * words. Zero,0x8000 and nonzero opaque pixels remain distinguishable.
 * Output is row-major PS1 words, not a PICA texture. No palette guessing,
 * Y-base interpretation, transparent-color substitution or GPU allocation.
 * initially{0}; owner/output stable through close. Output requires capacity
 * width*height words and must not alias the source page or owner.
 * Close remains mandatory after any failure following acquisition.
 */
int model_texture_begin(model_texture *owner,nsf_resource_ticket ticket,
    const model_material_uv *material,uint16_t *output,uint32_t capacity);
int model_texture_step(model_texture *owner,uint32_t pixel_budget);
/* Stop consumers of source/owner before close. Decoded output is caller-owned
 * and stays usable after this releases the page borrow. */
int model_texture_close(model_texture *owner);
#endif
