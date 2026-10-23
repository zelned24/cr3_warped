#include "nsf_stream.h"
#include <3ds.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t generation,offset;
    uint32_t encoded_size,references;
    nsf_stream_state state;
    int occupied,cancel,error;
} stream_slot;
static stream_slot s_slots[NSF_STREAM_SLOTS];
static uint8_t s_encoded[NSF_STREAM_ENCODED_LIMIT];
static uint8_t *s_pages;
static char s_path[256];
static LightLock s_lock;
static LightEvent s_wake;
static Thread s_worker;
static int s_open,s_stopping;
static uint64_t s_generation;

static stream_slot *lookup(nsf_stream_ticket ticket) {
    if(ticket.slot>=NSF_STREAM_SLOTS)return NULL;
    stream_slot *slot=&s_slots[ticket.slot];
    return slot->occupied&&slot->generation==ticket.generation&&slot->references?slot:NULL;
}
static void worker(void *argument) {
    (void)argument;
    FILE *file=NULL;
    for(;;) {
        LightLock_Lock(&s_lock);
        if(s_stopping) {LightLock_Unlock(&s_lock);break;}
        unsigned selected=NSF_STREAM_SLOTS;
        for(unsigned i=0;i<NSF_STREAM_SLOTS;i++)
            if(s_slots[i].occupied&&s_slots[i].state==NSF_STREAM_QUEUED&&
               (selected==NSF_STREAM_SLOTS||s_slots[i].generation<s_slots[selected].generation))selected=i;
        if(selected==NSF_STREAM_SLOTS) {
            LightLock_Unlock(&s_lock);LightEvent_Wait(&s_wake);continue;
        }
        stream_slot *slot=&s_slots[selected];
        uint64_t offset=slot->offset;uint32_t size=slot->encoded_size;
        slot->state=NSF_STREAM_READING;
        LightLock_Unlock(&s_lock);
        int error=NSF_STREAM_OK;
        if(!file) {
            file=fopen(s_path,"rb");
            if(file&&setvbuf(file,NULL,_IONBF,0)!=0) {fclose(file);file=NULL;}
        }
        if(!file||fseek(file,(long)offset,SEEK_SET)!=0||fread(s_encoded,1,size,file)!=size||ferror(file)) {
            error=NSF_STREAM_IO;
            if(file) {fclose(file);file=NULL;}
        } else {
            size_t consumed=0;
            uint8_t *page=s_pages+selected*NSF_PAGE_BYTES;
            if(nsf_page_decode(s_encoded,size,page,NSF_PAGE_BYTES,&consumed)||consumed!=size)
                error=NSF_STREAM_FORMAT;
            else if(R_FAILED(GSPGPU_FlushDataCache(page,NSF_PAGE_BYTES)))error=NSF_STREAM_CACHE;
        }
        LightLock_Lock(&s_lock);
        slot->error=error;
        slot->state=slot->cancel||s_stopping?NSF_STREAM_CANCELLED:
                    error?NSF_STREAM_FAILED:NSF_STREAM_READY;
        if(!slot->references)slot->occupied=0;
        LightLock_Unlock(&s_lock);
    }
    if(file)fclose(file);
}

int nsf_stream_open(const char *path) {
    if(s_open||!path||!*path||strlen(path)>=sizeof(s_path))return NSF_STREAM_INVALID;
    uint8_t *pages=linearAlloc(NSF_STREAM_SLOTS*NSF_PAGE_BYTES);
    if(!pages)return NSF_STREAM_FULL;
    memset(s_slots,0,sizeof(s_slots));
    memcpy(s_path,path,strlen(path)+1);s_pages=pages;s_stopping=0;
    LightLock_Init(&s_lock);LightEvent_Init(&s_wake,RESET_ONESHOT);
    s_worker=threadCreate(worker,NULL,8192,0x31,-2,false);
    if(!s_worker) {linearFree(s_pages);s_pages=NULL;return NSF_STREAM_FULL;}
    s_open=1;
    return NSF_STREAM_OK;
}
int nsf_stream_request(uint64_t offset,uint32_t encoded_size,nsf_stream_ticket *ticket) {
    if(!s_open||!ticket||encoded_size<2||encoded_size>NSF_STREAM_ENCODED_LIMIT||
       offset>(uint64_t)LONG_MAX||encoded_size>(uint64_t)LONG_MAX-offset)return NSF_STREAM_INVALID;
    LightLock_Lock(&s_lock);
    int result=NSF_STREAM_FULL;
    if(s_stopping)result=NSF_STREAM_INVALID;
    else if(s_generation!=UINT64_MAX)for(unsigned i=0;i<NSF_STREAM_SLOTS;i++)if(!s_slots[i].occupied) {
        s_slots[i]=(stream_slot){.generation=++s_generation,.offset=offset,.encoded_size=encoded_size,
            .references=1,.state=NSF_STREAM_QUEUED,.occupied=1};
        *ticket=(nsf_stream_ticket){.generation=s_generation,.slot=i};result=NSF_STREAM_OK;break;
    }
    LightLock_Unlock(&s_lock);
    if(!result)LightEvent_Signal(&s_wake);
    return result;
}
int nsf_stream_poll(nsf_stream_ticket ticket,nsf_stream_state *state,int *error) {
    if(!s_open||!state||!error)return NSF_STREAM_INVALID;
    LightLock_Lock(&s_lock);stream_slot *slot=lookup(ticket);
    if(slot) {*state=slot->state;*error=slot->error;}
    LightLock_Unlock(&s_lock);
    return slot?NSF_STREAM_OK:NSF_STREAM_INVALID;
}
int nsf_stream_acquire(nsf_stream_ticket ticket,const uint8_t **page) {
    if(!s_open||!page)return NSF_STREAM_INVALID;
    LightLock_Lock(&s_lock);stream_slot *slot=lookup(ticket);
    int result=NSF_STREAM_INVALID;
    if(slot) {
        result=NSF_STREAM_BUSY;
        if(slot->state==NSF_STREAM_READY&&slot->references!=UINT32_MAX) {
            slot->references++;*page=s_pages+ticket.slot*NSF_PAGE_BYTES;result=NSF_STREAM_OK;
        }
    }
    LightLock_Unlock(&s_lock);return result;
}
int nsf_stream_release(nsf_stream_ticket ticket) {
    if(!s_open)return NSF_STREAM_INVALID;
    LightLock_Lock(&s_lock);stream_slot *slot=lookup(ticket);
    if(slot&&!--slot->references) {
        slot->cancel=1;
        if(slot->state!=NSF_STREAM_READING)slot->occupied=0;
    }
    LightLock_Unlock(&s_lock);return slot?NSF_STREAM_OK:NSF_STREAM_INVALID;
}
int nsf_stream_cancel(nsf_stream_ticket ticket) {
    if(!s_open)return NSF_STREAM_INVALID;
    LightLock_Lock(&s_lock);stream_slot *slot=lookup(ticket);
    if(slot) {
        slot->cancel=1;
        if(slot->state!=NSF_STREAM_READING)slot->state=NSF_STREAM_CANCELLED;
    }
    LightLock_Unlock(&s_lock);return slot?NSF_STREAM_OK:NSF_STREAM_INVALID;
}
int nsf_stream_close(void) {
    if(!s_open)return NSF_STREAM_OK;
    LightLock_Lock(&s_lock);s_stopping=1;
    for(unsigned i=0;i<NSF_STREAM_SLOTS;i++)if(s_slots[i].occupied) {
        s_slots[i].cancel=1;
        if(s_slots[i].state!=NSF_STREAM_READING)s_slots[i].state=NSF_STREAM_CANCELLED;
    }
    LightLock_Unlock(&s_lock);LightEvent_Signal(&s_wake);
    if(s_worker) {
        if(R_FAILED(threadJoin(s_worker,UINT64_MAX)))return NSF_STREAM_IO;
        threadFree(s_worker);s_worker=NULL;
    }
    LightLock_Lock(&s_lock);
    int busy=0;
    for(unsigned i=0;i<NSF_STREAM_SLOTS;i++)if(s_slots[i].references)busy=1;
    LightLock_Unlock(&s_lock);
    if(busy)return NSF_STREAM_BUSY;
    linearFree(s_pages);s_pages=NULL;s_open=0;
    return NSF_STREAM_OK;
}
