#ifndef GOOL_OPERAND_H
#define GOOL_OPERAND_H
#include "gool_data.h"

typedef enum { GOOL_ARG_CONSTANT,GOOL_ARG_VARIABLE,GOOL_ARG_INDIRECT,
               GOOL_ARG_LITERAL,GOOL_ARG_FRAME,GOOL_ARG_BOOLEAN,
               GOOL_ARG_HALFWORD } gool_arg_kind;
typedef struct {gool_arg_kind kind;int32_t value;uint32_t base;} gool_operand;
enum {GOOL_ARG_OK=0,GOOL_ARG_INVALID=-1,GOOL_ARG_UNBOUND=-2,
      GOOL_ARG_READ_ONLY=-3,GOOL_ARG_RANGE=-4};

/* Callback must implement real member/frame/object semantics, including
 * stack pop/push at member 0x1F. Missing callbacks return UNBOUND, never zero.
 * For INDIRECT base identifies the link; VARIABLE/FRAME base is zero.
 * No output value is modified on a decoder/lookup failure. */
typedef int (*gool_arg_read_fn)(void *context,const gool_operand *operand,int32_t *value);
typedef int (*gool_arg_write_fn)(void *context,const gool_operand *operand,int32_t value);
typedef int (*gool_arg_address_fn)(void *context,const gool_operand *operand,
                                    uint32_t scratch_offset,uint32_t *address);
typedef struct {
    const gool_data *base,*alternate;
    void *context;
    gool_arg_read_fn read;
    gool_arg_write_fn write;
    /* Native dispatch loads base constants through object+10, whereas code
     * pointer formation uses object+C. NULL retains the legacy shared owner. */
    const gool_data *constant_base,*constant_alternate;
    gool_arg_address_fn address;
} gool_operand_context;

int gool_operand_decode(uint32_t raw,gool_operand *out);
int gool_operand_simple(uint32_t raw,uint32_t *out);
int gool_operand_read(const gool_operand_context *context,uint32_t raw,int32_t *value);
/* Native38B40 read-address mode: STACK pops; literal storage uses the
 * caller's explicit scratch slot18/1C; boolean forms addresses0/1. */
int gool_operand_address(const gool_operand_context *context,uint32_t raw,
                         uint32_t scratch_offset,uint32_t *address);
int gool_operand_write(const gool_operand_context *context,uint32_t raw,int32_t value);
#endif
