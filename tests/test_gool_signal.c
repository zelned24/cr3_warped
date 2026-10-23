/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "gool_signal.h"
#ifdef NDEBUG
#error Assertions are required
#endif
int main(void) {
    uint8_t indices[]={255,0,1,0,4,128};
    const uint8_t code[32]={0},states[32]={0};
    gool_data base={.indices=indices,.signal_count=3,.states=states,.state_count=2,
        .code=code,.code_words=8},owner;
    gool_signal_route route;uint32_t pc=987;
    assert(gool_signal_lookup(&base,0,&route)==0&&route.kind==GOOL_SIGNAL_NONE);
    assert(gool_signal_lookup(&base,0x100,&route)==0&&route.kind==GOOL_SIGNAL_STATE&&route.value==1);
    for(uint32_t low=0;low<256;low++) {
        assert(gool_signal_lookup(&base,0x100|low,&route)==0);
        assert(route.signal==1&&route.value==1);
    }
    assert(gool_signal_lookup(&base,0x200,&route)==0&&route.kind==GOOL_SIGNAL_SCRIPT);
    assert(route.raw==0x8004&&route.value==4);
    assert(gool_signal_script(&base,&route,&owner,&pc)==0&&pc==4&&owner.code==code);
    assert(gool_signal_lookup(&base,0x80000000,&route)==0&&route.kind==GOOL_SIGNAL_OUTSIDE_TABLE);
    indices[5]=0xc0;
    assert(gool_signal_lookup(&base,0x200,&route)==0);
    assert(gool_signal_script(&base,&route,&owner,&pc)==0&&pc==4&&owner.code==code);
    route.value=5;
    assert(gool_signal_script(&base,&route,&owner,&pc)==-1);
    indices[4]=8;indices[5]=0x80;
    assert(gool_signal_lookup(&base,0x200,&route)==0);
    assert(gool_signal_script(&base,&route,&owner,&pc)==-1);
    indices[2]=2;route.kind=GOOL_SIGNAL_NONE;route.value=987;
    assert(gool_signal_lookup(&base,0x100,&route)==-1&&route.kind==GOOL_SIGNAL_NONE&&route.value==987);
    gool_data library={0};
    assert(gool_signal_lookup(&library,0,&route)==0&&route.kind==GOOL_SIGNAL_OUTSIDE_TABLE);
    puts("PASS signal routing only; delivery and explicit-code calling convention pending");return 0;
}
