#ifndef AUDIO_SEQUENCE_H
#define AUDIO_SEQUENCE_H
#include "nsf_resource.h"

#define AUDIO_SEQUENCE_RECORDS 16u
enum {AUDIO_SEQUENCE_DONE=0,AUDIO_SEQUENCE_PENDING=1,AUDIO_SEQUENCE_WAITING=2,
      AUDIO_SEQUENCE_INVALID=-140,AUDIO_SEQUENCE_FORMAT=-141,
      AUDIO_SEQUENCE_UNSUPPORTED=-142,AUDIO_SEQUENCE_BUSY=-143,
      AUDIO_SEQUENCE_TIME=-144,AUDIO_SEQUENCE_CYCLE=-145};
enum {AUDIO_SEQUENCE_MIDI=0,AUDIO_SEQUENCE_TEMPO=1,AUDIO_SEQUENCE_END=2};

typedef struct {
    const uint8_t *events;
    uint32_t bytes,tempo;
    uint16_t id,division;
    uint8_t numerator,denominator;
} AudioSequenceRecord;
typedef struct {
    const uint8_t *bytes;
    uint32_t size,count;
    AudioSequenceRecord records[AUDIO_SEQUENCE_RECORDS];
} AudioSequenceCatalog;
typedef struct {
    uint64_t tick,time_us;
    const uint8_t *payload;
    uint32_t payload_size,tempo;
    uint8_t kind,status,data[2],data_count,internal;
} AudioSequenceEvent;
typedef struct {
    void *context;
    /* Same stable event is polled until accepted:0 completed,1 pending,negative
     * failure. Accept each event exactly once. Returning0 ends borrowing of the
     * mutable event descriptor: queued voices must copy needed fields before
     * the next decode. Payload bytes remain leased until close. Returning1
     * keeps the descriptor and payload stable until acceptance/cancellation.
     * MIDI retains native status and
     * velocity-zero NOTE_ON encoding; internal marks loop-control operations
     * already performed by this sequencer, not SPU controller replacements.
     * Instrument/sample ownership, ADSR and actual voice mixing belong here. */
    int (*event)(void *context,const AudioSequenceEvent *event);
    /* Cancel outstanding event work and quiesce every borrowed source/event
     * pointer before returning0.1 means poll again; negative retains ownership.
     * Also called on normal close after END, allowing voice release/drain. */
    int (*stop)(void *context);
} AudioSequenceSink;
typedef struct {
    nsf_resource_view view;
    AudioSequenceCatalog catalog;
    AudioSequenceRecord record;
    AudioSequenceSink sink;
    AudioSequenceEvent event;
    uint64_t due_us,due_tick,clock_us,loop_tick;
    uint32_t cursor,fraction,tempo,next_cursor,loop_cursor;
    uint8_t running_status,next_status,loop_count,loop_armed,loop_active,has_loop;
    int opened,ended,pending_event,stopped,error;
} AudioSequence;

/* Original music item pQES/SEP version0: six-byte prefix, independent13-byte
 * BE record headers, MIDI event spans, then0..3 zero alignment bytes. Bounds,
 * unique record IDs, all VLQs/status/data and exact END boundaries validate
 * before replacement. Records are independent sequences, not parallel tracks.
 * Pure borrowed views; the backing bytes must outlive the catalog/record users.
 * Sixteen records is a checked port budget, not an inferred PS1 format limit. */
int audio_sequence_catalog_bind(AudioSequenceCatalog *catalog,const void *bytes,uint32_t size);
/* Stable caller-owned sequence storage must initially be{0}; never copy/move
 * it after acquisition because the sink can borrow its event descriptor.
 * Acquire a distinct page borrow from an already READY NSF ticket. The ticket
 * caller keeps/releases its request reference independently. Real music lives
 * in ordinary page0, entry type13, normally item2; item and record are explicit.
 * No copy of a caller's live nsf_resource_view and no heap allocation.
 * start_us is the actual sound-clock origin, not an inferred frame timestamp.
 * Caller keeps sink/voice state stable until successful close. On any failure
 * after acquisition, close is still mandatory to release retained storage. */
int audio_sequence_acquire(AudioSequence *sequence,nsf_resource_ticket ticket,
                            uint32_t item,uint32_t record,uint64_t start_us,
                            const AudioSequenceSink *sink);
/* Monotonic caller-supplied sound clock. Event budget bounds zero-delta runs
 * and loop work; pending events preserve their original timestamps. Tempo uses
 * integer microseconds/quarter with exact carried fractional division. This
 * does not establish PSY-Q timer/interrupt quantization parity. Native FF51
 * tempo carries three bytes directly, without an SMF metadata length byte.
 * Native CC99=20/CC6/CC99=30 loop ownership/cursor rules are implemented, incl.
 * count127's discarded post-loop delta and zero-wait restart. No implicit whole
 * sequence repeat, arbitrary tempo, simultaneous SEP playback or synthesized
 * audio is supplied. EOF retains the resource until explicit close. */
int audio_sequence_advance(AudioSequence *sequence,uint64_t now_us,uint32_t event_budget);
int audio_sequence_close(AudioSequence *sequence);
#endif
