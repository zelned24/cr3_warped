/* Adversarial Stress-Test Suite for Crash Bandicoot 3DS Audio Subsystem (Milestone M2).
 *
 * Verifies under extreme / adversarial conditions:
 * 1. High-frequency rapid BGM theme switching (10,000+ transitions across all 8 themes).
 * 2. Out-of-bounds theme & SFX enum values (-1, 99, BGM_COUNT, BGM_NONE, INT_MIN, INT_MAX).
 * 3. Concurrent channel stress: rapid SFX firing on channels 0..3 while BGM loops on channel 7.
 * 4. Multi-cycle audio_init() / audio_exit() lifecycles with zero linearAlloc memory leaks.
 * 5. Complete 64-element level BGM mapping table bounds and authentic disc soundtrack validation.
 */
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "3ds.h"
#include "audio.h"

#define CANARY_MAGIC 0xDEADBEEFCAFEBABEULL
#define TOTAL_AUDIO_ALLOCS (SFX_COUNT + (BGM_COUNT - 1))
#define MOCK_NDSP_CHANNELS 8
#define MAX_BLOCKS 64

/* Mock allocation block with pre- and post-canaries */
typedef struct {
    uint64_t canary_pre;
    void *ptr;
    size_t bytes;
    int flushed;
    uint64_t canary_post;
} alloc_block;

static alloc_block blocks[MAX_BLOCKS];
static ndspWaveBuf *queues[MOCK_NDSP_CHANNELS];
static uint64_t channel_canaries[MOCK_NDSP_CHANNELS];

static int live = 0;
static int fail_init = 0;
static int fail_alloc = 0;
static int alloc_count = 0;
static int free_count = 0;
static int init_calls = 0;
static int exit_calls = 0;
static int submissions = 0;
static int sfx_submissions = 0;
static int bgm_submissions = 0;

static inline bool is_valid_channel(int ch)
{
    return (ch >= 0 && ch < 4) || ch == 7;
}

static void verify_all_canaries(void)
{
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (blocks[i].ptr) {
            assert(blocks[i].canary_pre == CANARY_MAGIC);
            assert(blocks[i].canary_post == CANARY_MAGIC);
        }
    }
    for (int i = 0; i < MOCK_NDSP_CHANNELS; i++) {
        assert(channel_canaries[i] == CANARY_MAGIC);
    }
}

/* ------------------------------------------------------------------------- */
/* Mock libctru linearAlloc / NDSP functions                                 */
/* ------------------------------------------------------------------------- */
void *linearAlloc(size_t size)
{
    if (fail_alloc)
        return NULL;

    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (blocks[i].ptr)
            continue;
        void *p = malloc(size);
        assert(p != NULL);
        blocks[i].canary_pre = CANARY_MAGIC;
        blocks[i].ptr = p;
        blocks[i].bytes = size;
        blocks[i].flushed = 0;
        blocks[i].canary_post = CANARY_MAGIC;
        alloc_count++;
        return p;
    }
    fprintf(stderr, "FATAL: Out of mock blocks in linearAlloc!\n");
    abort();
}

void linearFree(void *ptr)
{
    if (!ptr)
        return;
    /* In 3DS hardware, DSP processing MUST be stopped before freeing sample memory. */
    assert(!live && "Illegal attempt to free linear memory while DSP service is active!");
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (blocks[i].ptr != ptr)
            continue;
        assert(blocks[i].canary_pre == CANARY_MAGIC);
        assert(blocks[i].canary_post == CANARY_MAGIC);
        free(ptr);
        memset(&blocks[i], 0, sizeof(blocks[i]));
        free_count++;
        return;
    }
    fprintf(stderr, "FATAL: linearFree on unrecognized pointer %p!\n", ptr);
    abort();
}

Result ndspInit(void)
{
    init_calls++;
    assert(!live && "ndspInit called while already live!");
    if (fail_init)
        return (Result)-1;
    live = 1;
    for (int i = 0; i < MOCK_NDSP_CHANNELS; i++) {
        queues[i] = NULL;
        channel_canaries[i] = CANARY_MAGIC;
    }
    return 0;
}

void ndspExit(void)
{
    exit_calls++;
    assert(live && "ndspExit called while not live!");
    /* Ensure all channel queues were explicitly cleared before hardware shutdown */
    for (int i = 0; i < MOCK_NDSP_CHANNELS; i++) {
        assert(queues[i] == NULL && "ndspExit with uncleared wavebuf in channel queue!");
    }
    live = 0;
}

void ndspSetOutputMode(int mode)
{
    assert(live);
    assert(mode == NDSP_OUTPUT_STEREO);
}

void ndspChnReset(int ch)
{
    assert(live);
    assert(is_valid_channel(ch));
    queues[ch] = NULL;
}

void ndspChnWaveBufClear(int ch)
{
    assert(live);
    assert(is_valid_channel(ch));
    queues[ch] = NULL;
}

void ndspChnSetFormat(int ch, int format)
{
    assert(live);
    assert(is_valid_channel(ch));
    assert(format == NDSP_FORMAT_MONO_PCM16);
}

void ndspChnSetRate(int ch, float rate)
{
    assert(live);
    assert(is_valid_channel(ch));
    assert((int)rate == 22050);
}

void ndspChnSetMix(int ch, float mix[12])
{
    assert(live);
    assert(is_valid_channel(ch));
    assert(mix[0] > 0.0f && mix[0] == mix[1]);
    for (int i = 2; i < 12; i++) {
        assert(mix[i] == 0.0f);
    }
}

void DSP_FlushDataCache(const void *ptr, uint32_t size)
{
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (blocks[i].ptr != ptr)
            continue;
        assert(blocks[i].bytes == size);
        blocks[i].flushed = 1;
        return;
    }
    fprintf(stderr, "FATAL: DSP_FlushDataCache on unallocated address %p!\n", ptr);
    abort();
}

void ndspChnWaveBufAdd(int ch, ndspWaveBuf *wave)
{
    assert(live);
    assert(is_valid_channel(ch));
    assert(wave != NULL);
    assert(!queues[ch] && "Channel wavebuf queue was not cleared before add!");

    /* Descriptor isolation: no two channels may own the same wavebuf descriptor */
    for (int i = 0; i < MOCK_NDSP_CHANNELS; i++) {
        assert(queues[i] != wave && "Wavebuf descriptor shared across multiple active queues!");
    }

    /* Verify payload data address belongs to a live, flushed linearAlloc block */
    int found = 0;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (blocks[i].ptr != wave->data_vaddr)
            continue;
        assert(blocks[i].flushed && "Audio data cache was not flushed prior to DSP playback!");
        assert(blocks[i].bytes == wave->nsamples * sizeof(int16_t));
        found = 1;
        break;
    }
    assert(found && "Wavebuf data_vaddr not found in mock linearAlloc registry!");

    /* BGM is channel 7 and MUST be looping; SFX channels 0..3 MUST NOT loop */
    if (ch == 7) {
        assert(wave->looping && "Channel 7 BGM stream must be flagged looping!");
        bgm_submissions++;
    } else {
        assert(!wave->looping && "Channels 0..3 SFX wavebufs must not be flagged looping!");
        sfx_submissions++;
    }

    queues[ch] = wave;
    submissions++;
}

/* Prototype for level.c authentic soundtrack lookup */
BgmTheme level_get_bgm_theme(int level_id);

/* Stubs for level.c SD streaming symbols */
int level_sd_available(void) { return 0; }
const uint8_t *level_sd_blob(void) { return NULL; }
uint32_t level_sd_blob_size(void) { return 0; }

/* ------------------------------------------------------------------------- */
/* Adversarial Test Suites                                                   */
/* ------------------------------------------------------------------------- */

/* LCG Pseudo-random number generator for deterministic adversarial testing */
static uint32_t s_prng = 0x19981103; /* PS1 Crash 3 European release date seed */
static inline uint32_t next_prng(void)
{
    s_prng = s_prng * 1103515245u + 12345u;
    return s_prng;
}

/* =========================================================================
 * TEST 1: High-Frequency Rapid BGM Theme Switching (10,000+ transitions)
 * ========================================================================= */
static void test_rapid_bgm_switching(void)
{
    printf("[STRESS 1] Starting Rapid BGM Theme Switching (11,500 total transitions)...\n");

    static const BgmTheme valid_themes[] = {
        BGM_TITLE,
        BGM_WARP_ROOM,
        BGM_MEDIEVAL,
        BGM_ARABIAN,
        BGM_ORIENTAL,
        BGM_EGYPTIAN,
        BGM_FUTURE,
        BGM_BOSS
    };
    const int num_themes = (int)(sizeof(valid_themes) / sizeof(valid_themes[0]));

    audio_init();
    assert(live && alloc_count - free_count == TOTAL_AUDIO_ALLOCS);

    /* 1A. Round-robin cycle across all 8 themes (2,000 transitions) */
    int prev_bgm_subs = bgm_submissions;
    for (int i = 0; i < 2000; i++) {
        BgmTheme t = valid_themes[i % num_themes];
        audio_play_bgm(t);
        assert(queues[7] != NULL);
        assert(queues[7]->looping);
        /* SFX channels 0..3 must remain untouched */
        for (int c = 0; c < 4; c++) {
            assert(queues[c] == NULL);
        }
        /* Unused channels 4..6 must remain NULL */
        for (int c = 4; c < 7; c++) {
            assert(queues[c] == NULL);
        }
    }
    assert(bgm_submissions == prev_bgm_subs + 2000);
    printf("  [PASS] 1A: 2,000 sequential round-robin theme transitions verified.\n");

    /* 1B. Pseudo-random jump transitions across all 8 themes (5,000 transitions) */
    BgmTheme current = valid_themes[1999 % num_themes];
    for (int i = 0; i < 5000; i++) {
        BgmTheme next = valid_themes[next_prng() % num_themes];
        int was_different = (next != current);
        int before = bgm_submissions;
        audio_play_bgm(next);
        assert(queues[7] != NULL && queues[7]->looping);
        if (was_different) {
            assert(bgm_submissions == before + 1);
            current = next;
        } else {
            /* Idempotent: repeated same theme must not re-submit to DSP */
            assert(bgm_submissions == before);
        }
    }
    printf("  [PASS] 1B: 5,000 pseudo-random theme transitions with idempotency checks verified.\n");

    /* 1C. Rapid alternating ping-pong between 2 adjacent themes (3,000 transitions) */
    prev_bgm_subs = bgm_submissions;
    for (int i = 0; i < 3000; i++) {
        BgmTheme t = (i % 2 == 0) ? BGM_MEDIEVAL : BGM_ARABIAN;
        audio_play_bgm(t);
        assert(queues[7] != NULL && queues[7]->looping);
    }
    assert(bgm_submissions == prev_bgm_subs + 3000);
    printf("  [PASS] 1C: 3,000 rapid ping-pong transitions (Medieval <-> Arabian) verified.\n");

    /* 1D. Rapid switching interleaved with audio_stop_bgm() (1,000 transitions) */
    for (int i = 0; i < 1000; i++) {
        BgmTheme t = valid_themes[i % num_themes];
        audio_play_bgm(t);
        assert(queues[7] != NULL && queues[7]->looping);

        if (i % 3 == 0) {
            audio_stop_bgm();
            assert(queues[7] == NULL);
            /* Calling stop again while already stopped must be a safe no-op */
            audio_stop_bgm();
            assert(queues[7] == NULL);
        }
    }
    printf("  [PASS] 1D: 1,000 transitions interleaved with stop/idempotent stops verified.\n");

    /* 1E. 500 consecutive repeated calls of the same theme */
    audio_play_bgm(BGM_WARP_ROOM);
    assert(queues[7] != NULL);
    int baseline_subs = bgm_submissions;
    for (int i = 0; i < 500; i++) {
        audio_play_bgm(BGM_WARP_ROOM);
        assert(queues[7] != NULL && queues[7]->looping);
        assert(bgm_submissions == baseline_subs); /* Zero additional submissions */
    }
    printf("  [PASS] 1E: 500 identical consecutive theme calls verified strictly idempotent.\n");

    audio_exit();
    assert(!live && alloc_count == free_count);
    verify_all_canaries();
}

/* =========================================================================
 * TEST 2: Out-of-Bounds Theme & SFX Enum Values (Boundary Hardening)
 * ========================================================================= */
static void test_out_of_bounds_enums(void)
{
    printf("[STRESS 2] Starting Out-of-Bounds Theme & SFX Enum Hardening...\n");

    audio_init();
    assert(live);

    static const BgmTheme bad_themes[] = {
        (BgmTheme)-1,
        (BgmTheme)-2,
        (BgmTheme)-1000,
        (BgmTheme)INT_MIN,
        (BgmTheme)BGM_NONE,
        (BgmTheme)BGM_COUNT,
        (BgmTheme)(BGM_COUNT + 1),
        (BgmTheme)99,
        (BgmTheme)1000,
        (BgmTheme)INT_MAX
    };
    const int num_bad_themes = (int)(sizeof(bad_themes) / sizeof(bad_themes[0]));

    /* A. Out-of-bounds themes when stopped */
    int subs_before = submissions;
    for (int i = 0; i < num_bad_themes; i++) {
        audio_play_bgm(bad_themes[i]);
        assert(submissions == subs_before && "Out-of-bounds theme submitted to DSP!");
        assert(queues[7] == NULL && "Out-of-bounds theme corrupted channel 7 queue!");
    }
    printf("  [PASS] 2A: All %d out-of-bounds BGM themes safely rejected while stopped.\n", num_bad_themes);

    /* B. Out-of-bounds themes while an active theme is playing */
    audio_play_bgm(BGM_ORIENTAL);
    assert(queues[7] != NULL && queues[7]->looping);
    void *active_ptr = queues[7]->data_vaddr;
    uint32_t active_nsamples = queues[7]->nsamples;
    subs_before = submissions;

    for (int i = 0; i < num_bad_themes; i++) {
        audio_play_bgm(bad_themes[i]);
        /* Active theme MUST NOT be stopped or corrupted by bad theme calls */
        assert(submissions == subs_before);
        assert(queues[7] != NULL);
        assert(queues[7]->looping);
        assert(queues[7]->data_vaddr == active_ptr);
        assert(queues[7]->nsamples == active_nsamples);
    }
    printf("  [PASS] 2B: Active BGM stream preserved without corruption during %d bad theme calls.\n", num_bad_themes);

    /* C. Out-of-bounds SFX IDs */
    static const SfxId bad_sfx[] = {
        (SfxId)-1,
        (SfxId)-100,
        (SfxId)INT_MIN,
        (SfxId)SFX_COUNT,
        (SfxId)(SFX_COUNT + 1),
        (SfxId)99,
        (SfxId)1000,
        (SfxId)INT_MAX
    };
    const int num_bad_sfx = (int)(sizeof(bad_sfx) / sizeof(bad_sfx[0]));
    subs_before = submissions;

    for (int i = 0; i < num_bad_sfx; i++) {
        audio_play_sfx(bad_sfx[i]);
        assert(submissions == subs_before && "Out-of-bounds SFX ID submitted to DSP!");
        /* None of the SFX channels 0..3 should have been populated */
        for (int c = 0; c < 4; c++) {
            assert(queues[c] == NULL);
        }
        /* Active BGM on channel 7 must still be untouched */
        assert(queues[7] != NULL && queues[7]->looping);
    }
    printf("  [PASS] 2C: All %d out-of-bounds SFX IDs safely rejected without channel side-effects.\n", num_bad_sfx);

    audio_exit();
    assert(!live && alloc_count == free_count);
    verify_all_canaries();
}

/* =========================================================================
 * TEST 3: Concurrent Stress & Channel Isolation (SFX 0..3 vs BGM 7)
 * ========================================================================= */
static void test_concurrent_sfx_and_bgm(void)
{
    printf("[STRESS 3] Starting Concurrent SFX & BGM Channel Isolation Stress (20,000 steps)...\n");

    audio_init();
    assert(live);

    /* Start BGM on dedicated channel 7 */
    audio_play_bgm(BGM_EGYPTIAN);
    assert(queues[7] != NULL && queues[7]->looping);
    void *bgm_ptr = queues[7]->data_vaddr;
    uint32_t bgm_nsamples = queues[7]->nsamples;
    ndspWaveBuf *bgm_desc = queues[7];

    /* 3A. 10,000 rapid SFX calls cycling all 12 clips across channels 0..3 */
    for (int i = 0; i < 10000; i++) {
        SfxId sfx = (SfxId)(i % SFX_COUNT);
        audio_play_sfx(sfx);

        /* CRITICAL ISOLATION ASSERTIONS:
         * 1. Channel 7 descriptor is NEVER stomped by SFX calls.
         * 2. Channel 7 descriptor address remains constant.
         * 3. Channel 7 sample data address and sample count remain identical.
         * 4. Channel 7 looping flag remains true.
         * 5. Channels 0..3 descriptors NEVER equal channel 7 descriptor.
         * 6. Channels 0..3 looping flags remain false.
         */
        assert(queues[7] == bgm_desc);
        assert(queues[7]->looping == true);
        assert(queues[7]->data_vaddr == bgm_ptr);
        assert(queues[7]->nsamples == bgm_nsamples);

        int active_sfx_ch = (i) % 4;
        assert(queues[active_sfx_ch] != NULL);
        assert(queues[active_sfx_ch] != bgm_desc);
        assert(queues[active_sfx_ch]->looping == false);
        assert(queues[active_sfx_ch]->data_vaddr != bgm_ptr);

        /* Unused channels 4..6 must remain strictly NULL */
        for (int c = 4; c < 7; c++) {
            assert(queues[c] == NULL);
        }
    }
    printf("  [PASS] 3A: 10,000 rapid SFX triggers on channels 0..3 verified zero stomping on channel 7.\n");

    /* 3B. High-frequency interleaved storm: 10,000 steps mixing SFX (80%) and BGM switches (20%) */
    static const BgmTheme themes[] = {
        BGM_TITLE, BGM_WARP_ROOM, BGM_MEDIEVAL, BGM_ARABIAN,
        BGM_ORIENTAL, BGM_EGYPTIAN, BGM_FUTURE, BGM_BOSS
    };
    BgmTheme active_theme = BGM_EGYPTIAN;

    for (int step = 0; step < 10000; step++) {
        uint32_t roll = next_prng() % 100;
        if (roll < 80) {
            /* 80%: Fire random SFX */
            SfxId sfx = (SfxId)(next_prng() % SFX_COUNT);
            audio_play_sfx(sfx);
        } else if (roll < 95) {
            /* 15%: Switch BGM theme */
            BgmTheme next_t = themes[next_prng() % 8];
            audio_play_bgm(next_t);
            active_theme = next_t;
        } else {
            /* 5%: Pause / resume BGM */
            audio_stop_bgm();
            assert(queues[7] == NULL);
            /* Fire a burst of SFX while BGM is stopped */
            for (int k = 0; k < 4; k++) {
                audio_play_sfx((SfxId)(k % SFX_COUNT));
            }
            assert(queues[7] == NULL);
            /* Resume BGM */
            audio_play_bgm(active_theme);
            assert(queues[7] != NULL && queues[7]->looping);
        }

        /* Verify channel 7 descriptor is never assigned to any SFX channel */
        for (int c = 0; c < 4; c++) {
            if (queues[c]) {
                assert(queues[c] != queues[7]);
                assert(!queues[c]->looping);
            }
        }
        for (int c = 4; c < 7; c++) {
            assert(queues[c] == NULL);
        }
    }
    printf("  [PASS] 3B: 10,000 interleaved SFX/BGM storm steps with pause/resumes verified robust.\n");

    audio_exit();
    assert(!live && alloc_count == free_count);
    verify_all_canaries();
}

/* =========================================================================
 * TEST 4: Multiple audio_init() and audio_exit() Cycles (Zero Memory Leaks)
 * ========================================================================= */
static void test_lifecycle_and_zero_leaks(void)
{
    printf("[STRESS 4] Starting Lifecycle & Zero Memory Leak Stress (200 cycles)...\n");

    /* 4A. 100 consecutive full cycles of init -> play -> exit */
    for (int cycle = 0; cycle < 100; cycle++) {
        assert(alloc_count == free_count && "Pre-init leak detected!");
        audio_init();
        assert(live);
        assert(alloc_count - free_count == TOTAL_AUDIO_ALLOCS);

        /* Play BGM and SFX in each cycle */
        audio_play_bgm(BGM_MEDIEVAL);
        assert(queues[7] != NULL && queues[7]->looping);
        for (int s = 0; s < SFX_COUNT; s++) {
            audio_play_sfx((SfxId)s);
        }

        audio_exit();
        assert(!live);
        assert(alloc_count == free_count && "Post-exit memory leak in linearAlloc detected!");
        for (int c = 0; c < MOCK_NDSP_CHANNELS; c++) {
            assert(queues[c] == NULL);
        }
    }
    printf("  [PASS] 4A: 100 consecutive full init/play/exit cycles completed with exactly 0 leaks.\n");

    /* 4B. Re-initialization & re-exit idempotency */
    audio_init();
    int current_allocs = alloc_count;
    /* 10 duplicate init calls */
    for (int i = 0; i < 10; i++) {
        audio_init();
        assert(alloc_count == current_allocs && "Duplicate audio_init re-allocated memory!");
    }
    audio_play_bgm(BGM_FUTURE);
    audio_exit();
    assert(alloc_count == free_count);
    /* 10 duplicate exit calls */
    for (int i = 0; i < 10; i++) {
        audio_exit();
        assert(alloc_count == free_count && "Duplicate audio_exit caused double-free or crash!");
    }
    printf("  [PASS] 4B: Re-initialization and re-exit idempotency verified.\n");

    /* 4C. Fault injection & graceful degradation */
    fail_init = 1;
    audio_init();
    assert(!live && "audio_init succeeded despite ndspInit failure!");
    assert(alloc_count == free_count && "Memory allocated despite ndspInit failure!");
    /* Calling play on uninitialized system must be completely safe */
    audio_play_bgm(BGM_BOSS);
    audio_play_sfx(SFX_CRATE_BREAK);
    audio_stop_bgm();
    audio_exit();
    assert(alloc_count == free_count);
    fail_init = 0;

    /* Failure during linearAlloc */
    fail_alloc = 1;
    audio_init();
    assert(live);
    assert(alloc_count == free_count);
    audio_play_bgm(BGM_TITLE);
    audio_play_sfx(SFX_JUMP);
    audio_exit();
    assert(!live && alloc_count == free_count);
    fail_alloc = 0;
    printf("  [PASS] 4C: Fault injection (ndspInit fail, linearAlloc fail) safely handled.\n");

    verify_all_canaries();
}

/* =========================================================================
 * TEST 5: Complete 64-Element Level BGM Mapping Robustness
 * ========================================================================= */
static void test_level_bgm_mapping(void)
{
    printf("[STRESS 5] Starting Level BGM Mapping Robustness & Authentic Disc Soundtrack Test...\n");

    /* Negative indices */
    assert(level_get_bgm_theme(-1) == BGM_NONE);
    assert(level_get_bgm_theme(-100) == BGM_NONE);
    assert(level_get_bgm_theme(INT_MIN) == BGM_NONE);

    /* Out-of-bounds indices */
    assert(level_get_bgm_theme(64) == BGM_NONE);
    assert(level_get_bgm_theme(65) == BGM_NONE);
    assert(level_get_bgm_theme(1000) == BGM_NONE);
    assert(level_get_bgm_theme(INT_MAX) == BGM_NONE);

    /* Test all 64 table slots (must never segfault or access out of bounds) */
    for (int id = 0; id < 64; id++) {
        BgmTheme t = level_get_bgm_theme(id);
        assert(t >= BGM_NONE && t < BGM_COUNT);
    }

    /* Spot-check authentic PS1 disc soundtrack assignments */
    assert(level_get_bgm_theme(0x02) == BGM_WARP_ROOM); /* Time Twister Hub */
    assert(level_get_bgm_theme(0x03) == BGM_BOSS);      /* Dingodile */
    assert(level_get_bgm_theme(0x04) == BGM_BOSS);      /* Dr. N. Tropy */
    assert(level_get_bgm_theme(0x05) == BGM_BOSS);      /* Dr. N. Gin */
    assert(level_get_bgm_theme(0x06) == BGM_BOSS);      /* Tiny Tiger */
    assert(level_get_bgm_theme(0x07) == BGM_BOSS);      /* Dr. Neo Cortex */

    assert(level_get_bgm_theme(0x0A) == BGM_ORIENTAL);  /* Level 1: Orient Express */
    assert(level_get_bgm_theme(0x0B) == BGM_MEDIEVAL);  /* Level 2: Toad Village */
    assert(level_get_bgm_theme(0x0E) == BGM_EGYPTIAN);  /* Level 3: Under Pressure */
    assert(level_get_bgm_theme(0x0F) == BGM_MEDIEVAL);  /* Level 4: Gee Wiz */
    assert(level_get_bgm_theme(0x12) == BGM_EGYPTIAN);  /* Level 9: Tomb Time */
    assert(level_get_bgm_theme(0x16) == BGM_ARABIAN);   /* Level 7: Hang'em High */
    assert(level_get_bgm_theme(0x1B) == BGM_FUTURE);    /* Level 16: Future Frenzy */
    assert(level_get_bgm_theme(0x1D) == BGM_MEDIEVAL);  /* Level 18: Double Header */
    assert(level_get_bgm_theme(0x22) == BGM_ARABIAN);   /* Level 22: Flaming Passion */
    assert(level_get_bgm_theme(0x24) == BGM_EGYPTIAN);  /* Level 24: Bug Lite */

    assert(level_get_bgm_theme(0x3B) == BGM_NONE);      /* Game Over: authentic silence */
    assert(level_get_bgm_theme(0x3C) == BGM_TITLE);     /* Title Screen */

    printf("  [PASS] 5A: All 64 table slots, boundary clamped negative/overflow IDs, and authentic soundtrack tracks verified.\n");
}

/* =========================================================================
 * Main Stress Harness Entrypoint
 * ========================================================================= */
int main(void)
{
    printf("=================================================================\n");
    printf(" ADVERSARIAL STRESS TEST SUITE: Crash Bandicoot 3DS Audio (M2)   \n");
    printf("=================================================================\n");

    test_rapid_bgm_switching();
    test_out_of_bounds_enums();
    test_concurrent_sfx_and_bgm();
    test_lifecycle_and_zero_leaks();
    test_level_bgm_mapping();

    printf("=================================================================\n");
    printf(" ALL ADVERSARIAL AUDIO STRESS TESTS PASSED WITH 0 LEAKS/ERRORS.  \n");
    printf("=================================================================\n");
    return 0;
}
