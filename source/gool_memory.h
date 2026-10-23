#ifndef GOOL_MEMORY_H
#define GOOL_MEMORY_H
#include <stdint.h>

#define GOOL_MEMORY_SPANS 32u
enum {GOOL_MEMORY_INVALID=-1,GOOL_MEMORY_UNBOUND=-2,GOOL_MEMORY_READ_ONLY=-3,
      GOOL_MEMORY_CAPACITY=-4};
typedef struct {
    int32_t *words;
    uint32_t base,count;
    int live,writable;
} gool_memory_span;
typedef struct {
    gool_memory_span spans[GOOL_MEMORY_SPANS];
    uint32_t next_address;
} gool_memory;
/* Shared engine address domain, not PS1/native host addresses. Borrowed storage
 * must remain stable until release; release before recycling a delivery job.
 * Handles use tag 7, byte offsets and aligned words. Never reuse addresses in
 * a live domain: stale handles fail after slot reuse. Init only before binding
 * VMs; it starts a new domain and invalidates all old VM references.
 * Zero-length spans have a nonnull handle but cannot be dereferenced. */
void gool_memory_init(gool_memory *memory);
int gool_memory_borrow(gool_memory *memory,int32_t *words,uint32_t count,
                       int writable,int32_t *pointer);
int gool_memory_release(gool_memory *memory,int32_t pointer);
int gool_memory_read(const gool_memory *memory,uint32_t address,int32_t *value);
int gool_memory_write(gool_memory *memory,uint32_t address,int32_t value);
#endif
