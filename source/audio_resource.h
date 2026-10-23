#ifndef AUDIO_RESOURCE_H
#define AUDIO_RESOURCE_H
#include "audio_stream.h"
#include "nsf_resource.h"

typedef enum {
    AUDIO_RESOURCE_IDLE, AUDIO_RESOURCE_LOADING, AUDIO_RESOURCE_PLAYING,
    AUDIO_RESOURCE_FAILED
} AudioResourceState;
enum { AUDIO_RESOURCE_INVALID=-40, AUDIO_RESOURCE_BUSY=-41,
       AUDIO_RESOURCE_FORMAT=-42, AUDIO_RESOURCE_CANCELLED=-43 };
typedef struct {
    size_t external_loop_byte;
    uint32_t repeat_count;
    AudioAdpcmHistory history;
    uint16_t pitch,left_volume,right_volume;
} AudioResourceControls;
typedef struct {
    AudioStream stream;
    nsf_resource_ticket ticket;
    nsf_resource_view view;
    AudioResourceControls controls;
    AudioResourceState state;
    uint32_t eid;
    int initialized,has_ticket,error;
} AudioResource;

/* Initialize stable, zeroed caller storage after NDSP initialization. One
 * existing AudioStream pool is reserved; requests/polls allocate nothing.
 * The owner application thread services all functions. Never copy the slot.
 * This plays an explicit original ADIO EID/item0, not a GOOL/event mapping. */
int audio_resource_init(AudioResource *slot,int channel);
/* An NSF resource session must already be open. Caller supplies every playback
 * parameter, including loop policy and initial decoder history. Busy refusal
 * preserves the currently loading/playing sample and all its references. */
int audio_resource_begin(AudioResource *slot,uint32_t eid,const AudioResourceControls *controls);
/* Poll every frame even when audio_stream_poll_all is also used. Returns the
 * current nonnegative state, or a negative error retained in slot.error.
 * Natural drain releases the page borrow here; the global stream poll alone
 * cannot retire this owner. Item bytes remain leased through the full drain. */
int audio_resource_poll(AudioResource *slot);
/* Stop while NDSP and the resource session remain alive. Cancellation releases
 * the request reference; playback stop precedes page release. Retry a failed
 * stop before closing the resource session. Successful stop returns to IDLE. */
int audio_resource_stop(AudioResource *slot);
/* Call only after successful stop, then NDSP shutdown. No NDSP calls occur.
 * Refuses outstanding references/playback; releases the stream's linear pool. */
int audio_resource_release_after_ndsp_exit(AudioResource *slot);
#endif
