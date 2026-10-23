#ifndef MODEL_ACTOR_H
#define MODEL_ACTOR_H
#include "gool_native_actor.h"
#include "model_pose.h"
#include "model_triangles.h"
#include "gool_native_render.h"

enum {MODEL_ACTOR_DONE=0,MODEL_ACTOR_PENDING=1,MODEL_ACTOR_INVALID=-165,
      MODEL_ACTOR_CHANGED=-166};
typedef struct {
    model_vertex_bytes *first,*second;
    model_pose_vertex *pose;
    model_triangle *triangles;
    uint32_t vertex_capacity,triangle_capacity;
} model_actor_buffers;
typedef struct model_actor {
    gool_native_actor *actor;
    model_resources *bank;
    model_vertices frames[2];
    model_triangles topology;
    model_pose pose;
    model_actor_buffers buffers;
    uint64_t tick;
    uint32_t descriptor,frame_word,render_word,auxiliary,auxiliary_words[11],position[3],phase;
    model_resource_lease frame_identity,geometry_identity;
    uint32_t consumers;
    int opened,active,retained,paired,status;
} model_actor;
/* Stable initially{0} caller owner and disjoint caller-supplied fixed pools.
 * Compose the actual READY type1 actor model into retained SVTX decoding,
 * TGEO topology and original single/paired packed pose. Frames come from
 * already-published auxiliary20/24; no animation, loading fallback or index is
 * chosen. Missing resources must finish native model preparation first.
 * Retain the stopped actor through explicit close; independently retain each
 * model generation. Snapshot actual E8/EC/render word, native32-byte auxiliary
 * matrix and object60..68 position. Require unchanged snapshot and actual engine
 * tick across every continuation. This does not convert the native matrix into
 * a guessed camera/world transform, select draw order or perform GTE lighting.
 * Second frame buffer may beNULL only when published frame pointers match.
 * On failure after begin, close releases remaining owners without replay or
 * rollback. On DONE, pose/topology and snapshots stay readable until close.
 */
int model_actor_begin(model_actor *owner,gool_native_actor *actor,const model_actor_buffers *buffers);
/* One bounded stage per call; work_budget bounds vertices or command groups
 * in the active stage. No allocation, SD I/O, frame advance or busy spin. */
int model_actor_step(model_actor *owner,uint32_t work_budget);
int model_actor_close(model_actor *owner);
/* Bind actual retained actor matrix/TGEO/frame offsets to an explicit real
 * engine renderer snapshot. Remaining camera/gp/packet/lighting words are
 * caller-owned engine evidence; packet bit30/29 must match the type1 frame
 * form selected by original1B7A0. Does not synthesize missing global state. */
int model_actor_render_prepare(const model_actor *owner,uint32_t function,
    const gool_native_render_input *engine,gool_native_render_output *output);
/* Capture actual mapped engine packet and bind it to this prepared actor.
 * Validate packet10/14/30/34/6C identities against retained node/matrix/frames/
 * TGEO header and the still-current source/tick snapshot. Both outputs remain
 * unchanged on failure; caller can commit packet stores using captured input.
 * This requires real mapped renderer state, not an allocated replacement
 * packet or selected default camera. Outputs must be disjoint caller storage.
 */
int model_actor_render_capture(const model_actor *owner,uint32_t gp,uint32_t scratch,uint32_t packet,
    const uint32_t prior_light[5],uint32_t projection_distance,
    gool_native_render_input *captured,gool_native_render_output *output);
#endif
