#ifndef GOOL_INSTANCE_H
#define GOOL_INSTANCE_H
#include "gool_object.h"

typedef struct {
    uint32_t behavior_eid,class_index,object_id,path_points,geo_format;
    int32_t position[3]; /* raw fixed-point placement; no guessed rescaling */
    const int32_t *parameters;
    uint32_t parameter_count;
    uint64_t tick;
} gool_placement;

typedef struct {
    gool_object object;
    gool_vm vm;
    gool_data base,alternate,pre_run,resume_run;
    gool_object_context context;
    uint32_t state,pre_pc,resume_pc,handler_depth,phase;
    uint64_t update_tick,last_tick;
    uint64_t state_generation; /* port bookkeeping, not a GOOL-visible variable */
    int ready,active,has_pre_run,busy,has_last_tick,has_resume,resume_active;
} gool_instance;

enum {GOOL_INSTANCE_SKIPPED=3};
/* Initialize instance to {0}. Table[self] must point to &instance.object.
 * The instance cannot be copied/moved after init: internal views/callbacks
 * point into its stable storage. Requires local original state owners. */
int gool_instance_init(gool_instance *instance,const cga_archive *archive,
                       const gool_object_context *context,const gool_placement *placement);
/* Retained original NSF behavior view; source resolver and all resolved base/
 * alternate/code-owner leases must outlive the instance. No whole CGA needed. */
int gool_instance_init_data(gool_instance *instance,const gool_data *base,
                            const gool_object_context *context,const gool_placement *placement);
/* Native engine entry after caller checked compatibility. Does not recheck
 * INTERACT/MOTION or run pre_run; resets root from external args. Without
 * RESUME seeds zero; otherwise stages the OLD resolved code before changing
 * alternate owner and clears RESUME. Owner must finish this entry before
 * advancing tick. Used by opcode wrapper and signal service. */
int gool_instance_enter_state(gool_instance *instance,uint32_t state,
                              const int32_t *arguments,uint32_t argument_count);
/* Start the pending old-RESUME code view, before pre_run or a world tick
 * advances. Runs later under the owner's budget in native mode 0x13. */
int gool_instance_start_resume(gool_instance *instance);
/* Per-entity update driver, not world scheduling/physics. pre_run executes
 * first even if main is waiting. Budget continuation must use the same tick.
 * No entity is silently advanced to the next tick on budget exhaustion.
 * SET_STATE/TRUE/FALSE replace the root frame and enter the new pre_run;
 * incompatible/false transitions discard arguments without replacing state.
 * Opcode compatibility requires explicit engine member 0x32 and adds 0x402
 * for values 2..4 on any object. False condition skips state tables/providers.
 * Arguments are consumed before compatibility; errors after that keep discard. */
/* Per-entity PRE_RUN uses mode 3, main UPDATE mode 4. State initialization
 * without RESUME seeds zero; normal RESUME runs in mode 0x13 before pre_run
 * and RETURN seeds zero. State entry nested during RESUME is unsupported.
 * This is not the world scheduler/physics or nested signal delivery. */
int gool_instance_update(gool_instance *instance,uint64_t tick,uint32_t budget);
#endif
