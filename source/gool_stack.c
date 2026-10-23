#include "gool_stack.h"
#include <string.h>

uint32_t gool_stack_capacity(const gool_stack *s) {
    return s?(s->capacity?s->capacity:GOOL_STACK_WORDS):0;
}
static int valid(const gool_stack *s) {
    uint32_t capacity=gool_stack_capacity(s);
    if(!s||capacity<GOOL_FRAME_WORDS||capacity>GOOL_STACK_WORDS||s->top>capacity||s->depth>GOOL_STACK_FRAMES||
       !s->origin||(s->origin&3)||s->origin>65536u-capacity*4)return 0;
    if(s->depth) {
        uint32_t frame=s->frames[s->depth-1];
        if(frame>capacity-GOOL_FRAME_WORDS||s->top<frame+GOOL_FRAME_WORDS)return 0;
    }
    return 1;
}
static uint32_t floor_index(const gool_stack *s) {
    return s->depth?s->frames[s->depth-1]+GOOL_FRAME_WORDS:0;
}
void gool_stack_reset(gool_stack *s) {if(s){memset(s,0,sizeof(*s));s->origin=4;s->capacity=GOOL_STACK_WORDS;}}
int gool_stack_set_origin(gool_stack *s,uint32_t start) {
    if(!valid(s)||s->top||s->depth||!start||start>16384u-gool_stack_capacity(s))return GOOL_STACK_INVALID;
    s->origin=start*4;return GOOL_STACK_OK;
}
int gool_stack_set_capacity(gool_stack *s,uint32_t words) {
    if(!valid(s)||words<GOOL_FRAME_WORDS||words>GOOL_STACK_WORDS||s->top>words||
       s->origin>65536u-words*4)return GOOL_STACK_INVALID;
    for(uint32_t i=0;i<s->depth;i++)
        if(s->frames[i]>words-GOOL_FRAME_WORDS)return GOOL_STACK_INVALID;
    s->capacity=words;return GOOL_STACK_OK;
}
static int32_t signed_word(uint32_t n) {
    return n<=INT32_MAX?(int32_t)n:(int32_t)((int64_t)n-INT64_C(4294967296));
}
uint32_t gool_stack_height(const gool_stack *s) {
    return valid(s)?s->top-floor_index(s):0;
}
int gool_stack_push(gool_stack *s,int32_t value) {
    if(!valid(s))return GOOL_STACK_INVALID;
    if(s->top==gool_stack_capacity(s))return GOOL_STACK_OVERFLOW;
    s->words[s->top++]=value;return GOOL_STACK_OK;
}
int gool_stack_reserve(gool_stack *s) {
    if(!valid(s))return GOOL_STACK_INVALID;
    if(s->top==gool_stack_capacity(s))return GOOL_STACK_OVERFLOW;
    s->top++;return GOOL_STACK_OK;
}
int gool_stack_pop(gool_stack *s,int32_t *value) {
    if(!valid(s)||!value)return GOOL_STACK_INVALID;
    if(s->top==floor_index(s))return GOOL_STACK_UNDERFLOW;
    *value=s->words[--s->top];return GOOL_STACK_OK;
}
int gool_stack_enter(gool_stack *s,uint32_t argc,int32_t return_pointer) {
    return gool_stack_enter_flags(s,argc,return_pointer,0xffff);
}
int gool_stack_enter_flags(gool_stack *s,uint32_t argc,int32_t return_pointer,uint32_t flags) {
    if(!valid(s))return GOOL_STACK_INVALID;
    if(argc>s->top-floor_index(s))return GOOL_STACK_UNDERFLOW;
    if(s->depth==GOOL_STACK_FRAMES||s->top>gool_stack_capacity(s)-GOOL_FRAME_WORDS)return GOOL_STACK_OVERFLOW;
    uint32_t previous=s->depth?s->origin+s->frames[s->depth-1]*4:0;
    uint32_t arguments=s->origin+(s->top-argc)*4;
    s->frames[s->depth++]=s->top;
    s->words[s->top++]=signed_word(flags);
    s->words[s->top++]=return_pointer;
    s->words[s->top++]=signed_word(previous<<16|arguments);
    return GOOL_STACK_OK;
}
int gool_stack_return(gool_stack *s,int32_t *return_pointer) {
    uint32_t flags;return gool_stack_return_flags(s,return_pointer,&flags);
}
int gool_stack_return_flags(gool_stack *s,int32_t *return_pointer,uint32_t *flags) {
    if(!valid(s)||!return_pointer||!flags||!s->depth)return GOOL_STACK_INVALID;
    if(s->depth==1)return GOOL_STACK_DONE;
    uint32_t frame=s->frames[s->depth-1];
    uint32_t previous=s->frames[s->depth-2];
    uint32_t links=(uint32_t)s->words[frame+2],arguments=links&0xffff;
    if(previous>gool_stack_capacity(s)-GOOL_FRAME_WORDS||frame<previous+GOOL_FRAME_WORDS||
       (links>>16)!=s->origin+previous*4||(arguments&3)||arguments<s->origin||
       arguments<s->origin+(previous+GOOL_FRAME_WORDS)*4||arguments>s->origin+frame*4)
        return GOOL_STACK_INVALID;
    int32_t target=s->words[frame+1];
    uint32_t saved_flags=(uint32_t)s->words[frame]&0xffff;
    s->top=(arguments-s->origin)/4;s->depth--;
    *flags=saved_flags;
    *return_pointer=target;return GOOL_STACK_OK;
}
static int frame_slot(const gool_stack *s,int32_t offset,uint32_t *index) {
    if(!valid(s)||!s->depth)return GOOL_STACK_INVALID;
    int64_t slot=(int64_t)s->frames[s->depth-1]+offset;
    if(slot<0||slot>=gool_stack_capacity(s))return GOOL_STACK_INVALID;
    *index=(uint32_t)slot;return GOOL_STACK_OK;
}
int gool_stack_frame_read(const gool_stack *s,int32_t offset,int32_t *value) {
    if(!value)return GOOL_STACK_INVALID;
    uint32_t slot;int result=frame_slot(s,offset,&slot);
    if(result)return result;
    *value=s->words[slot];return GOOL_STACK_OK;
}
int gool_stack_frame_write(gool_stack *s,int32_t offset,int32_t value) {
    uint32_t slot;int result=frame_slot(s,offset,&slot);
    if(result)return result;
    s->words[slot]=value;return GOOL_STACK_OK;
}

int gool_stack_import(gool_stack *s,uint32_t origin,uint32_t capacity,uint32_t sp,uint32_t fp) {
    if(!s||capacity<GOOL_FRAME_WORDS||capacity>GOOL_STACK_WORDS||!origin||
       ((origin|sp|fp)&3u)||origin>65536u-capacity*4||sp<origin||
       sp-origin>capacity*4)return GOOL_STACK_INVALID;
    uint32_t frames[GOOL_STACK_FRAMES],depth=0,top=(sp-origin)/4;
    uint32_t current=fp,ceiling=top;
    while(current) {
        if(depth==GOOL_STACK_FRAMES||current<origin||current-origin>(capacity-GOOL_FRAME_WORDS)*4)
            return GOOL_STACK_INVALID;
        uint32_t index=(current-origin)/4;
        if(index+GOOL_FRAME_WORDS>ceiling)return GOOL_STACK_INVALID;
        uint32_t links=(uint32_t)s->words[index+2],previous=links>>16,arguments=links&0xffffu;
        if((previous|arguments)&3u||arguments<origin||arguments>current||
           (previous&&(previous<origin||previous>=current||
                       previous+GOOL_FRAME_WORDS*4>arguments)))return GOOL_STACK_INVALID;
        frames[depth++]=index;ceiling=index;current=previous;
    }
    s->origin=origin;s->capacity=capacity;s->top=top;s->depth=depth;
    for(uint32_t i=0;i<depth;i++)s->frames[i]=frames[depth-i-1];
    for(uint32_t i=depth;i<GOOL_STACK_FRAMES;i++)s->frames[i]=0;
    return GOOL_STACK_OK;
}
