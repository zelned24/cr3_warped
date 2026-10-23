#ifndef MODEL_VERTICES_H
#define MODEL_VERTICES_H
#include "model_resources.h"

enum {MODEL_VERTICES_DONE=0,MODEL_VERTICES_PENDING=1,
      MODEL_VERTICES_INVALID=-146,MODEL_VERTICES_FORMAT=-147};
typedef struct {uint8_t x,y,z,pad;} model_vertex_bytes;
typedef struct {
    model_resources *bank;
    model_resource_lease frame_lease,geometry_lease;
    model_vertex_bytes *output;
    uint32_t capacity,frame,frame_bytes,geometry,compression,compression_bytes;
    uint32_t count,decoded,cursor,sink,bits;
    uint32_t scale[3],origin_z_word,view_offset_words[2];
    int16_t origin[3];
    uint8_t previous[3];
    int opened,active,status,frame_retained,geometry_retained;
} model_vertices;

/* Format implementation adapted from retained MIT noclip.website reference
 * generateVertices/SignedBitStream (tools/reference/crashwarped_bin.ts).
 * Decode one actual resident SVTX item with the actual TGEO instruction item5.
 * The supplied frame and entries must be exact native mapped addresses, not
 * EIDs. Acquires independent generation leases; actor replacement cannot retire
 * bytes while decoding. No actor/animation selection or output allocation.
 * Output is unsigned XYZ plus zero padding, matching the four-byte format;
 * signed origin/32 and TGEO scale/256 remain explicit metadata for rendering.
 * initially{0}, stable owner/output storage through close. On acquisition or
 * format failure close remains mandatory. No output is GPU-ready until DONE;
 * after submitting it, callers must quiesce DMA before close/reusing storage.
 */
int model_vertices_begin(model_vertices *owner,model_resources *bank,
    uint32_t svtx_entry,uint32_t frame,uint32_t tgeo_entry,
    model_vertex_bytes *output,uint32_t capacity);
struct gool_native_actor;
/* Capture one of the actor's two actual prepared frames (member0 or1), checking
 * auxiliary pointers against retained owners before taking independent leases.
 * Does not retain the actor: after this synchronous capture, vertex input lives
 * on the decoder's leases. A renderer must separately retain object/matrices. */
int model_vertices_begin_actor(model_vertices *owner,struct gool_native_actor *actor,
    uint32_t member,model_vertex_bytes *output,uint32_t capacity);
/* At most budget whole vertices. Truncated input fails without manufactured
 * padding; already decoded vertices remain visible, status is sticky. */
int model_vertices_step(model_vertices *owner,uint32_t budget);
int model_vertices_close(model_vertices *owner);
#endif
