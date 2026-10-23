#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_stream.h"

static struct { void *p; size_t bytes; } allocations[24];
static ndspWaveBuf *queues[24][3];
static unsigned queue_count[24];
static int live, fail_allocation, allocation_count;
static float rates[24], mixes[24][12];
static bool paused[24];
static const void *flushed;
static uint32_t flushed_bytes;
static int16_t collected[600000];
static size_t collected_count;

void *linearAlloc(size_t n) {
    if(fail_allocation)return NULL;
    for(unsigned i=0;i<24;i++)if(!allocations[i].p){
        void *p=malloc(n);assert(p);allocations[i].p=p;allocations[i].bytes=n;
        allocation_count++;return p;
    }
    abort();
}
void linearFree(void *p) {
    assert(!live);
    for(unsigned i=0;i<24;i++)if(allocations[i].p==p){
        free(p);allocations[i].p=NULL;allocation_count--;return;
    }
    abort();
}
Result ndspInit(void){assert(!live);live=1;return 0;}
void ndspExit(void){assert(live);for(unsigned i=0;i<24;i++)assert(!queue_count[i]);live=0;}
void ndspSetOutputMode(int mode){assert(live && mode==NDSP_OUTPUT_STEREO);}
static void channel(int ch){assert(live && ch>=0 && ch<24);}
void ndspChnWaveBufClear(int ch){
    channel(ch);
    for(unsigned i=0;i<queue_count[ch];i++){
        queues[ch][i]->status=NDSP_WBUF_FREE;queues[ch][i]->next=NULL;
    }
    queue_count[ch]=0;
}
void ndspChnReset(int ch){channel(ch);assert(!queue_count[ch]);paused[ch]=false;}
void ndspChnSetFormat(int ch,int format){channel(ch);assert(format==NDSP_FORMAT_MONO_PCM16);}
void ndspChnSetRate(int ch,float rate){channel(ch);assert(rate>0 && rate<176400);rates[ch]=rate;}
void ndspChnSetPaused(int ch,bool pause){channel(ch);paused[ch]=pause;}
void ndspChnSetMix(int ch,float mix[12]){
    channel(ch);assert(mix[0]>=0 && mix[0]<=1 && mix[1]>=0 && mix[1]<=1);
    for(unsigned i=2;i<12;i++)assert(mix[i]==0);
    memcpy(mixes[ch],mix,sizeof(mixes[ch]));
}
void DSP_FlushDataCache(const void *p,uint32_t n){
    assert(p && n && n<=AUDIO_STREAM_SAMPLES_PER_BUFFER*2);
    for(unsigned i=0;i<24;i++)if(allocations[i].p){
        uintptr_t lo=(uintptr_t)allocations[i].p,at=(uintptr_t)p;
        if(at>=lo && at-lo<=allocations[i].bytes && n<=allocations[i].bytes-(at-lo)){
            flushed=p;flushed_bytes=n;return;
        }
    }
    abort();
}
void ndspChnWaveBufAdd(int ch,ndspWaveBuf *wave){
    channel(ch);assert(queue_count[ch]<3 && !wave->looping && !wave->next);
    assert(wave->status==NDSP_WBUF_FREE && wave->nsamples && wave->nsamples%28==0);
    assert(wave->data_vaddr==flushed && wave->nsamples*2==flushed_bytes);
    for(unsigned c=0;c<24;c++)for(unsigned i=0;i<queue_count[c];i++)assert(queues[c][i]!=wave);
    flushed=NULL;flushed_bytes=0;
    if(queue_count[ch])queues[ch][queue_count[ch]-1]->next=wave;
    queues[ch][queue_count[ch]++]=wave;wave->status=NDSP_WBUF_QUEUED;
}
static void finish(int ch,unsigned count){
    if(paused[ch])return;
    while(count-- && queue_count[ch]){
        ndspWaveBuf *wave=queues[ch][0];
        wave->status=NDSP_WBUF_PLAYING;
        assert(collected_count+wave->nsamples<=sizeof(collected)/sizeof(*collected));
        memcpy(collected+collected_count,wave->data_vaddr,wave->nsamples*2);
        collected_count+=wave->nsamples;
        for(unsigned i=1;i<queue_count[ch];i++)queues[ch][i-1]=queues[ch][i];
        queue_count[ch]--;wave->next=NULL;wave->status=NDSP_WBUF_DONE;
    }
}
static uint8_t *read_file(const char *path,size_t *bytes){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long size=ftell(f);assert(size>0);rewind(f);
    uint8_t *p=malloc((size_t)size);assert(p);
    assert(fread(p,1,(size_t)size,f)==(size_t)size);assert(!fclose(f));
    *bytes=(size_t)size;return p;
}
static uint32_t read32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}

int main(int argc,char **argv){
    assert(argc>=3 && argc%2==1);
    AudioStream stream,other;
    assert(ndspInit()==0);
    assert(audio_stream_poll_all()==0);
    audio_stream_stop_all();
    audio_stream_release_all_after_ndsp_exit(); /* no claims: no service/memory calls */
    for(int ch=0;ch<4;ch++)assert(audio_stream_init(&stream,ch)==AUDIO_STREAM_BUSY);
    assert(audio_stream_init(&stream,7)==AUDIO_STREAM_BUSY);
    assert(audio_stream_init(&stream,-1)==AUDIO_STREAM_INVALID);
    assert(audio_stream_init(&stream,24)==AUDIO_STREAM_INVALID);
    assert(audio_stream_init(NULL,4)==AUDIO_STREAM_INVALID);
    fail_allocation=1;
    assert(audio_stream_init(&stream,4)==AUDIO_STREAM_NO_MEMORY && !allocation_count);
    fail_allocation=0;
    assert(audio_stream_init(&stream,4)==0 && allocation_count==1);
    assert(allocations[0].bytes==AUDIO_STREAM_PCM_BYTES);
    assert(audio_stream_init(&stream,5)==AUDIO_STREAM_BUSY);
    assert(audio_stream_init(&other,4)==AUDIO_STREAM_BUSY);
    assert(audio_stream_init(&other,5)==0 && allocation_count==2);
    assert(audio_stream_drained(&stream));

    for(int fixture=1;fixture<argc;fixture+=2){
        size_t bytes,expected_bytes;
        uint8_t *raw=read_file(argv[fixture],&bytes);
        uint8_t *expected=read_file(argv[fixture+1],&expected_bytes);
        assert(bytes>8);
        uint32_t repeats=read32(raw),loop=read32(raw+4);
        AudioStreamSample sample={raw+8,bytes-8,loop==UINT32_MAX?AUDIO_STREAM_NO_LOOP:loop,repeats,{0,0}};
        collected_count=0;
        assert(audio_stream_start(&stream,&sample,0x1000,0x3fff,0x2000)==0);
        assert(queue_count[4]>0 && rates[4]==44100.0f && mixes[4][0]==1.0f);
        ndspWaveBuf *old=queues[4][0];size_t old_position=stream.position;
        AudioStreamSample bad=sample;bad.byte_size=15;
        assert(audio_stream_start(&stream,&bad,0x1000,1,1)==AUDIO_STREAM_INVALID);
        assert(queues[4][0]==old && stream.position==old_position);
        bad.byte_size=AUDIO_STREAM_MAX_SOURCE_BYTES+16;
        assert(audio_stream_start(&stream,&bad,0x1000,1,1)==AUDIO_STREAM_INVALID);
        bad.byte_size=16;
        bad.data=(const uint8_t *)stream.pcm;
        assert(audio_stream_start(&stream,&bad,0x1000,1,1)==AUDIO_STREAM_INVALID);
        bad.data=(const uint8_t *)&stream;
        assert(audio_stream_start(&stream,&bad,0x1000,1,1)==AUDIO_STREAM_INVALID);
        bad.data=(const uint8_t *)other.pcm;
        assert(audio_stream_start(&stream,&bad,0x1000,1,1)==AUDIO_STREAM_INVALID);
        bad.data=(const uint8_t *)&other.waves[1];
        assert(audio_stream_start(&stream,&bad,0x1000,1,1)==AUDIO_STREAM_INVALID);
        bad.data=(const uint8_t *)(uintptr_t)(UINTPTR_MAX-7);
        assert(audio_stream_start(&stream,&bad,0x1000,1,1)==AUDIO_STREAM_INVALID);
        assert(queues[4][0]==old && stream.position==old_position);
        assert(audio_stream_set_controls(&stream,0x4000,1,1)==AUDIO_STREAM_INVALID);
        assert(audio_stream_set_controls(&stream,1,0x4000,1)==AUDIO_STREAM_INVALID);
        assert(audio_stream_set_controls(&stream,0,1,1)==0 && paused[4]);
        finish(4,3);assert(!collected_count);
        assert(audio_stream_set_controls(&stream,0x1000,0x3fff,0x2000)==0 && !paused[4]);
        assert(audio_stream_start(&other,&sample,0x3fff,0,0)==0);
        assert(queue_count[5] && queue_count[4]);
        assert(audio_stream_poll_all()==(int)(queue_count[4]+queue_count[5]));
        audio_stream_stop(&other);assert(!queue_count[5] && queue_count[4]);
        unsigned polls=0;
        while(!audio_stream_drained(&stream)){
            /* Alternating single completions and complete underruns exercise
             * descriptor wrap and refill order; pending waveform data must match. */
            finish(4,polls%3==0?3:1);
            assert(audio_stream_poll_all()>=0);
            assert(++polls<2000);
            if(repeats==AUDIO_STREAM_REPEAT_FOREVER && collected_count>=expected_bytes/2)break;
        }
        if(repeats==AUDIO_STREAM_REPEAT_FOREVER){
            assert(!audio_stream_drained(&stream));
            assert(collected_count>=expected_bytes/2);
            assert(!memcmp(collected,expected,expected_bytes));
            audio_stream_stop(&stream);
        }else{
            assert(collected_count*2==expected_bytes);
            assert(!memcmp(collected,expected,expected_bytes));
            assert(audio_stream_poll(&stream)==0 && !stream.source);
        }
        assert(audio_stream_drained(&stream));
        assert(allocation_count==2);
        free(expected);free(raw);
    }
    uint8_t block[16]={12,3};
    AudioStreamSample malformed={block,16,AUDIO_STREAM_NO_LOOP,1,{0,0}};
    assert(audio_stream_start(&stream,&malformed,0x1000,1,1)==AUDIO_STREAM_INVALID);
    block[1]=0;
    assert(audio_stream_start(&stream,&malformed,0x1000,1,1)==AUDIO_STREAM_INVALID);
    block[1]=1;malformed.external_loop_byte=1;
    assert(audio_stream_start(&stream,&malformed,0x1000,1,1)==AUDIO_STREAM_INVALID);
    malformed.external_loop_byte=AUDIO_STREAM_NO_LOOP;
    assert(audio_stream_start(&stream,&malformed,0x1000,1,1)==0);
    assert(audio_stream_start(&other,&malformed,0x1000,1,1)==0);
    assert(queue_count[4] && queue_count[5]);
    assert(audio_stream_poll_all()==2 && allocation_count==2);
    audio_stream_stop_all();
    assert(!queue_count[4] && !queue_count[5] && allocation_count==2);
    assert(audio_stream_poll_all()==0);
    audio_stream_stop_all();ndspExit();
    audio_stream_release_all_after_ndsp_exit();
    assert(audio_stream_poll_all()==0);
    audio_stream_release_all_after_ndsp_exit();
    audio_stream_release_after_ndsp_exit(&stream);
    assert(!allocation_count);
    puts("PASS audio streaming: original PCM identity, repeat history, rings, underruns, pause, ownership, failures");
    return 0;
}
