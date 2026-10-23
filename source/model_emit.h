#ifndef MODEL_EMIT_H
#define MODEL_EMIT_H
#include "model_commands.h"
#include "gool_native_primitive.h"
#include "gool_native_shading.h"
enum {MODEL_EMIT_DONE=0,MODEL_EMIT_PENDING=1,MODEL_EMIT_INVALID=-172};
typedef struct {
    model_commands *command;
    uint64_t generation;
    uint32_t index,primitive,arena_end,table_begin,table_end,bucket,next,uv_end;
    uint32_t sxy[3],colors[3],color_flags,normal_flags,depth_mac,coefficient,selected_depth,fade_stores;
    gool_native_primitive_size size;
    int mode,phase,opened,retained,status,backface,skipped;
} model_emit;
/* Stable initially{0} owner. Capture an admitted projected command for actual
 * mode0/1/2/7 RGB or mode5 constant native primitive emission. Real native arena/OT
 * spans, F0 depth-base, packet flags, RGBC CODE and far color are caller inputs.
 * RGB modes apply DPCT only for packet-low16 nonzero and command NO_FOG clear;
 * final command color flags are aggregated. Mode5 uses unmodified table base.
 * Mode1 uses actual SZ1..3:zero through C00, DPCS through1400, then raw
 * attribute words; CODE comes from each attribute. Scratch124..12C is written
 * before its all-zero early return; otherwise emit the native textured36 body.
 * Retain the command handoff until close; cannot consume or close it meanwhile.
 * Scratch120 raw AVSZ3 MAC0, body and link execute once in bounded stages.
 * Failed stores are sticky and partial native effects remain, not rolled back.
 * Does not invent renderer arena addresses, initialize OT, submit PICA work or
 * acknowledge command/projection. After DONE, close then explicitly accept.
 */
int model_emit_begin(model_emit *owner,model_commands *command,const model_command_projection *projection,
    uint32_t primitive,uint32_t arena_end,uint32_t table_begin,uint32_t table_end,
    uint32_t depth_base,uint32_t packet_flags,uint32_t rgba,const uint32_t far_color[3]);
/* Actual mode3/4 normal paths. Capture first command normal, evaluate native
 * NCLIP XOR sign, custom shading and mode-specific bucket bias. Requires real
 * mapped highlight table/lighting/FC/RGBC context. Same handoff/failure rules.
 */
int model_emit_begin_normal(model_emit *owner,model_commands *command,const model_command_projection *projection,
    uint32_t primitive,uint32_t arena_end,uint32_t table_begin,uint32_t table_end,
    uint32_t depth_base,const gool_native_shading_controls *controls);
/* Mode6 chooses native F4 or F8 by signed(command<<6)>0, then follows mode0.
 * Both real depth bases are explicit; ordinary begin rejects mode6 so it
 * cannot silently substitute an already-selected/default depth base. */
int model_emit_begin_depth_pair(model_emit *owner,model_commands *command,const model_command_projection *projection,
    uint32_t primitive,uint32_t arena_end,uint32_t table_begin,uint32_t table_end,
    uint32_t depth_f4,uint32_t depth_f8,uint32_t packet_flags,uint32_t rgba,const uint32_t far_color[3]);
int model_emit_step(model_emit *owner,uint32_t stage_budget);
int model_emit_close(model_emit *owner);
#endif
