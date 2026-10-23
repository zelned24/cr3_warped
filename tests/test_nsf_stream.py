"""Run the production NSF worker on real host threads and original NSF data.

The SDK adapter models locks, one-shot wakeups and cache publication. It does
not establish 3DS I/O latency, DMA completion or console scheduler behavior.
"""
from pathlib import Path
import subprocess
import struct
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cb3

SDK=r'''
#ifndef STREAM_TEST_SDK
#define STREAM_TEST_SDK
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
typedef int32_t Result;
#define R_FAILED(x) ((x)<0)
#define RESET_ONESHOT 0
typedef SRWLOCK LightLock;
typedef struct {HANDLE event;} LightEvent;
typedef struct TestThread {HANDLE handle;void (*entry)(void *);void *arg;DWORD id;} *Thread;
extern int test_allocations,test_cache_failure,test_thread_failure,test_alloc_failure;
extern DWORD test_worker_id;
static inline void LightLock_Init(LightLock *l){InitializeSRWLock(l);}
static inline void LightLock_Lock(LightLock *l){AcquireSRWLockExclusive(l);}
static inline void LightLock_Unlock(LightLock *l){ReleaseSRWLockExclusive(l);}
/* Windows event is closed at test-process exit; native LightEvent owns no OS handle. */
static inline void LightEvent_Init(LightEvent *e,int mode){(void)mode;e->event=CreateEventA(NULL,FALSE,FALSE,NULL);assert(e->event);}
static inline void LightEvent_Wait(LightEvent *e){assert(WaitForSingleObject(e->event,10000)==WAIT_OBJECT_0);}
static inline void LightEvent_Signal(LightEvent *e){assert(SetEvent(e->event));}
static DWORD WINAPI test_entry(LPVOID arg){Thread t=arg;test_worker_id=GetCurrentThreadId();t->entry(t->arg);return 0;}
static inline Thread threadCreate(void(*entry)(void*),void *arg,size_t stack,int priority,int core,bool detached){
 (void)stack;(void)priority;(void)core;assert(!detached);
 if(test_thread_failure)return NULL;
 Thread t=malloc(sizeof(*t));assert(t);t->entry=entry;t->arg=arg;
 t->handle=CreateThread(NULL,0,test_entry,t,0,&t->id);assert(t->handle);return t;
}
static inline Result threadJoin(Thread t,uint64_t timeout){(void)timeout;return WaitForSingleObject(t->handle,10000)==WAIT_OBJECT_0?0:-1;}
static inline void threadFree(Thread t){assert(WaitForSingleObject(t->handle,0)==WAIT_OBJECT_0);CloseHandle(t->handle);free(t);}
static inline void *linearAlloc(size_t n){if(test_alloc_failure)return NULL;void *p=malloc(n);if(p)test_allocations++;return p;}
static inline void linearFree(void *p){assert(p);test_allocations--;free(p);}
static inline Result GSPGPU_FlushDataCache(void *p,size_t n){assert(GetCurrentThreadId()==test_worker_id);assert(p&&n==65536);return test_cache_failure?-1:0;}
#endif
'''

HARNESS=r'''
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "nsf_stream.h"
int test_allocations,test_cache_failure,test_thread_failure,test_alloc_failure;
DWORD test_worker_id;
static nsf_stream_state await(nsf_stream_ticket t,int expected_error){
 for(unsigned i=0;i<10000;i++){
  nsf_stream_state state;int error;
  assert(nsf_stream_poll(t,&state,&error)==0);
  if(state!=NSF_STREAM_QUEUED&&state!=NSF_STREAM_READING){assert(error==expected_error);return state;}
  Sleep(1);
 }
 assert(!"worker timeout");return NSF_STREAM_FAILED;
}
int main(int argc,char **argv){
 assert(argc==2);const uint8_t *page=NULL;nsf_stream_ticket tickets[4],extra;
 test_alloc_failure=1;assert(nsf_stream_open(argv[1])==NSF_STREAM_FULL);test_alloc_failure=0;
 test_thread_failure=1;assert(nsf_stream_open(argv[1])==NSF_STREAM_FULL);test_thread_failure=0;
 assert(test_allocations==0);
 assert(nsf_stream_open(argv[1])==0&&test_allocations==1);
 assert(nsf_stream_open(argv[1])==NSF_STREAM_INVALID);
 assert(nsf_stream_request(UINT64_MAX,65536,&extra)==NSF_STREAM_INVALID);
 for(unsigned i=0;i<4;i++)assert(nsf_stream_request(i*65536,65536,&tickets[i])==0);
 assert(nsf_stream_request(0,65536,&extra)==NSF_STREAM_FULL);
 FILE *f=fopen(argv[1],"rb");assert(f);uint8_t expected[65536];
 for(unsigned i=0;i<4;i++){
  assert(await(tickets[i],0)==NSF_STREAM_READY);
  assert(nsf_stream_acquire(tickets[i],&page)==0);
  assert(fread(expected,1,sizeof(expected),f)==sizeof(expected));
  assert(memcmp(page,expected,sizeof(expected))==0);
  assert(nsf_stream_cancel(tickets[i])==0); /* cancellation preserves acquired immutable storage */
  assert(memcmp(page,expected,sizeof(expected))==0);
  assert(nsf_stream_release(tickets[i])==0);
  assert(nsf_stream_release(tickets[i])==0);
  assert(nsf_stream_release(tickets[i])==NSF_STREAM_INVALID);
 }
 assert(fclose(f)==0);
 /* The fifth span has a compressed header and a full raw tail. Its encoded
  * length differs from 64KiB: offsets and consumed spans must stay explicit. */
 assert(nsf_stream_request(4u*65536u,65548,&extra)==0);
 assert(await(extra,0)==NSF_STREAM_READY&&nsf_stream_acquire(extra,&page)==0);
 assert(memcmp(page,expected,sizeof(expected))==0);
 assert(nsf_stream_release(extra)==0&&nsf_stream_release(extra)==0);
 /* An owner may close with live borrows: join first, retain all referenced pages. */
 assert(nsf_stream_request(0,65536,&extra)==0&&await(extra,0)==NSF_STREAM_READY);
 assert(nsf_stream_acquire(extra,&page)==0);uint8_t first=page[0];
 assert(nsf_stream_close()==NSF_STREAM_BUSY&&test_allocations==1&&page[0]==first);
 assert(nsf_stream_request(0,65536,&tickets[0])==NSF_STREAM_INVALID);
 assert(nsf_stream_release(extra)==0&&nsf_stream_release(extra)==0);
 assert(nsf_stream_close()==0&&test_allocations==0);
 assert(nsf_stream_open(argv[1])==0);
 nsf_stream_state state;int error;
 assert(nsf_stream_poll(extra,&state,&error)==NSF_STREAM_INVALID);
 test_cache_failure=1;assert(nsf_stream_request(0,65536,&extra)==0);
 assert(await(extra,NSF_STREAM_CACHE)==NSF_STREAM_FAILED);
 assert(nsf_stream_acquire(extra,&page)==NSF_STREAM_BUSY);
 assert(nsf_stream_release(extra)==0);test_cache_failure=0;
 assert(nsf_stream_request(0,10,&extra)==0&&await(extra,NSF_STREAM_FORMAT)==NSF_STREAM_FAILED);
 assert(nsf_stream_release(extra)==0);
 assert(nsf_stream_request(0x70000000,65536,&extra)==0&&await(extra,NSF_STREAM_IO)==NSF_STREAM_FAILED);
 assert(nsf_stream_release(extra)==0);
 for(unsigned i=0;i<100;i++){
  assert(nsf_stream_request(0,65536,&extra)==0);
  assert(nsf_stream_cancel(extra)==0&&nsf_stream_release(extra)==0);
  /* Reading slots retire only after the worker returns; remaining capacity stays safe. */
 }
 assert(nsf_stream_close()==0&&test_allocations==0);
 assert(nsf_stream_close()==0);
 puts("PASS async NSF: real threaded page identity, four-slot bounds, cancellation, stale tickets, retained borrows, I/O/cache failures and shutdown");
 return 0;
}
'''


def main():
    path,_=cb3.level_paths(11)
    with tempfile.TemporaryDirectory(prefix='crash-nsf-stream-') as folder:
        folder=Path(folder)
        (folder/'3ds.h').write_text(SDK)
        (folder/'test.c').write_text(HARNESS)
        output=folder/'test.exe'
        subprocess.run([sys.executable,'-m','ziglang','cc','-std=c11','-O2','-UNDEBUG',
            '-Wall','-Wextra','-Werror','-I'+str(folder),'-I'+str(ROOT/'source'),
            str(folder/'test.c'),str(ROOT/'source/nsf_stream.c'),str(ROOT/'source/nsf_page.c'),
            '-o',str(output)],check=True)
        # Keep four complete original pages, then add a valid compressed span.
        # No original supplied archive contains compressed pages.
        first=Path(path).read_bytes()[:4*65536]
        assert len(first)==4*65536
        fixture=folder/'pages.nsf'
        fixture.write_bytes(first+struct.pack('<HHII',0x1235,0,0,0)+first[-65536:])
        for _ in range(3):
            subprocess.run([str(output),str(fixture)],check=True,timeout=40)


if __name__=='__main__':main()
