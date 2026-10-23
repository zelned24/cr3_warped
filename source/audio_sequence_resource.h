#ifndef AUDIO_SEQUENCE_RESOURCE_H
#define AUDIO_SEQUENCE_RESOURCE_H
#include "audio_sequence.h"

enum { AUDIO_SEQUENCE_RESOURCE_IDLE=0, AUDIO_SEQUENCE_RESOURCE_LOADING=1,
       AUDIO_SEQUENCE_RESOURCE_PLAYING=2, AUDIO_SEQUENCE_RESOURCE_DRAINING=3,
       AUDIO_SEQUENCE_RESOURCE_FAILED=4 };

typedef struct {
    AudioSequence sequence;
    AudioSequenceSink sink;
    nsf_resource_ticket ticket;
    uint64_t start_us;
    uint32_t eid,item,record;
    int state,has_ticket,error;
} AudioSequenceResource;

/* Stable zero-initialized storage, explicit original type13 EID/item/record
 * and actual audio-clock origin. Requests allocate no application storage.
 * Sink supplies the real INST/SPU implementation; none is substituted here.
 * NSF session and sink stay alive until stop completes. Busy leaves unchanged. */
int audio_sequence_resource_begin(AudioSequenceResource *owner,uint32_t eid,
    uint32_t item,uint32_t record,uint64_t start_us,const AudioSequenceSink *sink);
/* Polls NSF once and advances at most event_budget events. END enters draining:
 * sink.stop must quiesce borrowed events/voices before the page is released.
 * Sink backpressure and close errors retain every outstanding reference. */
int audio_sequence_resource_poll(AudioSequenceResource *owner,uint64_t now_us,
    uint32_t event_budget);
/* Cancel request and stop voices before releasing page ownership. BUSY means
 * retry, with all borrowed source/event storage still alive. Also repairs a
 * failed owner; a successfully stopped owner can be requested again. */
int audio_sequence_resource_stop(AudioSequenceResource *owner);
#endif
