#include "audio_resource.h"
#include <string.h>

static int release_refs(AudioResource *slot) {
    int first=0,error;
    if(slot->view.leased) {
        error=nsf_resource_release_view(&slot->view);
        if(error)first=error;
    }
    if(slot->has_ticket) {
        error=nsf_resource_release_ticket(slot->ticket);
        if(error) {if(!first)first=error;}
        else {slot->has_ticket=0;memset(&slot->ticket,0,sizeof(slot->ticket));}
    }
    return first;
}

static int failed(AudioResource *slot,int error) {
    audio_stream_stop(&slot->stream);
    int cleanup=release_refs(slot);
    slot->state=AUDIO_RESOURCE_FAILED;
    slot->error=cleanup?cleanup:error;
    return slot->error;
}

int audio_resource_init(AudioResource *slot,int channel) {
    if(!slot)return AUDIO_RESOURCE_INVALID;
    if(slot->initialized)return AUDIO_RESOURCE_BUSY;
    memset(slot,0,sizeof(*slot));
    int error=audio_stream_init(&slot->stream,channel);
    if(error)return error;
    slot->initialized=1;slot->state=AUDIO_RESOURCE_IDLE;
    return 0;
}

int audio_resource_begin(AudioResource *slot,uint32_t eid,const AudioResourceControls *controls) {
    if(!slot||!slot->initialized||!controls||!(eid&1u)||controls->pitch>0x3fff||
       controls->left_volume>0x3fff||controls->right_volume>0x3fff)
        return AUDIO_RESOURCE_INVALID;
    if(slot->state==AUDIO_RESOURCE_LOADING||slot->state==AUDIO_RESOURCE_PLAYING||
       slot->has_ticket||slot->view.leased)return AUDIO_RESOURCE_BUSY;
    nsf_resource_ticket ticket;
    int error=nsf_resource_request(eid,&ticket);
    if(error)return error;
    slot->ticket=ticket;slot->has_ticket=1;slot->controls=*controls;
    slot->eid=eid;slot->error=0;slot->state=AUDIO_RESOURCE_LOADING;
    return 0;
}

int audio_resource_poll(AudioResource *slot) {
    if(!slot||!slot->initialized)return AUDIO_RESOURCE_INVALID;
    if(slot->state==AUDIO_RESOURCE_FAILED)return slot->error;
    if(slot->state==AUDIO_RESOURCE_IDLE)return AUDIO_RESOURCE_IDLE;
    if(slot->state==AUDIO_RESOURCE_LOADING) {
        nsf_stream_state state;int worker_error;
        int error=nsf_resource_poll(slot->ticket,&state,&worker_error);
        if(error)return failed(slot,error);
        if(state==NSF_STREAM_QUEUED||state==NSF_STREAM_READING)return AUDIO_RESOURCE_LOADING;
        if(state!=NSF_STREAM_READY)
            return failed(slot,worker_error?worker_error:
                          state==NSF_STREAM_CANCELLED?AUDIO_RESOURCE_CANCELLED:AUDIO_RESOURCE_FORMAT);
        error=nsf_resource_acquire(slot->ticket,&slot->view);
        if(error)return failed(slot,error);
        if(slot->view.page_type!=3||slot->view.entry_type!=12||slot->view.item_count!=1)
            return failed(slot,AUDIO_RESOURCE_FORMAT);
        const uint8_t *data;uint32_t bytes;
        error=nsf_resource_item(&slot->view,0,&data,&bytes);
        if(error)return failed(slot,error);
        AudioStreamSample sample={.data=data,.byte_size=bytes,
            .external_loop_byte=slot->controls.external_loop_byte,
            .repeat_count=slot->controls.repeat_count,.history=slot->controls.history};
        error=audio_stream_start(&slot->stream,&sample,slot->controls.pitch,
                                slot->controls.left_volume,slot->controls.right_volume);
        if(error)return failed(slot,error);
        error=nsf_resource_release_ticket(slot->ticket);
        if(error)return failed(slot,error);
        slot->has_ticket=0;memset(&slot->ticket,0,sizeof(slot->ticket));
        slot->state=AUDIO_RESOURCE_PLAYING;
    }
    int error=audio_stream_poll(&slot->stream);
    if(error<0)return failed(slot,error);
    if(audio_stream_drained(&slot->stream)) {
        /* Clear source/descriptors before releasing the borrowed original page. */
        audio_stream_stop(&slot->stream);
        error=release_refs(slot);
        if(error)return failed(slot,error);
        slot->state=AUDIO_RESOURCE_IDLE;
    }
    return slot->state;
}

int audio_resource_stop(AudioResource *slot) {
    if(!slot||!slot->initialized)return AUDIO_RESOURCE_INVALID;
    audio_stream_stop(&slot->stream);
    int cancel=slot->has_ticket?nsf_resource_cancel(slot->ticket):0;
    int error=release_refs(slot);
    if(error) {
        slot->state=AUDIO_RESOURCE_FAILED;slot->error=error;return error;
    }
    /* A stale/cancelled request can still have been released successfully. */
    slot->state=AUDIO_RESOURCE_IDLE;slot->error=0;slot->eid=0;
    return cancel;
}

int audio_resource_release_after_ndsp_exit(AudioResource *slot) {
    if(!slot||!slot->initialized)return AUDIO_RESOURCE_INVALID;
    if(slot->has_ticket||slot->view.leased||slot->stream.active||
       slot->state==AUDIO_RESOURCE_LOADING||slot->state==AUDIO_RESOURCE_PLAYING)
        return AUDIO_RESOURCE_BUSY;
    audio_stream_release_after_ndsp_exit(&slot->stream);
    memset(slot,0,sizeof(*slot));return 0;
}
