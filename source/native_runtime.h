#ifndef NATIVE_RUNTIME_H
#define NATIVE_RUNTIME_H
#include "gool_forest.h"
#include "gool_native_object.h"
#include "model_stream.h"
#include "model_present.h"

enum {NATIVE_RUNTIME_DONE=0,NATIVE_RUNTIME_PENDING=1,
      NATIVE_RUNTIME_INVALID=-180,NATIVE_RUNTIME_BUSY=-181};

/* The owner borrows a bootstrapped factory and the real destruction providers.
 * It does not seed engine globals, advance a guessed clock or activate objects.
 * One native timeline is held across all eight roots and nested VM handoffs.
 * Object callbacks must provide actual protected-player, motion and rendering
 * services. In particular a missing collision provider cannot mean success. */
typedef struct {
    gool_native_factory *factory;
    gool_native_object object;
    gool_forest forest;
    gool_destroy_services retirement;
    uint64_t tick;
    uint32_t requested,visiting;
    int bound,active,status;
} native_runtime;
int native_runtime_bind(native_runtime *owner,gool_native_factory *factory,
    gool_native_route_frame *frames,uint32_t frame_capacity,uint32_t arguments_base,
    const gool_native_object_services *objects,const gool_destroy_services *retirement);
int native_runtime_begin(native_runtime *owner,uint32_t requested_update);
int native_runtime_step(native_runtime *owner,uint32_t budget);
/* A pending destruction callback has no generic cancellation contract. Keep
 * the owner alive until it has completed; close refuses to discard that job. */
int native_runtime_close(native_runtime *owner);

/* Exclusive real GPU frame owner. Stable initially-zero storage. This owns
 * C3D FrameBegin/End, retained packets and model_draw texture leases. No other
 * user may open a C3D frame during step/close. On a capacity drain, completed
 * batches submit once, an actual queue barrier retires them, and the pending
 * packet/pass resumes with aggregate vertex accounting intact. The target is
 * cleared once per logical frame, including across multiple queue drains.
 * All world/UI draws must be included in controls.prior_vertices beforehand.
 * A final GPU-frame callback can draw the Citro2D bottom UI before FrameEnd;
 * its vertex allowance belongs to that same prior count. */
typedef struct {
    model_draw draw;
    model_present present;
    model_stream stream;
    C3D_RenderTarget *target;
    void *finish_context;
    void (*finish)(void *context);
    uint32_t clear_color,vertices,draw_mode;
    int initialized,active,phase,complete,cleared,status;
} native_frame;
int native_frame_init(native_frame *owner);
/* Stream remains retained on DONE so the caller can publish its actual arena
 * cursor/FIFO and append immutable bindings before close. No native register
 * addresses or arena layout are inferred by this bridge. */
int native_frame_stream_begin(native_frame *owner,model_actor *actor,const model_stream_controls *controls);
int native_frame_stream_step(native_frame *owner,uint32_t budget);
int native_frame_stream_close(native_frame *owner);
int native_frame_begin(native_frame *owner,C3D_RenderTarget *target,uint32_t clear_color,
    gool_domain *domain,const model_packet_span spans[2],uint32_t first_low24,
    const model_present_controls *controls,void (*finish)(void *),void *finish_context);
int native_frame_step(native_frame *owner,uint32_t budget,uint32_t pixel_budget);
int native_frame_close(native_frame *owner);
#endif
