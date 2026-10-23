#include <assert.h>
#ifdef NDEBUG
#error Audio tests require enabled assertions
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "3ds.h"
#include "audio.h"

#define TOTAL_AUDIO_ALLOCS (SFX_COUNT + (BGM_COUNT - 1))
#define MOCK_NDSP_CHANNELS 8

static ndspWaveBuf *queues[MOCK_NDSP_CHANNELS];
static struct { void *ptr; size_t bytes; int flushed; } blocks[64];
static int live, fail_init, fail_alloc, allocs, init_calls, submissions;

static inline bool is_valid_channel(int ch)
{
    return (ch >= 0 && ch < 4) || ch == 7;
}

void *linearAlloc(size_t size)
{
    if (fail_alloc) return NULL;
    for (int i = 0; i < 64; i++) {
        if (blocks[i].ptr) continue;
        blocks[i].ptr = malloc(size);
        assert(blocks[i].ptr);
        blocks[i].bytes = size;
        blocks[i].flushed = 0;
        allocs++;
        return blocks[i].ptr;
    }
    abort();
}
void linearFree(void *ptr)
{
    assert(!live); /* The DSP may still reference samples until ndspExit. */
    for (int i = 0; i < 64; i++) {
        if (blocks[i].ptr != ptr) continue;
        free(ptr);
        memset(&blocks[i], 0, sizeof(blocks[i]));
        allocs--;
        return;
    }
    abort();
}
Result ndspInit(void)
{
    init_calls++;
    assert(!live);
    if (fail_init) return -1;
    live = 1;
    return 0;
}
void ndspExit(void)
{
    assert(live);
    for (int i = 0; i < MOCK_NDSP_CHANNELS; i++) assert(!queues[i]);
    live = 0;
}
void ndspSetOutputMode(int mode) { assert(live && mode == NDSP_OUTPUT_STEREO); }
void ndspChnReset(int ch) { assert(live && is_valid_channel(ch)); queues[ch] = NULL; }
void ndspChnWaveBufClear(int ch) { assert(live && is_valid_channel(ch)); queues[ch] = NULL; }
void ndspChnSetFormat(int ch, int format) { assert(is_valid_channel(ch) && format == NDSP_FORMAT_MONO_PCM16); }
void ndspChnSetRate(int ch, float rate) { assert(is_valid_channel(ch) && rate == 22050); }
void ndspChnSetMix(int ch, float mix[12])
{
    assert(is_valid_channel(ch) && mix[0] > 0 && mix[0] == mix[1]);
    for (int i = 2; i < 12; i++) assert(mix[i] == 0);
}
void DSP_FlushDataCache(const void *ptr, uint32_t size)
{
    for (int i = 0; i < 64; i++) {
        if (blocks[i].ptr != ptr) continue;
        assert(blocks[i].bytes == size);
        blocks[i].flushed = 1;
        return;
    }
    abort();
}
void ndspChnWaveBufAdd(int ch, ndspWaveBuf *wave)
{
    assert(live && is_valid_channel(ch) && !queues[ch]);
    for (int i = 0; i < MOCK_NDSP_CHANNELS; i++) assert(queues[i] != wave);
    int found = 0;
    for (int i = 0; i < 64; i++) {
        if (blocks[i].ptr != wave->data_vaddr) continue;
        assert(blocks[i].flushed && blocks[i].bytes == wave->nsamples * sizeof(int16_t));
        found = 1;
    }
    assert(found);
    if (ch == 7) {
        assert(wave->looping);
    } else {
        assert(!wave->looping);
    }
    queues[ch] = wave;
    submissions++;
}

int main(void)
{
    audio_play_sfx(SFX_JUMP);
    audio_exit();
    fail_init = 1;
    audio_init();
    audio_play_sfx(SFX_JUMP);
    audio_exit();
    assert(!live && !allocs && !submissions);
    fail_init = 0;
    assert(!audio_init_native());
    assert(live && !allocs && !submissions);
    int native_before = init_calls;
    assert(!audio_init_native());
    assert(init_calls == native_before && !allocs);
    /* Explicit prototype startup can promote the shared native-only service
     * without initializing NDSP twice. Native startup itself allocates no clips. */
    audio_init();
    assert(live && allocs == TOTAL_AUDIO_ALLOCS && init_calls == native_before);
    int before = init_calls;
    audio_init();
    assert(init_calls == before && allocs == TOTAL_AUDIO_ALLOCS);

    /* Repeating the same clip must use separate descriptors while samples
     * remain shared and immutable across all four outstanding channels. */
    for (int i = 0; i < 4; i++) audio_play_sfx(SFX_JUMP);
    for (int i = 1; i < 4; i++) {
        assert(queues[i] != queues[0]);
        assert(queues[i]->data_vaddr == queues[0]->data_vaddr);
    }
    for (int i = 0; i < 100; i++) audio_play_sfx((SfxId)(i % SFX_COUNT));
    before = submissions;
    audio_play_sfx((SfxId)-1);
    audio_play_sfx(SFX_COUNT);
    assert(submissions == before);
    audio_exit();
    assert(!live && !allocs);
    audio_exit();

    fail_alloc = 1;
    audio_init();
    before = submissions;
    audio_play_sfx(SFX_JUMP);
    assert(submissions == before && !allocs);
    audio_exit();
    fail_alloc = 0;
    audio_init();
    audio_play_sfx(SFX_SPIN);
    assert(queues[0] && !queues[1] && !queues[2] && !queues[3]);

    /* Verify BGM stream on channel 7, looping flag, idempotency, theme changes, stop */
    audio_play_bgm(BGM_WARP_ROOM);
    assert(queues[7] && queues[7]->looping);
    before = submissions;
    audio_play_bgm(BGM_WARP_ROOM); /* Idempotent call */
    assert(submissions == before);
    audio_play_bgm(BGM_MEDIEVAL); /* Change theme */
    assert(queues[7] && queues[7]->looping && submissions == before + 1);
    audio_stop_bgm();
    assert(!queues[7]);
    before = submissions;
    audio_play_bgm(BGM_NONE);
    audio_play_bgm(BGM_COUNT);
    audio_play_bgm((BgmTheme)-1);
    assert(submissions == before && !queues[7]);

    audio_exit();
    assert(!live && !allocs);
    fail_init = 1;
    assert(audio_init_native() < 0 && !live && !allocs);
    fail_init = 0;
    assert(!audio_init_native() && live && !allocs);
    audio_exit();
    assert(!live && !allocs);
    puts("PASS audio: queue ownership, replay, cache, lifecycle and failures");
    return 0;
}
