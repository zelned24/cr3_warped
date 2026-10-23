/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gool_stack.h"
#ifdef NDEBUG
#error Assertions are required
#endif

int main(void) {
    gool_stack s,before;int32_t value=987;
    gool_stack_reset(&s);
    assert(gool_stack_push(&s,10)==0&&gool_stack_push(&s,20)==0);
    assert(gool_stack_enter(&s,2,0)==0&&s.top==5&&s.depth==1);
    assert(gool_stack_frame_read(&s,-1,&value)==0&&value==20);
    assert(gool_stack_frame_read(&s,-2,&value)==0&&value==10);
    assert(gool_stack_pop(&s,&value)==GOOL_STACK_UNDERFLOW);
    before=s;
    assert(gool_stack_enter(&s,1,0)==GOOL_STACK_UNDERFLOW&&!memcmp(&s,&before,sizeof(s)));
    assert(gool_stack_push(&s,77)==0);
    assert(gool_stack_push(&s,100)==0&&gool_stack_push(&s,200)==0);
    assert(gool_stack_enter(&s,2,0x50000040)==0);
    assert(gool_stack_frame_write(&s,-2,101)==0);
    assert(gool_stack_push(&s,33)==0&&gool_stack_frame_write(&s,3,44)==0);
    assert(gool_stack_pop(&s,&value)==0&&value==44);
    assert(gool_stack_return(&s,&value)==0&&value==0x50000040);
    assert(s.top==6&&s.depth==1&&s.words[6]==101);
    assert(gool_stack_pop(&s,&value)==0&&value==77);
    before=s;value=987;
    assert(gool_stack_return(&s,&value)==GOOL_STACK_DONE&&value==987);
    assert(!memcmp(&s,&before,sizeof(s)));
    assert(gool_stack_frame_write(&s,4,90)==0&&s.top==5);
    assert(gool_stack_frame_read(&s,4,&value)==0&&value==90);
    assert(gool_stack_frame_read(&s,INT32_MIN,&value)==GOOL_STACK_INVALID);
    assert(gool_stack_frame_write(&s,INT32_MAX,2)==GOOL_STACK_INVALID);
    gool_stack_reset(&s);
    for(unsigned i=0;i<GOOL_STACK_WORDS;i++)assert(gool_stack_push(&s,(int32_t)i)==0);
    before=s;
    assert(gool_stack_push(&s,0)==GOOL_STACK_OVERFLOW&&!memcmp(&s,&before,sizeof(s)));
    assert(gool_stack_enter(&s,0,0)==GOOL_STACK_OVERFLOW&&!memcmp(&s,&before,sizeof(s)));
    gool_stack_reset(&s);
    for(unsigned i=0;i<GOOL_STACK_FRAMES;i++)assert(gool_stack_enter(&s,0,(int32_t)i)==0);
    before=s;
    assert(gool_stack_enter(&s,0,0)==GOOL_STACK_OVERFLOW&&!memcmp(&s,&before,sizeof(s)));
    for(unsigned i=GOOL_STACK_FRAMES-1;i>0;i--)
        assert(gool_stack_return(&s,&value)==0&&value==(int32_t)i);
    assert(gool_stack_return(&s,&value)==GOOL_STACK_DONE);
    gool_stack_reset(&s);
    assert(gool_stack_return(&s,&value)==GOOL_STACK_INVALID);
    assert(gool_stack_push(NULL,0)==GOOL_STACK_INVALID);
    gool_stack_reset(&s);assert(gool_stack_set_origin(&s,64)==0);
    assert(gool_stack_push(&s,10)==0&&gool_stack_push(&s,20)==0);
    assert(gool_stack_enter(&s,2,0x50000000)==0);
    assert(s.words[2]==0xffff&&s.words[3]==0x50000000&&s.words[4]==0x100);
    assert(gool_stack_push(&s,77)==0&&gool_stack_push(&s,100)==0&&gool_stack_push(&s,200)==0);
    assert(gool_stack_enter_flags(&s,2,0x50000040,0x12345678)==0);
    assert(s.words[8]==0x12345678&&s.words[9]==0x50000040&&s.words[10]==0x01080118);
    uint32_t flags;
    assert(gool_stack_return_flags(&s,&value,&flags)==0&&value==0x50000040&&flags==0x5678&&s.top==6);
    assert(gool_stack_enter(&s,0,77)==0);
    s.words[s.frames[s.depth-1]+2]=0;before=s;value=987;
    assert(gool_stack_return(&s,&value)==GOOL_STACK_INVALID&&value==987&&!memcmp(&s,&before,sizeof(s)));
    puts("PASS GOOL stack: parameters, nested returns, boundaries, root completion and unchanged failures");
    return 0;
}
