#ifndef MODEL_RESOURCES_H
#define MODEL_RESOURCES_H
#include "zdat_resources.h"
/* Shared NSF entry-bank implementation, configured for readonly SVTX/TGEO.
 * Four fixed64KiB copies; all entry item offsets become actual mapped native
 * pointers. Page/ticket leases return after copying; model generation leases
 * retain resident entries until every actor/frame/GPU reader is quiescent.
 * No vertex decompression, frame guess, automatic eviction or SD blocking.
 * Caller initially zeroes owner, supplies4*64KiB storage and unique base.
 */
typedef zdat_resources model_resources;
typedef zdat_resource_lease model_resource_lease;
int model_resources_open(model_resources *bank,gool_domain *domain,void *storage,uint32_t base);
int model_resources_request(model_resources *bank,uint32_t eid);
int model_resources_poll(model_resources *bank);
int model_resources_acquire(model_resources *bank,uint32_t eid,uint32_t *address,model_resource_lease *lease);
int model_resources_release(model_resources *bank,model_resource_lease lease);
int model_resources_evict(model_resources *bank,uint32_t eid);
int model_resources_can_close(const model_resources *bank);
int model_resources_close(model_resources *bank);
/* Bounded item pointer/size from a live retained model lease. Does not guess
 * an animation frame index; caller supplies the actual selected item. */
int model_resources_item(model_resources *bank,model_resource_lease lease,uint32_t index,
                          uint32_t *address,uint32_t *bytes);
typedef struct {
    model_resource_lease lease;
    uint32_t first,second,index;
} model_frame_selection;
/* Native1B524 selection from actual type1 GOOL model descriptor and raw EC
 * frame word. Descriptor+10 is EID or an existing exact native entry handle.
 * Bit0 atdescriptor+4 selects half-rate morph pairs; descriptor+2 is the real
 * native frame limit. No clamping/default animation. Acquires one lease for
 * both pointers; caller releases/transfers it after actual users stop.
 * selection initially{0}; failures do not publish it or leak the temporary view. */
int model_resources_select_frames(model_resources *bank,uint32_t descriptor,uint32_t frame_word,
                                   int paired,model_frame_selection *selection);
#endif
