#include "audio_sequence.h"
#include <string.h>

static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be24(const uint8_t *p){return (uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2];}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int vlq(const AudioSequenceRecord *r,uint32_t *cursor,uint32_t *value) {
    uint32_t p=*cursor,v=0;
    for(unsigned i=0;i<4;i++) {
        if(p>=r->bytes)return AUDIO_SEQUENCE_FORMAT;
        uint8_t b=r->events[p++];v=(v<<7)|(b&0x7fu);
        if(!(b&0x80u)){*cursor=p;*value=v;return 0;}
    }
    return AUDIO_SEQUENCE_FORMAT;
}
static int decode(const AudioSequenceRecord *r,uint32_t at,uint8_t running,
                   AudioSequenceEvent *event,uint32_t *after,uint8_t *status_after) {
    if(at>=r->bytes)return AUDIO_SEQUENCE_FORMAT;
    uint32_t p=at;uint8_t status=r->events[p];
    if(status&0x80u)p++;
    else status=running;
    if(status<0x80u)return AUDIO_SEQUENCE_FORMAT;
    AudioSequenceEvent next={.kind=AUDIO_SEQUENCE_MIDI,.status=status};
    if(status==0xffu) {
        if(p>=r->bytes)return AUDIO_SEQUENCE_FORMAT;
        uint8_t type=r->events[p++];
        if(type==0x2fu) {
            if(p>=r->bytes||r->events[p++]||p!=r->bytes)return AUDIO_SEQUENCE_FORMAT;
            next.kind=AUDIO_SEQUENCE_END;
        } else if(type==0x51u) {
            /* Original56498..564BC consumes the type;546CC..54748 then reads
             * exactly three tempo bytes. There is no SMF length03 prefix. */
            if(r->bytes-p<3)return AUDIO_SEQUENCE_FORMAT;
            next.payload=r->events+p;next.payload_size=3;
            next.tempo=be24(r->events+p);if(!next.tempo)return AUDIO_SEQUENCE_FORMAT;
            next.kind=AUDIO_SEQUENCE_TEMPO;p+=3;
        } else return AUDIO_SEQUENCE_UNSUPPORTED;
        running=status;
    } else {
        if(status>=0xf0u)return AUDIO_SEQUENCE_UNSUPPORTED;
        unsigned family=status&0xf0u;
        if(family!=0x90u&&family!=0xb0u&&family!=0xc0u&&family!=0xe0u)
            return AUDIO_SEQUENCE_UNSUPPORTED;
        next.data_count=family==0xc0u?1:2;
        if(next.data_count>r->bytes-p)return AUDIO_SEQUENCE_FORMAT;
        for(unsigned i=0;i<next.data_count;i++) {
            uint8_t value=r->events[p++];if(value&0x80u)return AUDIO_SEQUENCE_FORMAT;
            next.data[i]=value;
        }
        running=status;
    }
    *event=next;*after=p;*status_after=running;return 0;
}
static int validate_record(const AudioSequenceRecord *r) {
    uint32_t cursor=0;uint8_t running=0;
    while(cursor<r->bytes) {
        uint32_t delta,after;uint8_t status;AudioSequenceEvent event;
        int error=vlq(r,&cursor,&delta);if(error)return error;
        error=decode(r,cursor,running,&event,&after,&status);if(error)return error;
        cursor=after;running=status;
        if(event.kind==AUDIO_SEQUENCE_END)return 0;
    }
    return AUDIO_SEQUENCE_FORMAT;
}
int audio_sequence_catalog_bind(AudioSequenceCatalog *catalog,const void *bytes,uint32_t size) {
    if(!catalog||!bytes||size<19||size>65536u)return AUDIO_SEQUENCE_INVALID;
    const uint8_t *b=bytes;
    if(memcmp(b,"pQES",4)||be16(b+4)!=0)return AUDIO_SEQUENCE_UNSUPPORTED;
    AudioSequenceCatalog next={.bytes=b,.size=size};uint32_t p=6;
    while(size-p>3) {
        if(size-p<13)return AUDIO_SEQUENCE_FORMAT;
        if(next.count==AUDIO_SEQUENCE_RECORDS)return AUDIO_SEQUENCE_UNSUPPORTED;
        AudioSequenceRecord record={.id=be16(b+p),.division=be16(b+p+2),
            .tempo=be24(b+p+4),.numerator=b[p+7],.denominator=b[p+8],.bytes=be32(b+p+9)};
        if(!record.division||(record.division&0x8000u)||!record.tempo||
           !record.numerator||record.denominator>7||!record.bytes||record.bytes>size-p-13)
            return AUDIO_SEQUENCE_FORMAT;
        for(uint32_t i=0;i<next.count;i++)if(next.records[i].id==record.id)return AUDIO_SEQUENCE_FORMAT;
        record.events=b+p+13;
        int error=validate_record(&record);if(error)return error;
        next.records[next.count++]=record;p+=13+record.bytes;
    }
    if(!next.count)return AUDIO_SEQUENCE_FORMAT;
    for(;p<size;p++)if(b[p])return AUDIO_SEQUENCE_FORMAT;
    *catalog=next;return 0;
}
static int schedule(AudioSequence *s,uint32_t delta) {
    uint64_t numerator=(uint64_t)delta*s->tempo+s->fraction;
    uint64_t duration=numerator/s->record.division;
    if(duration>UINT64_MAX-s->due_us||delta>UINT64_MAX-s->due_tick)return AUDIO_SEQUENCE_TIME;
    s->due_us+=duration;s->due_tick+=delta;s->fraction=(uint32_t)(numerator%s->record.division);
    return 0;
}
static int prime(AudioSequence *s) {
    uint32_t cursor=s->cursor,delta;int error=vlq(&s->record,&cursor,&delta);if(error)return error;
    error=schedule(s,delta);if(error)return error;
    s->cursor=cursor;return 0;
}
static int commit(AudioSequence *s) {
    const AudioSequenceEvent *e=&s->event;
    s->cursor=s->next_cursor;s->running_status=s->next_status;s->pending_event=0;
    if(e->kind==AUDIO_SEQUENCE_END){s->ended=1;return 0;}
    if(e->kind==AUDIO_SEQUENCE_TEMPO)s->tempo=e->tempo;
    if(e->kind==AUDIO_SEQUENCE_MIDI&&(e->status&0xf0u)==0xb0u) {
        uint8_t control=e->data[0],value=e->data[1];
        /* Original CC99 handler54114 saves the cursor AFTER consuming the
         * next delta at54188..5419C. There is one native loop slot per record. */
        if(control==99&&value==20) {
            s->loop_armed=1;int error=prime(s);if(error)return error;
            s->loop_cursor=s->cursor;s->loop_tick=s->due_tick;s->has_loop=1;return 0;
        }
        /* CC6 handler5389C..538D0 initializes count only for an armed, inactive
         * loop. A jump back through the count event never resets the count. */
        if(control==6&&s->loop_armed&&!s->loop_active) {
            s->loop_count=value;s->loop_armed=0;s->loop_active=1;
        } else if(control==99&&value==30) {
            if(!s->loop_count){s->loop_active=0;return prime(s);}
            if(!s->has_loop||s->loop_cursor>=s->record.bytes)return AUDIO_SEQUENCE_FORMAT;
            if(s->loop_count<127) {
                s->loop_count--;
                int error=prime(s);if(error)return error;
                if(s->loop_count)s->cursor=s->loop_cursor;
                else s->loop_active=0;
                return 0;
            }
            /* Native541F4..54208 consumes/discards the post-loop delta and
             * restarts saved event bytes with wait0, keeping running status. */
            uint32_t ignored,cursor=s->cursor;
            int error=vlq(&s->record,&cursor,&ignored);if(error)return error;
            if(s->due_tick==s->loop_tick)return AUDIO_SEQUENCE_CYCLE;
            s->loop_tick=s->due_tick;s->cursor=s->loop_cursor;return 0;
        }
    }
    return prime(s);
}
int audio_sequence_acquire(AudioSequence *s,nsf_resource_ticket ticket,
                            uint32_t item,uint32_t record,uint64_t start_us,
                            const AudioSequenceSink *sink) {
    if(!s||!sink||!sink->event||!sink->stop)return AUDIO_SEQUENCE_INVALID;
    if(s->opened||s->view.leased)return AUDIO_SEQUENCE_BUSY;
    AudioSequenceSink copy=*sink;memset(s,0,sizeof(*s));s->sink=copy;
    int error=nsf_resource_acquire(ticket,&s->view);if(error)return error;
    s->opened=1;s->clock_us=s->due_us=start_us;
    if(s->view.page_type!=0||s->view.entry_type!=13){s->error=AUDIO_SEQUENCE_FORMAT;return s->error;}
    const uint8_t *bytes;uint32_t size;
    error=nsf_resource_item(&s->view,item,&bytes,&size);
    if(!error)error=audio_sequence_catalog_bind(&s->catalog,bytes,size);
    if(!error&&record>=s->catalog.count)error=AUDIO_SEQUENCE_INVALID;
    if(!error){s->record=s->catalog.records[record];s->tempo=s->record.tempo;error=prime(s);}
    if(error)s->error=error;
    return error;
}
int audio_sequence_advance(AudioSequence *s,uint64_t now_us,uint32_t budget) {
    if(!s||!s->opened||s->stopped)return AUDIO_SEQUENCE_INVALID;
    if(s->error)return s->error;
    if(now_us<s->clock_us)return AUDIO_SEQUENCE_TIME;
    s->clock_us=now_us;
    if(s->ended)return AUDIO_SEQUENCE_DONE;
    while(budget--) {
        if(!s->pending_event) {
            if(s->due_us>now_us)return AUDIO_SEQUENCE_WAITING;
            int error=decode(&s->record,s->cursor,s->running_status,&s->event,&s->next_cursor,&s->next_status);
            if(error){s->error=error;return error;}
            s->event.tick=s->due_tick;s->event.time_us=s->due_us;
            if(s->event.kind==AUDIO_SEQUENCE_MIDI&&(s->event.status&0xf0u)==0xb0u) {
                uint8_t c=s->event.data[0],v=s->event.data[1];
                s->event.internal=(c==99&&(v==20||v==30))||(c==6&&s->loop_armed&&!s->loop_active);
            }
            s->pending_event=1;
        }
        int error=s->sink.event(s->sink.context,&s->event);
        if(error<0){s->error=error;return error;}
        if(error==AUDIO_SEQUENCE_PENDING)return error;
        if(error){s->error=AUDIO_SEQUENCE_INVALID;return s->error;}
        error=commit(s);if(error){s->error=error;return error;}
        if(s->ended)return AUDIO_SEQUENCE_DONE;
    }
    return AUDIO_SEQUENCE_PENDING;
}
int audio_sequence_close(AudioSequence *s) {
    if(!s)return AUDIO_SEQUENCE_INVALID;
    if(!s->opened&&!s->view.leased)return 0;
    if(!s->stopped) {
        int error=s->sink.stop(s->sink.context);
        if(error<0)return error;
        if(error==AUDIO_SEQUENCE_PENDING)return AUDIO_SEQUENCE_BUSY;
        if(error)return AUDIO_SEQUENCE_INVALID;
        s->stopped=1;s->pending_event=0;
    }
    if(s->view.leased){int error=nsf_resource_release_view(&s->view);if(error)return error;}
    memset(s,0,sizeof(*s));return 0;
}
