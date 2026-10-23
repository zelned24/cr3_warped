#ifndef MODEL_COMMANDS_H
#define MODEL_COMMANDS_H
#include "model_actor.h"
#include "model_visibility.h"
enum {MODEL_COMMANDS_DONE=0,MODEL_COMMANDS_PENDING=1,MODEL_COMMANDS_TRIANGLE=2,
      MODEL_COMMANDS_INVALID=-170};
typedef struct model_commands {
    model_actor *actor;
    model_triangle triangle;
    model_pose_vertex vertices[3];
    uint32_t scratch,cursor,copied,index,attributes[3];
    uint32_t handoffs;
    uint64_t tick,generation;
    int opened,retained,status,starts_triangle;
} model_commands;
/* Stable initially{0} owner. Walk retained TGEO item1 in original order,
 * copy its actual prefix of item2 words to scratch140, publish each new packed
 * pose vertex and immediately resolve its attribute before the next cache
 * store. Preserve T5/T6/T7 across strip carry and attribute-only commands.
 * Scratch is a real mapped renderer span, exclusively owned through close;
 * no allocation/clearing or source clock advancement. Hold a model_actor
 * consumer reference, preventing its close until this cursor releases it.
 */
int model_commands_begin(model_commands *owner,model_actor *actor,uint32_t scratch);
/* Budget counts prefix words or command groups. TRIANGLE retains a resolved
 * raw-color/normal triangle at owner.triangle until explicit consume. Polling
 * cannot replay stores or advance a pending handoff. Errors are sticky; partial
 * native stores remain and require close, not automatic retry. Output corner
 * attributes are captured values, not live cache indices anymore.
 * Vertices retain command-point packed GTE inputs:LOAD_VERTEX reads actual
 * scratch140/144, while newly decoded inputs survive later cache overwrites.
 */
int model_commands_step(model_commands *owner,uint32_t budget);
int model_commands_consume(model_commands *owner);
int model_commands_close(model_commands *owner);
/* Compute projection/admission of the currently retained source-order triangle.
 * local FIFO copy models this triple's RTPT result without mutating the actual
 * streaming renderer FIFO; command scheduling must still choose RTPS versus
 * RTPT/carry writes. Output camera MACs feed the backend near-plane clipper.
 * Does not consume, cull or acknowledge the cursor; both outputs unchanged on
 * failure. Depth/packet flags are explicit actual engine inputs.
 */
int model_commands_project(const model_commands *owner,const model_project_controls *controls,
    uint32_t packet_flags,int16_t zsf3,model_project_triangle *projected,model_visibility *visibility);
typedef struct {
    model_project_fifo fifo;
    model_project_triangle projected;
    model_visibility visibility;
    const model_commands *owner;
    uint64_t generation;
    uint32_t next_index;
    int pending;
} model_command_projection;
/* Initialize once from the actual prior FIFO; do not zero/replace it during
 * strips. At each retained triangle:RTPT for a new group, otherwise native
 * oldest-vertex SXY1= SXY0/SZ2=SZ1 writes followed by RTPS as selected by bit30.
 * Keep carried camera results in matching slots. Apply NCLIP/AVSZ3 MAC0 writes
 * in original order; visibility rejects still retain completed GTE effects.
 * Repeated calls on the same pending index return its captured result without
 * issuing another command. After downstream accepts/rejects that triangle,
 * model_commands_project_accept advances projection and command together.
 * Storage/control/timeline must remain stable; never copy this live owner.
 */
int model_commands_project_stream(const model_commands *owner,const model_project_controls *controls,
    uint32_t packet_flags,int16_t zsf3,model_command_projection *projection);
int model_commands_project_accept(model_commands *owner,model_command_projection *projection);
#endif
