#include "audio_stream.h"
#include <string.h>

static AudioStream *owners[24];

static int claimed(const AudioStream *stream) {
    if(!stream)return 0;
    for(unsigned i=0;i<24;i++)if(owners[i]==stream)return 1;
    return 0;
}

static int controls_valid(uint16_t pitch,uint16_t left,uint16_t right) {
    return pitch<=0x3fff && left<=0x3fff && right<=0x3fff;
}

static int source_safe(const uint8_t *data,size_t bytes) {
    uintptr_t address=(uintptr_t)data;
    if(!data || !bytes || bytes>AUDIO_STREAM_MAX_SOURCE_BYTES || bytes>UINTPTR_MAX-address)
        return 0;
    uintptr_t end=address+bytes;
    for(unsigned i=0;i<24;i++)if(owners[i]) {
        uintptr_t state=(uintptr_t)owners[i],pcm=(uintptr_t)owners[i]->pcm;
        if(sizeof(*owners[i])>UINTPTR_MAX-state || AUDIO_STREAM_PCM_BYTES>UINTPTR_MAX-pcm)
            return 0;
        if((address<state+sizeof(*owners[i]) && state<end) ||
           (address<pcm+AUDIO_STREAM_PCM_BYTES && pcm<end))return 0;
    }
    return 1;
}

int audio_stream_init(AudioStream *stream,int channel) {
    if(!stream || channel<0 || channel>=24)return AUDIO_STREAM_INVALID;
    if(channel<4 || channel==7 || owners[channel] || claimed(stream))return AUDIO_STREAM_BUSY;
    int16_t *pcm=linearAlloc(AUDIO_STREAM_PCM_BYTES);
    if(!pcm)return AUDIO_STREAM_NO_MEMORY;
    memset(stream,0,sizeof(*stream));stream->pcm=pcm;stream->channel=channel;
    stream->source_done=1;owners[channel]=stream;
    return AUDIO_STREAM_OK;
}

static int validate_sample(const AudioStreamSample *sample,size_t *consumed) {
    if(!sample || !sample->data || !sample->byte_size ||
       sample->byte_size>AUDIO_STREAM_MAX_SOURCE_BYTES || sample->byte_size%16)
        return AUDIO_STREAM_INVALID;
    size_t end=0,loop=sample->external_loop_byte;
    unsigned end_flags=0;
    for(size_t position=0;position<sample->byte_size;position+=16) {
        uint8_t flags=sample->data[position+1];
        if(flags&4)loop=position;
        if(flags&1){end=position+16;end_flags=flags;break;}
    }
    if(!end)return AUDIO_STREAM_INVALID;
    if(sample->external_loop_byte!=AUDIO_STREAM_NO_LOOP &&
       (sample->external_loop_byte%16 || sample->external_loop_byte>=end))
        return AUDIO_STREAM_INVALID;
    if((end_flags&2) && sample->repeat_count && loop==AUDIO_STREAM_NO_LOOP)
        return AUDIO_STREAM_INVALID;
    *consumed=end;return AUDIO_STREAM_OK;
}

void audio_stream_stop(AudioStream *stream) {
    if(!claimed(stream))return;
    ndspChnWaveBufClear(stream->channel);
    ndspChnReset(stream->channel);
    memset(stream->waves,0,sizeof(stream->waves));
    memset(stream->submitted,0,sizeof(stream->submitted));
    stream->source=NULL;stream->source_bytes=0;
    stream->active=0;stream->source_done=1;stream->refill_index=0;
}

int audio_stream_set_controls(AudioStream *stream,uint16_t pitch,uint16_t left,uint16_t right) {
    if(!claimed(stream) || !controls_valid(pitch,left,right))return AUDIO_STREAM_INVALID;
    float mix[12]={left/16383.0f,right/16383.0f};
    ndspChnSetMix(stream->channel,mix);
    /* A zero rate is not sent to the resampler; pause preserves queued data. */
    ndspChnSetRate(stream->channel,pitch?(44100.0f/4096.0f)*pitch:44100.0f);
    ndspChnSetPaused(stream->channel,pitch==0);
    return AUDIO_STREAM_OK;
}

static unsigned decode_buffer(AudioStream *stream,int16_t *output) {
    unsigned samples=0;
    while(samples<AUDIO_STREAM_SAMPLES_PER_BUFFER && !stream->source_done) {
        uint8_t flags;
        /* Start validated the immutable complete span. */
        if(stream->position>=stream->source_bytes ||
           !audio_decode_adpcm_block(stream->source+stream->position,16,output+samples,28,
                                     &stream->history,&flags)) {
            stream->source_done=1;break;
        }
        samples+=28;
        if(flags&4)stream->loop_byte=stream->position;
        stream->position+=16;
        if(flags&1) {
            if((flags&2) && stream->repeats_left) {
                stream->position=stream->loop_byte;
                if(stream->repeats_left!=AUDIO_STREAM_REPEAT_FOREVER)stream->repeats_left--;
            } else stream->source_done=1;
        }
    }
    return samples;
}

int audio_stream_poll(AudioStream *stream) {
    if(!claimed(stream))return AUDIO_STREAM_INVALID;
    if(!stream->active)return 0;
    /* Consume slots in submission order. Scanning physical slot0 first after
     * a wrap/underrun would reorder decoded chunks relative to queued audio. */
    for(unsigned visited=0;visited<AUDIO_STREAM_BUFFERS;visited++) {
        unsigned index=stream->refill_index;
        ndspWaveBuf *wave=&stream->waves[index];
        unsigned status=*(volatile uint8_t *)&wave->status;
        if(stream->submitted[index] && status!=NDSP_WBUF_DONE)break;
        __sync_synchronize();
        stream->submitted[index]=0;
        if(stream->source_done) {
            stream->refill_index=(index+1)%AUDIO_STREAM_BUFFERS;
            continue;
        }
        int16_t *pcm=stream->pcm+index*AUDIO_STREAM_SAMPLES_PER_BUFFER;
        unsigned count=decode_buffer(stream,pcm);
        if(!count)break;
        DSP_FlushDataCache(pcm,count*sizeof(*pcm));
        memset(wave,0,sizeof(*wave));wave->data_vaddr=pcm;wave->nsamples=count;
        wave->status=NDSP_WBUF_FREE;wave->looping=false;
        stream->submitted[index]=1;
        ndspChnWaveBufAdd(stream->channel,wave);
        stream->refill_index=(index+1)%AUDIO_STREAM_BUFFERS;
    }
    int pending=0;
    for(unsigned i=0;i<AUDIO_STREAM_BUFFERS;i++)if(stream->submitted[i]) {
        if(*(volatile uint8_t *)&stream->waves[i].status==NDSP_WBUF_DONE)
            stream->submitted[i]=0;
        else pending++;
    }
    if(stream->source_done && !pending){stream->active=0;stream->source=NULL;}
    return pending;
}

int audio_stream_start(AudioStream *stream,const AudioStreamSample *sample,
                        uint16_t pitch,uint16_t left,uint16_t right) {
    size_t consumed;
    if(!claimed(stream) || !sample || !controls_valid(pitch,left,right))
        return AUDIO_STREAM_INVALID;
    AudioStreamSample next=*sample;
    /* Stop clears descriptors and decode overwrites PCM rings. Reject aliases
     * before scanning source bytes or mutating any active stream. */
    if(!source_safe(next.data,next.byte_size) || validate_sample(&next,&consumed))
        return AUDIO_STREAM_INVALID;
    audio_stream_stop(stream);
    stream->source=next.data;stream->source_bytes=consumed;
    stream->position=0;stream->loop_byte=next.external_loop_byte;
    stream->history=next.history;stream->repeats_left=next.repeat_count;
    stream->source_done=0;stream->active=1;
    ndspChnSetFormat(stream->channel,NDSP_FORMAT_MONO_PCM16);
    audio_stream_set_controls(stream,pitch,left,right);
    return audio_stream_poll(stream)<0?AUDIO_STREAM_INVALID:AUDIO_STREAM_OK;
}

int audio_stream_drained(const AudioStream *stream) {
    if(!claimed(stream))return 1;
    if(!stream->source_done)return 0;
    for(unsigned i=0;i<AUDIO_STREAM_BUFFERS;i++)
        if(stream->submitted[i] && *(volatile const uint8_t *)&stream->waves[i].status!=NDSP_WBUF_DONE)
            return 0;
    return 1;
}

void audio_stream_release_after_ndsp_exit(AudioStream *stream) {
    if(!claimed(stream))return;
    for(unsigned i=0;i<24;i++)if(owners[i]==stream)owners[i]=NULL;
    linearFree(stream->pcm);memset(stream,0,sizeof(*stream));
}

int audio_stream_poll_all(void) {
    int pending=0;
    for(unsigned channel=0;channel<24;channel++)if(owners[channel]) {
        int count=audio_stream_poll(owners[channel]);
        if(count<0)return count;
        pending+=count;
    }
    return pending;
}

void audio_stream_stop_all(void) {
    for(unsigned channel=0;channel<24;channel++)
        if(owners[channel])audio_stream_stop(owners[channel]);
}

void audio_stream_release_all_after_ndsp_exit(void) {
    for(unsigned channel=0;channel<24;channel++)
        if(owners[channel])audio_stream_release_after_ndsp_exit(owners[channel]);
}
