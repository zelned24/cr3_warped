"""Exercise production SEP resource ownership with explicit backpressure/failures."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r"""
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "audio_sequence_resource.h"
static const uint8_t bytes[]={ 'p','Q','E','S',0,0, 0,1,0,96,7,161,32,4,2,0,0,0,8, 0,0x90,60,100,0,0xff,0x2f,0 };
static int refs,borrows,ready,accepted,pending=1,stop_pending=1,release_failure,format_failure;
int nsf_resource_request(uint32_t eid,nsf_resource_ticket *t){memset(t,0,sizeof(*t));t->eid=eid;refs++;return 0;}
int nsf_resource_poll(nsf_resource_ticket t,nsf_stream_state *s,int *error){(void)t;*s=ready?NSF_STREAM_READY:NSF_STREAM_READING;*error=0;return 0;}
int nsf_resource_acquire(nsf_resource_ticket t,nsf_resource_view *v){(void)t;memset(v,0,sizeof(*v));v->leased=1;v->entry_type=format_failure?12:13;borrows++;return 0;}
int nsf_resource_item(const nsf_resource_view *v,uint32_t i,const uint8_t **p,uint32_t *n){(void)v;assert(i==2);*p=bytes;*n=sizeof(bytes);return 0;}
int nsf_resource_release_view(nsf_resource_view *v){assert(v->leased&&borrows);if(release_failure){release_failure=0;return -99;}borrows--;v->leased=0;return 0;}
int nsf_resource_release_ticket(nsf_resource_ticket t){(void)t;assert(refs);refs--;return 0;}
int nsf_resource_cancel(nsf_resource_ticket t){(void)t;return 0;}
static int event(void *c,const AudioSequenceEvent *e){(void)c;assert(borrows==1);if(pending){pending=0;return 1;}accepted++;assert(e->kind==AUDIO_SEQUENCE_END||e->data[0]==60);return 0;}
static int stop(void *c){(void)c;assert(borrows==1);if(stop_pending){stop_pending=0;return 1;}return 0;}
int main(void){
 AudioSequenceResource o={0};AudioSequenceSink sink={NULL,event,stop};
 assert(!audio_sequence_resource_begin(&o,3,2,0,100,&sink));assert(refs==1&&!borrows);
 assert(audio_sequence_resource_begin(&o,5,2,0,100,&sink)==AUDIO_SEQUENCE_BUSY);
 assert(audio_sequence_resource_poll(&o,100,5)==AUDIO_SEQUENCE_RESOURCE_LOADING);
 ready=1;assert(audio_sequence_resource_poll(&o,100,5)==AUDIO_SEQUENCE_RESOURCE_PLAYING);
 assert(!refs&&borrows==1&&!accepted);
 assert(audio_sequence_resource_poll(&o,100,5)==AUDIO_SEQUENCE_RESOURCE_DRAINING);
 assert(accepted==2&&borrows==1);
 release_failure=1;assert(audio_sequence_resource_poll(&o,100,5)==-99);assert(borrows==1);
 assert(!audio_sequence_resource_stop(&o));assert(!refs&&!borrows);
 ready=0;assert(!audio_sequence_resource_begin(&o,3,2,0,0,&sink));assert(!audio_sequence_resource_stop(&o));assert(!refs&&!borrows);
 ready=1;format_failure=1;assert(!audio_sequence_resource_begin(&o,3,2,0,0,&sink));
 assert(audio_sequence_resource_poll(&o,0,5)==AUDIO_SEQUENCE_FORMAT);assert(refs==1&&borrows==1);
 assert(!audio_sequence_resource_stop(&o));assert(!refs&&!borrows);
 puts("PASS sequence resource: asynchronous load, event backpressure, voice drain, cancellation, invalid format and failed-release retry");return 0;
}

"""
def main():
    with tempfile.TemporaryDirectory(prefix="crash-sequence-resource-") as folder:
        path=Path(folder)
        (path/"probe.c").write_text(HARNESS)
        executable=path/"probe.exe"
        subprocess.run([sys.executable,"-m","ziglang","cc","-std=c11","-O2","-UNDEBUG",
            "-Wall","-Wextra","-Werror","-I",str(ROOT/"source"),str(path/"probe.c"),
            str(ROOT/"source/audio_sequence.c"),str(ROOT/"source/audio_sequence_resource.c"),
            "-o",str(executable)],check=True)
        subprocess.run([str(executable)],check=True)
if __name__=="__main__":main()
