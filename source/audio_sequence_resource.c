#include "audio_sequence_resource.h"
#include <string.h>

static int fail(AudioSequenceResource *owner,int error) {
    owner->state=AUDIO_SEQUENCE_RESOURCE_FAILED;
    owner->error=error;
    return error;
}

int audio_sequence_resource_begin(AudioSequenceResource *owner,uint32_t eid,
    uint32_t item,uint32_t record,uint64_t start_us,const AudioSequenceSink *sink) {
    if(!owner||!sink||!sink->event||!sink->stop||!(eid&1u))return AUDIO_SEQUENCE_INVALID;
    if(owner->state!=AUDIO_SEQUENCE_RESOURCE_IDLE||owner->has_ticket||
       owner->sequence.opened||owner->sequence.view.leased)return AUDIO_SEQUENCE_BUSY;
    AudioSequenceSink captured=*sink;
    nsf_resource_ticket ticket;
    int error=nsf_resource_request(eid,&ticket);
    if(error)return error;
    owner->ticket=ticket;owner->has_ticket=1;owner->sink=captured;
    owner->eid=eid;owner->item=item;owner->record=record;owner->start_us=start_us;
    owner->error=0;owner->state=AUDIO_SEQUENCE_RESOURCE_LOADING;
    return 0;
}

int audio_sequence_resource_stop(AudioSequenceResource *owner) {
    if(!owner)return AUDIO_SEQUENCE_INVALID;
    int error=audio_sequence_close(&owner->sequence);
    if(error)return error;
    if(owner->has_ticket) {
        /* Cancellation is advisory. Release still owns the request even if a
         * worker already completed or failed it while the sink drained. */
        (void)nsf_resource_cancel(owner->ticket);
        error=nsf_resource_release_ticket(owner->ticket);
        if(error)return fail(owner,error);
    }
    memset(owner,0,sizeof(*owner));
    return 0;
}

int audio_sequence_resource_poll(AudioSequenceResource *owner,uint64_t now_us,
    uint32_t budget) {
    if(!owner)return AUDIO_SEQUENCE_INVALID;
    if(owner->state==AUDIO_SEQUENCE_RESOURCE_FAILED)return owner->error;
    if(owner->state==AUDIO_SEQUENCE_RESOURCE_IDLE)return owner->state;
    if(owner->state==AUDIO_SEQUENCE_RESOURCE_LOADING) {
        if(now_us<owner->start_us)return AUDIO_SEQUENCE_TIME;
        nsf_stream_state state;int worker_error;
        int error=nsf_resource_poll(owner->ticket,&state,&worker_error);
        if(error)return fail(owner,error);
        if(state==NSF_STREAM_QUEUED||state==NSF_STREAM_READING)return owner->state;
        if(state!=NSF_STREAM_READY)return fail(owner,worker_error?worker_error:AUDIO_SEQUENCE_FORMAT);
        error=audio_sequence_acquire(&owner->sequence,owner->ticket,owner->item,
            owner->record,owner->start_us,&owner->sink);
        if(error)return fail(owner,error);
        error=nsf_resource_release_ticket(owner->ticket);
        if(error)return fail(owner,error);
        owner->has_ticket=0;memset(&owner->ticket,0,sizeof(owner->ticket));
        owner->state=AUDIO_SEQUENCE_RESOURCE_PLAYING;
    }
    if(owner->state==AUDIO_SEQUENCE_RESOURCE_PLAYING) {
        int error=audio_sequence_advance(&owner->sequence,now_us,budget);
        if(error<0)return fail(owner,error);
        if(error!=AUDIO_SEQUENCE_DONE)return owner->state;
        owner->state=AUDIO_SEQUENCE_RESOURCE_DRAINING;
    }
    int error=audio_sequence_resource_stop(owner);
    if(error==AUDIO_SEQUENCE_BUSY)return AUDIO_SEQUENCE_RESOURCE_DRAINING;
    if(error)return fail(owner,error);
    return AUDIO_SEQUENCE_RESOURCE_IDLE;
}
