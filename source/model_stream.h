#ifndef MODEL_STREAM_H
#define MODEL_STREAM_H
#include "model_emit.h"
#include "model_packets.h"

enum {MODEL_STREAM_DONE=0,MODEL_STREAM_PENDING=1,MODEL_STREAM_INVALID=-173,MODEL_STREAM_BINDINGS=-176};
typedef struct {
    model_project_controls projection;
    model_project_fifo prior_fifo;
    gool_native_shading_controls shading;
    uint32_t scratch,primitive,arena_end,table_begin,table_end;
    uint32_t depth_base,depth_f4,depth_f8;
    model_packet_binding *bindings;
    uint32_t binding_capacity;
    int16_t zsf3;
} model_stream_controls;
typedef struct {
    model_commands commands;
    model_command_projection projection;
    model_emit emission;
    model_stream_controls controls;
    uint32_t primitive,emitted,rejected,skipped,color_flags,normal_flags;
    model_packet_binding binding;
    uint32_t binding_count;
    int opened,status,phase;
} model_stream;
/* Stable initially{0} fixed-storage owner. Connect retained actor command
 * traversal, real GTE projection/admission and all eight native primitive
 * modes. Caller supplies actual scratch, arena, OT, depth, lighting and prior
 * FIFO context; no default addresses, OT clear, frame advance or allocation.
 * Keep the actor stopped and its engine tick unchanged through completion.
 * Scratch/arena/OT must remain exclusively owned. Each budget unit executes
 * one bounded command group, projection, emission stage or acknowledgement.
 * DONE retains the actor and exposes final primitive cursor/FIFO until close.
 * Failures are sticky:partial arena/OT/scratch writes remain, so abandon the
 * failed frame rather than replaying it. Close releases emission before the
 * command/actor consumer. Does not publish global cursor registers or submit
 * PICA commands; the real frame owner must explicitly commit completed work.
 * Caller bindings pool captures each emitted textured command's actual EID,
 * sorted by native packet/word address. Preserve pool through presentation,
 * including after this owner closes. Capacity must cover all textured output.
 */
int model_stream_begin(model_stream *owner,model_actor *actor,const model_stream_controls *controls);
int model_stream_step(model_stream *owner,uint32_t budget);
int model_stream_close(model_stream *owner);
#endif
