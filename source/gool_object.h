#ifndef GOOL_OBJECT_H
#define GOOL_OBJECT_H
#include "gool_vm.h"

#define GOOL_OBJECT_MEMBERS 512u
typedef struct {
    int32_t members[GOOL_OBJECT_MEMBERS];
    uint32_t known[GOOL_OBJECT_MEMBERS/32];
    gool_vm *vm;
    gool_memory *stack_memory;
    int32_t stack_pointer;
    uint16_t lighting[16]; /* native object+0x20..0x3f; no invented defaults */
    uint16_t lighting_known;
} gool_object;
typedef struct {
    gool_object **objects;
    uint32_t count,self,player;
    int has_player;
} gool_object_context;

/* Caller owns object table and objects. Null slots are not live references.
 * Unknown engine members stay unbound; explicit zero is a valid known value.
 * Object pointers use base 6 and table index, never native addresses. */
void gool_object_reset(gool_object *object,gool_vm *vm);
int gool_object_set(gool_object *object,uint32_t member,int32_t value);
int gool_object_get(const gool_object *object,uint32_t member,int32_t *value);
/* Seed/read the native signed halfwords preceding the word-member region.
 * Selectors16..63 alias the existing word members through the callbacks;
 * structural pointer writes still require their explicit engine provider. */
int gool_object_lighting_set(gool_object *object,uint32_t selector,int32_t value);
int gool_object_lighting_get(const gool_object *object,uint32_t selector,int32_t *value);
int gool_object_pointer(uint32_t index,int32_t *value);
/* Optional stable stack mapping. Costs one shared span; release before object
 * reset/recycling, VM replacement or memory-domain destruction. No automatic
 * registration: a full domain reports capacity rather than aliasing storage.
 * Owner and reading VMs must already bind the same memory domain. */
int gool_object_bind_stack(gool_object *object,gool_memory *memory);
int gool_object_unbind_stack(gool_object *object);
int gool_object_read(void *context,const gool_operand *operand,int32_t *value);
int gool_object_write(void *context,const gool_operand *operand,int32_t value);
/* Explicit begin-update metadata, not an automatic scheduler. It supplies
 * LAST_UPDATE and logic tick only when the engine actually starts an update. */
int gool_object_begin_update(gool_object *object,uint64_t tick);
#endif
