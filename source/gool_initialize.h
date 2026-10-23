#ifndef GOOL_INITIALIZE_H
#define GOOL_INITIALIZE_H
#include <stdint.h>

enum {GOOL_INITIALIZE_DONE=0,GOOL_INITIALIZE_BUDGET=1,GOOL_INITIALIZE_WAIT=2,
      GOOL_INITIALIZE_INVALID=-84,GOOL_INITIALIZE_RANGE=-85,GOOL_INITIALIZE_ASYNC=-86};
#define GOOL_INITIALIZE_PROVIDER_PENDING 1
typedef struct {
    void *context;
    int (*read)(void *context,uint32_t address,uint32_t *value);
    int (*write)(void *context,uint32_t address,uint32_t value);
    /* Native32DF8 key47, value0/time0/mode2/default0. */
    int (*geometry)(void *context,uint32_t database,uint32_t *value);
    /* Native142B4(class_word_address,1,0). Result is the original resolved
     * behavior handle or native failure word; provider status is separate.
     * Keep its resource lease alive through the object/state lifetime. */
    int (*load)(void *context,uint32_t class_word_address,uint32_t *native_result);
    /* Native1D2D4(node,state,argc,args). Completes real state/frame setup;
     * native return word is separate from provider status. Return PENDING
     * only to resume the same retained state job on the next callback. */
    int (*enter_state)(void *context,uint32_t node,uint32_t state,uint32_t argc,
                         uint32_t arguments,uint32_t *native_result);
} gool_initialize_services;
typedef struct {
    uint32_t node,class_index,object_id,argument_count,arguments,placed;
} gool_initialize_request;

typedef struct {
    gool_initialize_request request;
    gool_initialize_services services;
    uint32_t state,phase;
    int active,status;
    int32_t native_result;
} gool_initialize;
/* Stable cursor and providers persist through pending state dispatch. The
 * initialization prefix executes once; only enter_state is re-polled while
 * pending. Keep native objects, argument spans and resource leases retained. */
int gool_initialize_begin(gool_initialize *initialize,const gool_initialize_request *request,
                            const gool_initialize_services *services);
int gool_initialize_step(gool_initialize *initialize,uint32_t work_budget);
/* Native1A5C8 initialization, including both placed/child paths, parent
 * transform inheritance, native default writes, behavior lookup and initial
 * object-state selection. Synchronous convenience requires providers that
 * finish without PENDING; asynchronous owners use begin/step above.
 * No heap allocation, guessed members, no-op state entry or implicit loading.
 * Explicit engine memory must map NSD base at80061044, reserved slot at80068E98,
 * object storage and resolved entry/header/index spans. Immutable archive bytes
 * are exposed through a runtime address domain; metadata is never cast to a
 * host pointer. Object type+4 must already be3 as established by creation.
 * Native result: -255 initialized,-27 no-state255,-14 failed admission/load.
 * Provider failure preserves native_result output; previous object/provider
 * side effects remain visible and cannot be retried as a fresh initialization.
 * Invalid class/index/address extents reject before unsafe native accesses.
 * This is not complete placement creation, physical frame scheduling or the
 * existing high-level gool_instance API; its providers still need engine binding.
 */
int gool_initialize_native(const gool_initialize_request *request,
    const gool_initialize_services *services,int32_t *native_result);
#endif
