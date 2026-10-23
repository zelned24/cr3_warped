#include "gool_operand.h"

static int32_t extend(uint32_t raw,unsigned bits) {
    uint32_t mask=(1u<<bits)-1u,value=raw&mask;
    return value&(1u<<(bits-1))?(int32_t)value-(int32_t)(1u<<bits):(int32_t)value;
}
int gool_operand_decode(uint32_t raw,gool_operand *out) {
    if(!out||raw>=4096)return GOOL_ARG_INVALID;
    gool_operand arg={0};
    if(!(raw&0x800)) {
        arg.kind=GOOL_ARG_CONSTANT;arg.value=(int32_t)(raw&0x3ff);arg.base=(raw>>10)&1;
    } else if(raw&0x400) {
        if(raw&0x200){arg.kind=GOOL_ARG_VARIABLE;arg.value=(int32_t)(raw&0x1ff);}
        else {arg.kind=GOOL_ARG_INDIRECT;arg.value=(int32_t)(raw&0x3f);arg.base=(raw>>6)&7;}
    } else if(!(raw&0x200)) {arg.kind=GOOL_ARG_LITERAL;arg.value=extend(raw,9)*256;}
    else if(!(raw&0x100)) {arg.kind=GOOL_ARG_LITERAL;arg.value=extend(raw,8)*16;}
    else if(!(raw&0x80)) {
        arg.kind=GOOL_ARG_FRAME;arg.value=extend(raw,7);
    } else {arg.kind=GOOL_ARG_BOOLEAN;arg.value=(raw&0x10)?1:0;}
    *out=arg;return GOOL_ARG_OK;
}
int gool_operand_simple(uint32_t raw,uint32_t *out) {
    if(!out||raw>=4096)return GOOL_ARG_INVALID;
    raw|=0x800;
    *out=(raw&0x400)?raw:(raw&~0x80u)|0x300;
    return GOOL_ARG_OK;
}
int gool_operand_read(const gool_operand_context *ctx,uint32_t raw,int32_t *value) {
    if(!ctx||!value)return GOOL_ARG_INVALID;
    gool_operand arg;
    int result=gool_operand_decode(raw,&arg);
    if(result)return result;
    if(arg.kind==GOOL_ARG_LITERAL||arg.kind==GOOL_ARG_BOOLEAN){*value=arg.value;return GOOL_ARG_OK;}
    if(arg.kind==GOOL_ARG_CONSTANT) {
        const gool_data *source=arg.base?
            (ctx->constant_alternate?ctx->constant_alternate:ctx->alternate):
            (ctx->constant_base?ctx->constant_base:ctx->base);
        uint32_t bits;
        if(!source)return GOOL_ARG_UNBOUND;
        if(gool_data_constant(source,(uint32_t)arg.value,&bits))return GOOL_ARG_RANGE;
        /* Preserve the original signed word without implementation-defined
         * unsigned-to-signed conversion or signed shift overflow. */
        *value=bits<=INT32_MAX?(int32_t)bits:(int32_t)((int64_t)bits-INT64_C(4294967296));
        return GOOL_ARG_OK;
    }
    if(!ctx->read)return GOOL_ARG_UNBOUND;
    int32_t next;
    result=ctx->read(ctx->context,&arg,&next);
    if(!result)*value=next;
    return result;
}
int gool_operand_write(const gool_operand_context *ctx,uint32_t raw,int32_t value) {
    if(!ctx)return GOOL_ARG_INVALID;
    gool_operand arg;
    int result=gool_operand_decode(raw,&arg);
    if(result)return result;
    if(arg.kind==GOOL_ARG_CONSTANT||arg.kind==GOOL_ARG_LITERAL||arg.kind==GOOL_ARG_BOOLEAN)
        return GOOL_ARG_READ_ONLY;
    /* Native fast stores address object members directly, including SCRIPT_PTR.
     * The engine provider owns cached dispatch-register versus raw-memory
     * semantics. Legacy providers may still reject an unbound register store. */
    if(!ctx->write)return GOOL_ARG_UNBOUND;
    return ctx->write(ctx->context,&arg,value);
}

int gool_operand_address(const gool_operand_context *ctx,uint32_t raw,
                         uint32_t scratch_offset,uint32_t *address) {
    if(!ctx||!address||(scratch_offset!=0x18&&scratch_offset!=0x1c))return GOOL_ARG_INVALID;
    gool_operand arg;int error=gool_operand_decode(raw,&arg);if(error)return error;
    if(!ctx->address)return GOOL_ARG_UNBOUND;
    uint32_t next;error=ctx->address(ctx->context,&arg,scratch_offset,&next);
    if(!error)*address=next;
    return error;
}
