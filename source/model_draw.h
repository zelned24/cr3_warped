#ifndef MODEL_DRAW_H
#define MODEL_DRAW_H
#include "model_texture_cache.h"
#include "model_pose.h"
#include "model_clip.h"
#include "model_project.h"
#include "gool_native_shading.h"
#include "model_packets.h"

#define MODEL_DRAW_VERTICES 24999u
#define MODEL_DRAW_BATCHES 256u
enum {MODEL_DRAW_OK=0,MODEL_DRAW_INVALID=-159,MODEL_DRAW_BUSY=-160,
      MODEL_DRAW_CAPACITY=-161,MODEL_DRAW_MEMORY=-162,MODEL_DRAW_SHADING=-164};
typedef struct {float x,y,z,r,g,b,u,v;} model_draw_vertex;
typedef struct {
    model_draw_vertex vertices[3];
    model_material_uv material;
    int textured;
} model_draw_triangle;
typedef struct {
    GPU_CULLMODE cull;
    GPU_TESTFUNC alpha_test,depth_test;
    GPU_WRITEMASK write_mask;
    GPU_BLENDEQUATION color_equation,alpha_equation;
    GPU_BLENDFACTOR source_color,destination_color,source_alpha,destination_alpha;
    uint32_t blend_constant;
    uint8_t alpha_reference,rgb_scale;
    int alpha_enabled,depth_enabled;
} model_draw_state;
typedef struct {model_draw_state states[2];uint32_t count,draw_mode;} model_draw_packet_passes;
/* Decode E1/TPage draw-mode changes and native opaque/STP pass selection.
 * PICA constant blend factors use normalized8-bit coefficients; native5-bit
 * rounding/dithering and mask-buffer parity require hardware verification.
 * Passes retain source order and disable depth testing for DMA painter order.
 * NOP/E1 commands produce no geometry. Output unchanged on invalid input.
 */
int model_draw_packet_plan(const model_packet_command *command,uint32_t draw_mode,
    model_draw_packet_passes *output);
typedef struct {
    C3D_Mtx projection,view;
    model_draw_state state;
    model_texture_cache *cache;
    model_texture_cache_lease lease;
    C3D_Tex *texture;
    uint32_t first,count;
    float low[3],high[3];
} model_draw_batch;
typedef struct {
    model_draw_vertex *vertices;
    model_draw_batch batches[MODEL_DRAW_BATCHES];
    DVLB_s *shader;
    shaderProgram_s program;
    int projection_location,view_location;
    uint32_t vertex_count,batch_count,cursor,submitted_vertices;
    int initialized,program_initialized,recording,sealed,submitted,status;
} model_draw;
/* Initialize stable initially{0} owner while citro3d is alive. One799968-byte
 * linear vertex pool plus fixed command storage; no per-frame allocation.
 * A failed init still requires close. Program uses world_shader.v.pica's actual
 * position/color/UV ABI. Prepared vertices must already contain the correct
 * morph, lighting, winding, UVs and coordinate conversion; this layer supplies
 * no default native transform, material order or blend interpretation.
 */
int model_draw_init(model_draw *owner);
int model_draw_begin(model_draw *owner);
/* Copy immutable prepared triangles and matrices. Explicit complete GPU state
 * is retained per batch; rgb_scale is1,2 or4. cacheNULL selects untextured
 * primary color; otherwise retain actual READY handle through retirement.
 * Negative failures preserve batch/vertex counts. PENDING means texture is
 * not ready, no batch accepted. This queue's total is strictly below25k;
 * whole-frame legacy/world/other pass accounting remains the caller's job.
 * Adjacent contiguous records with identical matrices, GPU state and texture
 * identity share one batch/lease without changing primitive order.
 */
int model_draw_record(model_draw *owner,const model_draw_vertex *vertices,uint32_t count,
    const C3D_Mtx *projection,const C3D_Mtx *view,const model_draw_state *state,
    model_texture_cache *cache,model_texture_handle texture);
/* Inside the caller's actual C3D frame/target. Each budget unit handles one
 * batch, with conservative side-plane AABB culling. GPU handles depth clipping.
 * Does not replay submitted commands on continuation; errors are sticky.
 * Caller binds required target/scissor and owns surrounding render state.
 */
int model_draw_submit(model_draw *owner,uint32_t batch_budget);
/* Outside an open C3D frame, after FrameEnd flushed all frame-hook work:
 * an empty FrameBegin(0)/FrameEnd(0) barrier waits for the prior GX queue,
 * then releases texture leases and permits vertex reuse. C3D must be alive.
 * FrameBegin refusal returns BUSY without releasing submitted ownership.
 * VBlank/FrameSync alone is not GPU completion. Also cancels unsubmitted
 * recorded batches. No invented completion signal or fixed frame delay.
 */
int model_draw_retire(model_draw *owner);
int model_draw_close(model_draw *owner);
/* Bridge completed packed pose/topology to the actual PICA vertex ABI. Positions
 * are exact signed Q5 integer inputs represented as floats, with no invented
 * object scale, axis swap or world units. Caller supplies the actual transform
 * for that coordinate space when recording the resulting batch.
 * RGB draw modes0/1/2/6/7 preserve each raw attribute's RGB8 input. Mode5 uses
 * its original untextured constant204088. Modes3/4 require the separate normal
 * shading entry below. RGB fog/light modulation remain upstream processing.
 * Static UV commands require resolved_uv=UINT32_MAX. Animated commands require
 * the engine's actual zero-based resolved UV index; no clock/mip guess exists.
 * Applies FLIPPED winding, captures actual material and maps texel centers to
 * the fixed GPU texture crop. Batch these triangles by actual native ordering
 * and obtain a matching texture-cache handle before model_draw_record.
 * Output unchanged on failure; it must not alias input owners/buffers.
 */
int model_draw_prepare_triangle(const model_pose *pose,const model_triangles *topology,
    uint32_t triangle,uint8_t render_mode,uint32_t resolved_uv,model_draw_triangle *output);
struct model_actor;
/* Same conversion using a completed retained actor preparation and its actual
 * captured object+122 render mode. No caller-selected replacement draw mode. */
int model_draw_prepare_actor_triangle(const struct model_actor *actor,uint32_t triangle,
    uint32_t resolved_uv,model_draw_triangle *output);
/* Actual mode3/4 custom flat normal shader, including native DPCS/highlight.
 * Uses first source triangle normal, actual engine controls and mapped original
 * highlight table. Returns processed RGB on all three PICA corners plus native
 * command results; no inferred light direction or generic material lighting.
 * controls.attribute_scratch supplies the real renderer cache for STORED_COLORS;
 * missing scratch rejects rather than using a TGEO word as a cache value.
 * Both outputs unchanged on failure and require disjoint caller storage. */
int model_draw_prepare_normal_actor_triangle(const struct model_actor *actor,uint32_t triangle,
    uint32_t resolved_uv,const gool_native_shading_controls *controls,model_draw_triangle *output,
    gool_native_shading_result *shading);
/* General prepared actor conversion with actual live scratch-cache attributes.
 * Caller must populate/exclude writes at the native command point. Normal
 * modes require controls/shading outputs; RGB modes acceptNULL for both. */
int model_draw_prepare_native_actor_triangle(const struct model_actor *actor,uint32_t triangle,
    uint32_t resolved_uv,uint32_t scratch,const gool_native_shading_controls *controls,
    model_draw_triangle *output,gool_native_shading_result *shading);
/* Resolve static/chained animated UVs from retained native spans and actual
 * scratch1C clock, then prepare the actor triangle. Dynamic zero selectors
 * remain untextured. Scratch attributes still require original command order.
 * Mode5 skips material lookup as the original constant-color primitive does. */
int model_draw_prepare_timed_actor_triangle(const struct model_actor *actor,uint32_t triangle,
    uint32_t scratch,const gool_native_shading_controls *controls,
    model_draw_triangle *output,gool_native_shading_result *shading);
struct model_commands;
/* Consume captured command-point attributes, preserving carried colors/normals
 * and packed vertex inputs, including actual LOAD_VERTEX scratch reads,
 * rather than rereading overwritten scratch words. Does not acknowledge the
 * command; explicitly consume only after downstream work accepts this triangle. */
int model_draw_prepare_command_triangle(const struct model_commands *command,
    const gool_native_shading_controls *controls,model_draw_triangle *output,gool_native_shading_result *shading);
typedef struct {uint32_t vertices,projection_flags;} model_draw_clip_result;
/* O3DS near-plane stage for an actual camera-space triangle. RGBUV attributes
 * use signed16.16; RGB must already be processed/clamped to0..1. Clip to the
 * caller's positive near depth, project each polygon corner with real GTE
 * controls, fan triangulate, then copy at most six vertices into this queue.
 * viewport is the caller's actual orthographic PS1-screen-to-PICA mapping,
 * including its selected depth range. No camera transform is applied again;
 * no viewport, near distance, backface, shading or blend policy is invented.
 * Fully clipped/degenerate polygons consume zero vertices/texture leases.
 * Pending texture/capacity/error leaves result and queue counts unchanged.
 * Does not mutate the original native command FIFOs or consume native flags.
 */
int model_draw_record_camera_triangle(model_draw *owner,const model_clip_vertex input[3],
    int32_t near,const model_project_controls *controls,const C3D_Mtx *viewport,
    const model_draw_state *state,model_texture_cache *cache,model_texture_handle texture,
    model_draw_clip_result *result);
/* Copy an already projected DMA triangle into the PICA queue in source
 * corner order. Decode GPU signed11 XY, retain final packet RGB, and map raw
 * UV bytes through the actual READY texture crop. Raw-texture commands use
 * white primary color and scale1; modulated texture commands require scale2.
 * Untextured commands require scale1 and no cache/material/texture handle.
 * The frame owner supplies real screen viewport, depth and complete pass
 * state (including opaque/STP alpha selection and native blend mode).
 * This does not infer a depth buffer value from lost DMA camera coordinates
 * or acknowledge/advance packet cursors. Material must come from this
 * packet's owning texture table. Pending/error leaves queue unchanged.
 */
int model_draw_record_packet_triangle(model_draw *owner,const model_packet_command *command,
    const model_material_uv *material,const C3D_Mtx *viewport,float depth,const model_draw_state *state,
    model_texture_cache *cache,model_texture_handle texture);
#endif
