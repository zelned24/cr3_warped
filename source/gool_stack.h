#ifndef GOOL_STACK_H
#define GOOL_STACK_H
#include <stdint.h>

/* SCUS-94244 0x8001d588: flags, return PC, packed previous-frame/argument-base
 * byte offsets relative to object+0x40. Capacities remain port budgets. */
#define GOOL_STACK_WORDS 256u
#define GOOL_STACK_FRAMES 32u
#define GOOL_FRAME_WORDS 3u
typedef struct {
    int32_t words[GOOL_STACK_WORDS];
    uint32_t frames[GOOL_STACK_FRAMES];
    uint32_t top,depth,origin,capacity; /* origin: bytes from object+0x40 to words[0] */
} gool_stack;
enum { GOOL_STACK_OK=0,GOOL_STACK_DONE=1,GOOL_STACK_INVALID=-1,
       GOOL_STACK_OVERFLOW=-2,GOOL_STACK_UNDERFLOW=-3 };
void gool_stack_reset(gool_stack *stack);
/* Set actual GOOL stackStart (member index), before pushing/entering.
 * Standalone reset uses virtual member 1; entity VMs must bind their header. */
uint32_t gool_stack_capacity(const gool_stack *stack);
/* Restrict storage to the native object extent; existing frames must fit. */
int gool_stack_set_capacity(gool_stack *stack,uint32_t words);
int gool_stack_set_origin(gool_stack *stack,uint32_t stack_start);
/* Import canonical object register offsets over existing words. Walk packed
 * previous-frame links back to native root0; reject cycles, crossed headers,
 * invalid argument bases and out-of-slot SP/FP before replacing metadata.
 * origin/sp/fp are byte offsets relative to object+0x40. fp0 means no frame.
 * words are not cleared or copied; caller has populated actual native storage. */
int gool_stack_import(gool_stack *stack,uint32_t origin,uint32_t capacity,
                      uint32_t sp,uint32_t fp);
int gool_stack_push(gool_stack *stack,int32_t value);
/* Advance without overwriting the existing slot: native destination allocation
 * and explicit result-slot reservation. Caller performs the eventual store. */
int gool_stack_reserve(gool_stack *stack);
int gool_stack_pop(gool_stack *stack,int32_t *value);
/* Args already at top. Ordinary engine entry stores flags=0xffff. */
int gool_stack_enter(gool_stack *stack,uint32_t argc,int32_t return_pointer);
int gool_stack_enter_flags(gool_stack *stack,uint32_t argc,int32_t return_pointer,uint32_t flags);
int gool_stack_return(gool_stack *stack,int32_t *return_pointer);
int gool_stack_return_flags(gool_stack *stack,int32_t *return_pointer,uint32_t *flags);
/* Raw signed frame offsets: 0..2 are header words, 3 begins locals. */
int gool_stack_frame_read(const gool_stack *stack,int32_t offset,int32_t *value);
int gool_stack_frame_write(gool_stack *stack,int32_t offset,int32_t value);
uint32_t gool_stack_height(const gool_stack *stack);
#endif
