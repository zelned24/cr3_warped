#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H
#include <3ds.h>
#include <stddef.h>
#include <stdint.h>
#include "audio.h"

enum { AUDIO_STREAM_BUFFERS = 3, AUDIO_STREAM_BLOCKS_PER_BUFFER = 64,
       AUDIO_STREAM_SAMPLES_PER_BUFFER = 28 * AUDIO_STREAM_BLOCKS_PER_BUFFER,
       AUDIO_STREAM_PCM_BYTES = 2 * AUDIO_STREAM_BUFFERS * AUDIO_STREAM_SAMPLES_PER_BUFFER,
       AUDIO_STREAM_MAX_SOURCE_BYTES = 65536 };
#define AUDIO_STREAM_NO_LOOP SIZE_MAX
#define AUDIO_STREAM_REPEAT_FOREVER UINT32_MAX
enum { AUDIO_STREAM_OK = 0, AUDIO_STREAM_INVALID = -1, AUDIO_STREAM_BUSY = -2,
       AUDIO_STREAM_NO_MEMORY = -3 };

/* Source remains borrowed and immutable until stop/drain. Each start is one
 * bounded original ADIO span, not XA audio. repeat_count requests extra jumps
 * only when the native end block has repeat flag2. A missing in-stream loop
 * marker requires an explicit external_loop_byte; no implicit loop is invented.
 * Source pointer arithmetic must not overflow, and the source cannot overlap
 * any claimed stream's state/descriptors or PCM pool. */
typedef struct {
    const uint8_t *data;
    size_t byte_size;
    size_t external_loop_byte;
    uint32_t repeat_count;
    AudioAdpcmHistory history;
} AudioStreamSample;

/* Caller-owned stable storage. Do not copy/move or alter fields while claimed.
 * Single application thread owns start/poll/set_controls/stop; the NDSP service
 * owns queued descriptors. Poll at least every frame; complete underruns recover
 * when polled again, but skipped audio time is not synthesized. */
typedef struct {
    ndspWaveBuf waves[AUDIO_STREAM_BUFFERS];
    int16_t *pcm;
    const uint8_t *source;
    size_t source_bytes, position, loop_byte;
    AudioAdpcmHistory history;
    uint32_t repeats_left;
    unsigned refill_index;
    int channel, active, source_done;
    uint8_t submitted[AUDIO_STREAM_BUFFERS];
} AudioStream;

/* Caller must initialize NDSP before use and reserve channel exclusively.
 * Channels0..3 and7 belong to the current synthesized engine and are rejected.
 * This API also rejects duplicate claims. One10752-byte linear pool per stream;
 * start/poll/restart perform no allocations. */
int audio_stream_init(AudioStream *stream, int channel);
int audio_stream_start(AudioStream *stream, const AudioStreamSample *sample,
                        uint16_t pitch, uint16_t left_volume, uint16_t right_volume);
/* Native SPU pitch0..0x3FFF;0 pauses,0x1000 is44100Hz. Stereo gains0..0x3FFF.
 * The caller supplies resolved pitch/mix; no ADSR or spatialization is guessed. */
int audio_stream_set_controls(AudioStream *stream, uint16_t pitch,
                              uint16_t left_volume, uint16_t right_volume);
int audio_stream_poll(AudioStream *stream); /* queued buffers, or a negative error */
int audio_stream_drained(const AudioStream *stream);
void audio_stream_stop(AudioStream *stream); /* retains reusable DMA storage */
/* Stop every stream, then call ndspExit, then release each pool. No NDSP calls
 * occur here. Releasing before service shutdown violates the caller contract. */
void audio_stream_release_after_ndsp_exit(AudioStream *stream);
/* Application lifecycle helpers visit only channels claimed by this module.
 * Poll returns total pending buffers; no additional manager storage/allocation.
 * Shutdown order: stop_all -> audio_exit/ndspExit -> release_all_after_ndsp_exit. */
int audio_stream_poll_all(void);
void audio_stream_stop_all(void);
void audio_stream_release_all_after_ndsp_exit(void);
#endif
