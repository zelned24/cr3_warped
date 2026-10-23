/* Production C regression harness; run through tests/test_gool_c.py. */
#include <assert.h>
#include <stdio.h>
#include "runtime_archive.c"
#include "gool_data.c"
#ifdef NDEBUG
#error Assertions are required
#endif

int main(int argc,char **argv) {
    assert(argc>1);
    unsigned entries=0,states=0,missing=0,libraries=0;
    for(int arg=1;arg<argc;arg++) {
        FILE *f=fopen(argv[arg],"rb");assert(f);
        uint8_t header[32];assert(fread(header,1,32,f)==32);assert(fclose(f)==0);
        cga_archive archive={0};assert(cga_open(&archive,argv[arg],rd32(header+8))==0);
        for(uint32_t i=0;i<archive.count;i++) {
            cga_entry entry;assert(cga_entry_at(&archive,i,&entry)==0);
            gool_data d={0};
            if(entry.type!=11) {assert(gool_data_open(&d,&archive,entry.eid)==-1);continue;}
            assert(gool_data_open(&d,&archive,entry.eid)==0);entries++;
            gool_data by_lookup;
            assert(gool_data_lookup(&by_lookup,&archive,d.lookup_index)==0&&by_lookup.eid==d.eid);
            assert(d.entry==i&&d.eid==entry.eid&&d.archive==&archive);
            uint32_t value;
            for(uint32_t k=0;k<d.constant_words;k++) {
                assert(gool_data_constant(&d,k,&value)==0&&value==word(d.constants+k*4));
            }
            assert(gool_data_constant(&d,d.constant_words,&value)==-1);
            for(uint32_t k=0;k<d.code_words;k++) {
                assert(gool_data_instruction(&d,k,&value)==0&&value==word(d.code+k*4));
            }
            assert(gool_data_instruction(&d,d.code_words,&value)==-1);
            int32_t signed_value;
            for(uint32_t k=0;k<d.signal_count;k++) {
                assert(gool_data_signal(&d,k,&signed_value)==0);
                assert(signed_value==signed_half(d.indices+k*2));
            }
            for(uint32_t k=0;k<d.object_count;k++) {
                assert(gool_data_object(&d,k,&signed_value)==0);
                assert(signed_value==signed_half(d.indices+(d.signal_count+k)*2));
            }
            assert(gool_data_signal(&d,d.signal_count,&signed_value)==-1);
            assert(gool_data_object(&d,d.object_count,&signed_value)==-1);
            if(entry.section_count==3) {
                libraries++;assert(!d.states&&!d.indices&&!d.state_count&&!d.signal_count);
                assert(d.declared_signals==81);
            }
            gool_state_data s;
            assert(gool_data_state(&d,d.state_count,&s)==-1);
            for(uint32_t k=0;k<d.state_count;k++) {
                states++;assert(gool_data_state(&d,k,&s)==0);
                const uint8_t *raw=d.states+k*16;
                assert(s.motion_flags==word(raw)&&s.interact_flags==word(raw+4));
                assert(s.alternate_eid==word(d.constants+s.alternate_index*4));
                for(int kind=GOOL_SCRIPT_MAIN;kind<=GOOL_SCRIPT_SIGNAL;kind++) {
                    uint16_t address=kind==GOOL_SCRIPT_MAIN?s.script:
                        kind==GOOL_SCRIPT_PRE_RUN?s.pre_run:s.on_signal;
                    gool_data owner={0};uint32_t pc=UINT32_MAX;
                    int result=gool_data_script(&d,k,(gool_script_kind)kind,&owner,&pc);
                    if(kind!=GOOL_SCRIPT_MAIN&&address==0x3fff) {
                        assert(result==1&&owner.archive==NULL&&pc==UINT32_MAX);
                    } else if(result<0) {
                        uint32_t alternate_index;
                        assert((address&0x4000)&&cga_find(&archive,s.alternate_eid,&alternate_index)==-1);
                        assert(owner.archive==NULL&&pc==UINT32_MAX);missing++;
                    } else {
                        assert(result==0&&owner.archive==&archive&&pc==(address&0x3fff));
                        assert(owner.eid==((address&0x4000)?s.alternate_eid:d.eid));
                        assert(gool_data_instruction(&owner,pc,&value)==0);
                    }
                }
            }
            assert(gool_data_instruction(NULL,0,&value)==-1);
            assert(gool_data_state(&d,0,NULL)==-1);
        }
        cga_close(&archive);
    }
    printf("GOOL data checked: %u entries, %u states, %u libraries, %u unresolved handler references\n",
           entries,states,libraries,missing);
    return 0;
}
