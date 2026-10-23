#ifndef GOOL_NATIVE_ACTOR_H
#define GOOL_NATIVE_ACTOR_H
#include "gool_native_resources.h"
#include "gool_vm.h"
#include "zdat_resources.h"
#include "model_resources.h"
enum {GOOL_NATIVE_ACTOR_INVALID=-104,GOOL_NATIVE_ACTOR_BUSY=-105};
typedef struct gool_native_actor gool_native_actor;
struct gool_native_geometry_services;
struct audio_native_services;
typedef struct {
    gool_domain *domain;
    gool_native_resources *resources;
    zdat_resources *zones; /* optional for standalone providers; required by level owner */
    model_resources *models;
    gool_native_actor **actors;
    uint32_t count;
} gool_native_registry;
struct gool_native_actor {
    gool_native_registry *registry;
    gool_vm *vm;
    const uint64_t *tick_source; /* borrowed real engine timeline, when factory-owned */
    gool_operand_context previous_context;
    gool_data base,alternate,constant_base;
    void *previous_address_context;
    gool_vm_address_read previous_read;
    gool_vm_address_write previous_write;
    gool_domain_lease stack_lease;
    zdat_resource_lease zone_lease,database_lease;
    model_resource_lease frame_lease;
    model_resource_lease geometry_lease;
    uint32_t frame_address;
    uint32_t frame_next_address;
    uint32_t geometry_address;
    int frame_retained;
    int geometry_retained;
    uint32_t zone_address,database_zone_address;
    int zone_retained,database_retained;
    uint8_t *stack_backing;
    uint64_t resource_generation;
    uint32_t node,index,stack_address,stack_words,call_references;
    int attached,enabled,executing,error,stack_mapped,resource_retained,flow_dirty;
};
/* Registry storage, actor pointers and every actor/VM stay at stable addresses.
 * Existing native object root mappings are caller-owned mutable storage.
 * When registry.zones is bound, each actor retains its actual object+18 zone.
 * Placement database object+F0 also retains its database+4 owner zone.
 * All subsequent object+18/+F0 writes must use registry_write to transfer ownership
 * before publishing the handle; direct raw writes would bypass that lifetime. */
int gool_native_registry_bind(gool_native_registry *registry,gool_domain *domain,
    gool_native_resources *resources,gool_native_actor **actors,uint32_t count);
int gool_native_registry_can_close(const gool_native_registry *registry);
int gool_native_registry_close(gool_native_registry *registry);
/* VM must already have actual native-source base/alternate/code and engine
 * state-change/send/global/RNG/timing providers. This does not create guessed
 * state or replace native scheduling. Creates a retained stack overlay within
 * object_region and restores untouched old stack slots from its real backing.
 * Rebinds member/lighting/array callbacks to the native domain. No heap storage.
 * Caller uses step/publish rather than bypassing live register synchronization.
 */
int gool_native_actor_attach(gool_native_actor *actor,gool_native_registry *registry,
    uint32_t index,uint32_t node,gool_domain_lease object_region,gool_vm *vm);
/* Adopt a quiescent native object's actual code, base/alternate and packed
 * frames into an already configured VM, then attach its bounded stack overlay.
 * Preserves caller-provided globals/RNG/timing/signal/state services. VM must
 * have no active frame, pending SEND or delay token; no defaults are invented.
 * Object backing is never overwritten with a fabricated initial root frame. */
int gool_native_actor_attach_raw(gool_native_actor *actor,gool_native_registry *registry,
    uint32_t index,uint32_t node,gool_domain_lease object_region,gool_vm *vm);
/* After real engine state/frame operations modified native registers/storage,
 * pull their exact state while stopped. Leaves VM mode, timing, signal result,
 * fault and token ownership to the calling state/dispatch service. */
int gool_native_actor_pull_flow(gool_native_actor *actor);
/* Actual frame formation over stopped raw SP/FP/PC, including null saved PC
 * during state setup. Does not fetch code or spill a stale cached PC. Marks
 * flow dirty; start/pull_flow resolves the eventual dispatch entry address. */
int gool_native_actor_enter_frame(gool_native_actor *actor,uint32_t argc,uint32_t flags);
/* Original39378 spill/call/reload wrapper while the interpreter is executing.
 * Preserves cached PC/FP, adopts raw SP only, refreshes constant owners.
 * F00 nearest search resolves/pops its operand once, queues GEOMETRY with the
 * actual operand address and yields to the native router before any predicate
 * executes. Other services complete synchronously. Failure retains effects. */
int gool_native_actor_geometry(gool_native_actor *actor,uint32_t instruction,
    const struct gool_native_geometry_services *services);
/* Original38DD8 wrapper: spills SP/FP, invokes3780C/378F8 and adopts actual
 * raw PC/SP/FP. Called by vm.sound while actor is executing. Successful return
 * installs next PC itself; unlike geometry the VM must not increment again. */
int gool_native_actor_sound(gool_native_actor *actor,uint32_t instruction,
    const struct audio_native_services *services);
/* Bind an explicitly selected resident SVTX frame into auxiliary+20/+24.
 * Both pointers select the same frame, retiring any previous morph pair.
 * Retains its generation before publishing; releases previous frame afterward.
 * Quiescent actor only. Selection/decompression/render scheduling is engine
 * policy. Subsequent frame stores must use this owner to preserve lifetime. */
int gool_native_actor_set_frame(gool_native_actor *actor,uint32_t eid,uint32_t index);
/* Retain actual TGEO entry atauxiliary+28 from EID/exact resident handle.
 * reference0 clears the binding and releases
 * old ownership after publishing0. Requires stopped actor and resident data. */
int gool_native_actor_set_geometry(gool_native_actor *actor,uint32_t reference);
/* Native1B524 paired selection from actual object+E8/+EC. Retains the selected
 * SVTX entry across both auxiliary+20/+24 pointers and records raw index+2C.
 * Does not choose animation, advance its clock or invent morph vertices. */
int gool_native_actor_select_frames(gool_native_actor *actor);
/* Native1B1B0..1B300 protected-player bounds from the retained selected SVTX
 * frame. scale is the real native caller's three-word scale vector. Preserves
 * temporary wrappedABS(X), original low32 products/shifts and restoration.
 * Other actors return without player-only stores. No guessed hitbox/extent.
 * On provider failure completed writes remain; owner must stop, not replay. */
int gool_native_actor_frame_bounds(gool_native_actor *actor,uint32_t scale);
/* Native1B0BC type1 model path over READY resources:clear old binding,
 * temporarily retain/publish actual SVTX entry, select morph frames, bind TGEO
 * and update protected-player bounds. Actual E8/EC/descriptor data drives it.
 * No animation choice, synchronous SD fallback or swallowed provider error.
 * On failure keep published actor/resource state for explicit retirement;
 * callers must not replay a failed world update without handling that state. */
int gool_native_actor_prepare_svtx(gool_native_actor *actor,uint32_t scale);
int gool_native_actor_step(gool_native_actor *actor,uint32_t budget);
int gool_native_actor_publish(gool_native_actor *actor);
/* Stop dispatch is not object deletion. Complete native notifications, child
 * and audio/resource retirement separately, then detach after quiescence.
 * Detach refuses executing, retained dispatch calls or borrowed stack storage, copies the
 * final stack to native object backing for slot reuse, and releases leases. */
/* Begin a new native dispatch: import raw registers and constant owners. */
int gool_native_actor_start(gool_native_actor *actor);
int gool_native_actor_stop(gool_native_actor *actor);
int gool_native_actor_detach(gool_native_actor *actor);
/* Native engine/ARRAY callbacks: live SP/FP/PC reads, bounded SP adjustment,
 * exact ordinary word/halfword domain accesses. Executing object register
 * stores change raw bytes, preserving cached native S5/S6/S7 until spill.
 * Foreign/quiescent register stores mark flow dirty; the next dispatch imports
 * actual SP/FP/PC and validates its packed frames before executing. */
int gool_native_registry_read(void *context,uint32_t address,uint32_t *value);
int gool_native_registry_write(void *context,uint32_t address,uint32_t value);
#endif
