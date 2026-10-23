#ifndef GOOL_WORLD_H
#define GOOL_WORLD_H
#include <stdint.h>

/* Native admission primitives, not activation, hierarchy or a full scheduler.
 * Counter units are intentionally unspecified; the original world pass uses
 * an unsigned /34 conversion from *(0x80061a80)+0x10 to 0x80068eec. */
typedef struct {
    uint32_t tick;
    uint32_t effective_request;
    int run_update;
} gool_world_decision;

typedef struct {
    uint32_t counter,world_flags,requested_update;
    uint32_t category,status_100,flags_ac;
} gool_world_input;

/* All providers are required. They operate on the caller's stable real object.
 * bind_current publishes the native current-object context before admission.
 * stamp_update writes LAST_UPDATE only for admitted objects.
 * dispatch executes the selected native admitted/inactive path, including its
 * real logic, physics, transform and presentation services. An inactive object
 * still needs dispatch: rejecting UPDATE does not reject native presentation.
 * The owner supplies native hierarchy order and retains object lifetime during
 * all callbacks. Budget continuation and provider failures belong to the owner;
 * this entry must not be replayed as a VM budget continuation. No fallback
 * physics, object activation, timestamp writes or service defaults are supplied.
 * A provider failure is returned unchanged; prior side effects remain visible. */
typedef struct {
    void *context;
    int (*bind_current)(void *context,void *object);
    int (*stamp_update)(void *context,void *object,uint32_t tick);
    int (*dispatch)(void *context,void *object,const gool_world_decision *decision);
} gool_world_services;

enum {GOOL_WORLD_INVALID=-1};
uint32_t gool_world_tick(uint32_t counter);
gool_world_decision gool_world_admit(gool_world_input input);
typedef struct {
    uint32_t category; /* *(object+0x0c)->item0 +4 */
    uint32_t world_flags; /* 0x80068f1c, also native globals+0x84 */
    uint32_t requested_update; /* live S1 at1D910, including prior UPDATE override */
    uint32_t status_100,flags_ac; /* object+0x100,+0xac */
    uint32_t model,geometry_format,mode; /* object+0xe8,+0x120 low byte,+0x108 */
    uint32_t engine_69034; /* category100 gate: globals+0x19c, bit0 */
    uint32_t engine_6087c; /* category100 gate: actual word0x8006087c */
    uint32_t engine_68f28; /* category200 gate: globals+0x90 */
} gool_world_presentation_input;
/* Native1D910..1DB18 admission for presentation call1B7A0. Pure predicate;
 * caller supplies actual reads, holds object/code lifetime and performs final
 * current-object clearing itself. Does not infer category or engine defaults.
 * Geometry guard tests the native low byte. Unknown categories start denied,
 * even when requested_update is nonzero; explicit visibility override can admit.
 */
int gool_world_presentation_admit(gool_world_presentation_input input);
int gool_world_step(void *object,const gool_world_input *input,
                    const gool_world_services *services);
#endif
