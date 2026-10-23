#ifndef GOOL_NATIVE_RESOURCES_H
#define GOOL_NATIVE_RESOURCES_H
#include "gool_resources.h"
#include "gool_domain.h"
enum {GOOL_NATIVE_RESOURCE_INVALID=-102,GOOL_NATIVE_RESOURCE_BUSY=-103};
typedef struct {
    uint8_t entry[44];
    gool_domain_lease leases[4];
    gool_data data;
    uint32_t handle,code_address,model_address,eid,mapped;
} gool_native_behavior;
typedef struct {
    gool_data_source source;
    gool_native_behavior behaviors[GOOL_RESOURCES_SLOTS];
    gool_resources *bank;gool_domain *domain;
    uint8_t *constant_storage;uint32_t constant_capacity,constant_used;
    uint64_t bank_generation,generation;
    uint32_t count,references;
    int open,closing,bank_retained;
} gool_native_resources;

/* Map resident original behavior entries into an explicit native address
 * domain. Virtual entry tables contain real EIDs/type/count and translated
 * item addresses; code/items remain readonly copies from the NSF bank, except
 * constants use caller-owned mutable storage for original142B4 EID fixups.
 * Supply a stable fixed constant pool; exhaustion rejects, never falls back.
 * No original file bytes are patched. Caller supplies an aligned high-bit
 * virtual_base,128KiB per entry, and stable zeroed owner storage. All catalog
 * behaviors must already be READY. The bank is retained throughout mapping.
 * Entry and raw byte views may not be moved while mapped. Open failure can
 * retain cleanup ownership; call close again if its cleanup reports an error.
 */
int gool_native_resources_open(gool_native_resources *resources,gool_resources *bank,
                                 gool_domain *domain,uint32_t virtual_base,void *constant_storage,uint32_t constant_capacity);
int gool_native_resources_retain(gool_native_resources *resources,uint64_t *generation);
int gool_native_resources_release(gool_native_resources *resources,uint64_t generation);
int gool_native_resources_can_close(const gool_native_resources *resources);
int gool_native_resources_close(gool_native_resources *resources);
/* Creation142B4 boundary for preloaded code. Reads native class word, resolves
 * odd EID or existing mapped entry handle, and publishes a handle into a mutable
 * NSD runtime overlay. Missing/unready/readonly domains fail, never fake success.
 * Provider status is separate from native_result. No SD I/O occurs here. */
int gool_native_resources_load(void *context,uint32_t class_word_address,uint32_t *native_result);
int gool_native_resources_behavior(gool_native_resources *resources,uint32_t handle,gool_data *out);
/* Typed code pointer translation. Native formation may be past code end;
 * instruction fetch still validates code_words in the VM. NULL is preserved.
 * Other generic pointers must not be passed through a code-pointer adapter. */
int gool_native_resources_code_to_vm(gool_native_resources *resources,uint32_t address,int32_t *pointer);
int gool_native_resources_code_from_vm(gool_native_resources *resources,int32_t pointer,uint32_t *address);
/* Model offsets need an explicit behavior owner; no active-file guess. */
int gool_native_resources_model(gool_native_resources *resources,uint32_t handle,
                                  uint32_t offset,uint32_t *address);
#endif
