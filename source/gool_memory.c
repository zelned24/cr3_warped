#include "gool_memory.h"
#include <string.h>

void gool_memory_init(gool_memory *memory) {
    if(!memory)return;
    memset(memory,0,sizeof(*memory));memory->next_address=0x70000004;
}
int gool_memory_borrow(gool_memory *memory,int32_t *words,uint32_t count,
                       int writable,int32_t *pointer) {
    if(!memory||!pointer||(count&&!words)||memory->next_address<0x70000004u||
       memory->next_address>0x80000000u||(memory->next_address&3))return GOOL_MEMORY_INVALID;
    uint64_t end=(uint64_t)memory->next_address+(uint64_t)count*4+4;
    if(end>0x80000000u)return GOOL_MEMORY_CAPACITY;
    for(uint32_t n=0;n<GOOL_MEMORY_SPANS;n++) {
        gool_memory_span *span=&memory->spans[n];
        if(span->live)continue;
        *span=(gool_memory_span){.words=words,.base=memory->next_address,
            .count=count,.live=1,.writable=writable!=0};
        *pointer=(int32_t)span->base;memory->next_address=(uint32_t)end;return 0;
    }
    return GOOL_MEMORY_CAPACITY;
}
static const gool_memory_span *resolve(const gool_memory *memory,uint32_t address) {
    if(!memory||(address>>28)!=7||(address&3))return NULL;
    for(uint32_t n=0;n<GOOL_MEMORY_SPANS;n++) {
        const gool_memory_span *span=&memory->spans[n];
        if(span->live&&address>=span->base&&(address-span->base)/4<span->count)return span;
    }
    return NULL;
}
int gool_memory_release(gool_memory *memory,int32_t pointer) {
    if(!memory)return GOOL_MEMORY_INVALID;
    for(uint32_t n=0;n<GOOL_MEMORY_SPANS;n++) {
        gool_memory_span *span=&memory->spans[n];
        if(span->live&&span->base==(uint32_t)pointer) {
            memset(span,0,sizeof(*span));return 0;
        }
    }
    return GOOL_MEMORY_UNBOUND;
}
int gool_memory_read(const gool_memory *memory,uint32_t address,int32_t *value) {
    if(!value)return GOOL_MEMORY_INVALID;
    const gool_memory_span *span=resolve(memory,address);
    if(!span)return GOOL_MEMORY_UNBOUND;
    *value=span->words[(address-span->base)/4];return 0;
}
int gool_memory_write(gool_memory *memory,uint32_t address,int32_t value) {
    const gool_memory_span *span=resolve(memory,address);
    if(!span)return GOOL_MEMORY_UNBOUND;
    if(!span->writable)return GOOL_MEMORY_READ_ONLY;
    span->words[(address-span->base)/4]=value;return 0;
}
