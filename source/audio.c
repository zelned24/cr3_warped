/* Crash Bandicoot: Warped - 3DS Audio System Implementation (ndsp). */
#include <3ds.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"

#define SAMPLERATE 22050
#define MAX_CHANNELS 4
#define BGM_CHANNEL 7

typedef struct {
    int16_t *data;
    uint32_t sample_count;
} sfx_clip;

static sfx_clip s_clips[SFX_COUNT];
static sfx_clip s_bgm_clips[BGM_COUNT];
/* NDSP keeps each descriptor in its channel's linked queue until cleared.
 * Samples may be shared, but a descriptor must never belong to two queues. */
static ndspWaveBuf s_channel_waves[MAX_CHANNELS];
static ndspWaveBuf s_bgm_wave;
static int s_audio_ready = 0;
static int s_next_channel = 0;
static BgmTheme s_current_bgm = BGM_NONE;
static int s_prototype_ready;

/* Explicit arithmetic shift, avoiding implementation-defined signed shifts
 * and undefined left shifts of negative ADPCM nibbles. Products fit int32_t. */
static int32_t adpcm_floor_shift(int32_t value, unsigned bits)
{
    int32_t divisor = (int32_t)(1u << bits);
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

int audio_decode_adpcm_block(const uint8_t *data, size_t data_size,
                             int16_t *pcm, size_t pcm_capacity,
                             AudioAdpcmHistory *history, uint8_t *flags)
{
    static const int16_t filters[5][2] = {
        {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}
    };
    if (!data || data_size != AUDIO_ADPCM_BLOCK_BYTES || !pcm ||
        pcm_capacity < AUDIO_ADPCM_BLOCK_SAMPLES || !history || !flags)
        return 0;

    unsigned shift = data[0] & 15u;
    unsigned predictor = data[0] >> 4;
    if (shift > 12) shift = 9;
    int32_t positive = predictor < 5 ? filters[predictor][0] : 0;
    int32_t negative = predictor < 5 ? filters[predictor][1] : 0;
    int32_t recent = history->recent;
    int32_t older = history->older;
    for (unsigned i = 0; i < AUDIO_ADPCM_BLOCK_SAMPLES; ++i) {
        unsigned nibble = (data[2 + i / 2] >> ((i & 1u) * 4)) & 15u;
        int32_t signed_nibble = nibble < 8 ? (int32_t)nibble : (int32_t)nibble - 16;
        int32_t sample = adpcm_floor_shift(signed_nibble * 4096, shift);
        sample += adpcm_floor_shift(recent * positive, 6);
        sample += adpcm_floor_shift(older * negative, 6);
        if (sample > INT16_MAX) sample = INT16_MAX;
        if (sample < INT16_MIN) sample = INT16_MIN;
        pcm[i] = (int16_t)sample;
        older = recent;
        recent = sample;
    }
    history->recent = (int16_t)recent;
    history->older = (int16_t)older;
    *flags = data[1];
    return 1;
}

static int16_t *alloc_and_generate(uint32_t count)
{
    int16_t *buf = (int16_t *)linearAlloc(count * sizeof(int16_t));
    if (buf)
        memset(buf, 0, count * sizeof(int16_t));
    return buf;
}

int32_t audio_init_native(void)
{
    if (s_audio_ready)
        return 0;

    Result res = ndspInit();
    if (R_FAILED(res)) return (int32_t)res;

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    s_next_channel = 0;
    memset(s_channel_waves, 0, sizeof(s_channel_waves));
    s_audio_ready = 1;
    return 0;
}

void audio_init(void)
{
    if (s_prototype_ready) return;
    int32_t result = audio_init_native();
    if (result < 0) {
        printf("[crash3ds] ndspInit failed (0x%08lX) - audio disabled\n", (unsigned long)(uint32_t)result);
        return;
    }
    s_prototype_ready = 1;

    /* 1. SFX_JUMP: upward frequency sweep (0.18s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.18f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            float phase = 0.0f;
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float freq = 200.0f + 900.0f * (t / 0.18f);
                phase += 6.28318531f * freq / (float)SAMPLERATE;
                float env = 1.0f - (t / 0.18f);
                b[i] = (int16_t)(sinf(phase) * env * 16000.0f);
            }
            s_clips[SFX_JUMP].data = b;
            s_clips[SFX_JUMP].sample_count = count;
        }
    }

    /* 2. SFX_SPIN: whoosh swirl (0.22s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.22f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            float phase = 0.0f;
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float freq = 140.0f + 160.0f * sinf(t * 30.0f);
                phase += 6.28318531f * freq / (float)SAMPLERATE;
                float noise = ((float)(rand() % 100) / 100.0f) * 0.4f;
                float env = sinf(t / 0.22f * 3.14159f);
                b[i] = (int16_t)((sinf(phase) * 0.6f + noise) * env * 18000.0f);
            }
            s_clips[SFX_SPIN].data = b;
            s_clips[SFX_SPIN].sample_count = count;
        }
    }

    /* 3. SFX_CRATE_BREAK: punchy splinter snap (0.25s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.25f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float env = expf(-t * 22.0f);
                float noise = ((float)(rand() % 2000) - 1000.0f) / 1000.0f;
                float thud = sinf(t * 6.28318531f * 90.0f) * expf(-t * 15.0f);
                b[i] = (int16_t)((noise * 0.7f + thud * 0.7f) * env * 24000.0f);
            }
            s_clips[SFX_CRATE_BREAK].data = b;
            s_clips[SFX_CRATE_BREAK].sample_count = count;
        }
    }

    /* 4. SFX_WUMPA: bright chime (0.16s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.16f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float env = 1.0f - (t / 0.16f);
                float tone1 = sinf(t * 6.28318531f * 987.77f); /* B5 */
                float tone2 = sinf(t * 6.28318531f * 1318.51f); /* E6 */
                b[i] = (int16_t)((tone1 * 0.5f + tone2 * 0.5f) * env * 16000.0f);
            }
            s_clips[SFX_WUMPA].data = b;
            s_clips[SFX_WUMPA].sample_count = count;
        }
    }

    /* 5. SFX_TNT_TICK: short sharp warning beep (0.07s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.07f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float env = 1.0f - (t / 0.07f);
                float tone = (sinf(t * 6.28318531f * 1400.0f) > 0.0f) ? 1.0f : -1.0f;
                b[i] = (int16_t)(tone * env * 14000.0f);
            }
            s_clips[SFX_TNT_TICK].data = b;
            s_clips[SFX_TNT_TICK].sample_count = count;
        }
    }

    /* 6. SFX_EXPLODE: deep boom rumble (0.45s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.45f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float env = expf(-t * 7.0f);
                float noise = ((float)(rand() % 2000) - 1000.0f) / 1000.0f;
                float rumble = sinf(t * 6.28318531f * 55.0f);
                b[i] = (int16_t)((noise * 0.6f + rumble * 0.6f) * env * 26000.0f);
            }
            s_clips[SFX_EXPLODE].data = b;
            s_clips[SFX_EXPLODE].sample_count = count;
        }
    }

    /* 7. SFX_MENU_SELECT: UI click chime (0.10s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.10f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float env = 1.0f - (t / 0.10f);
                float tone = sinf(t * 6.28318531f * 740.0f);
                b[i] = (int16_t)(tone * env * 14000.0f);
            }
            s_clips[SFX_MENU_SELECT].data = b;
            s_clips[SFX_MENU_SELECT].sample_count = count;
        }
    }

    /* 8. SFX_CRYSTAL: sparkling mystical chime arpeggio (0.35s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.35f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float env = expf(-t * 6.0f);
                float note1 = sinf(t * 6.28318531f * 1046.50f); /* C6 */
                float note2 = sinf(t * 6.28318531f * 1318.51f); /* E6 */
                float note3 = sinf(t * 6.28318531f * 1567.98f); /* G6 */
                float note4 = sinf(t * 6.28318531f * 2093.00f); /* C7 */
                float sparkle = sinf(t * 6.28318531f * 3135.96f) * 0.25f;
                b[i] = (int16_t)((note1 * 0.3f + note2 * 0.3f + note3 * 0.25f + note4 * 0.25f + sparkle) * env * 22000.0f);
            }
            s_clips[SFX_CRYSTAL].data = b;
            s_clips[SFX_CRYSTAL].sample_count = count;
        }
    }

    /* 9. SFX_AKU_AKU: resonant tribal mask incantation (0.28s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.28f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float env = expf(-t * 8.0f);
                float f0 = 180.0f + 60.0f * sinf(t * 40.0f);
                float wood = sinf(t * 6.28318531f * f0) * 0.7f + sinf(t * 6.28318531f * f0 * 2.5f) * 0.3f;
                b[i] = (int16_t)(wood * env * 24000.0f);
            }
            s_clips[SFX_AKU_AKU].data = b;
            s_clips[SFX_AKU_AKU].sample_count = count;
        }
    }

    /* 10. SFX_BAZOOKA: pneumatic compressed air pop (0.16s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.16f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float env = expf(-t * 18.0f);
                float sweep = sinf(t * 6.28318531f * (480.0f - 300.0f * (t / 0.16f)));
                float puff = ((float)(rand() % 1000) - 500.0f) / 500.0f;
                b[i] = (int16_t)((sweep * 0.6f + puff * 0.4f) * env * 22000.0f);
            }
            s_clips[SFX_BAZOOKA].data = b;
            s_clips[SFX_BAZOOKA].sample_count = count;
        }
    }

    /* 11. SFX_BOSS_HIT: heavy clanging metallic impact (0.35s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.35f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float env = expf(-t * 9.0f);
                float metal = sinf(t * 6.28318531f * 320.0f) * 0.5f + sinf(t * 6.28318531f * 840.0f) * 0.3f;
                float sub = sinf(t * 6.28318531f * 75.0f) * 0.5f;
                b[i] = (int16_t)((metal + sub) * env * 26000.0f);
            }
            s_clips[SFX_BOSS_HIT].data = b;
            s_clips[SFX_BOSS_HIT].sample_count = count;
        }
    }

    /* 12. SFX_LEVEL_CLEAR: triumphant fanfare flourish (0.50s) */
    {
        uint32_t count = (uint32_t)(SAMPLERATE * 0.50f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float env = (t < 0.35f) ? 1.0f : expf(-(t - 0.35f) * 12.0f);
                float freq = (t < 0.12f) ? 523.25f : ((t < 0.24f) ? 659.25f : 783.99f); /* C5 -> E5 -> G5 */
                float horn = sinf(t * 6.28318531f * freq) * 0.7f + sinf(t * 6.28318531f * freq * 2.0f) * 0.3f;
                b[i] = (int16_t)(horn * env * 22000.0f);
            }
            s_clips[SFX_LEVEL_CLEAR].data = b;
            s_clips[SFX_LEVEL_CLEAR].sample_count = count;
        }
    }

    /* -------------------------------------------------------------- */
    /* Background Music Generation (Seamless 2.5s loops for DSP)       */
    /* -------------------------------------------------------------- */
    static const float theme_bases[BGM_COUNT] = {
        0.0f, 220.0f, 220.0f, 196.0f, 164.8f, 261.6f, 146.8f, 130.8f, 110.0f
    };

    for (int t_idx = 1; t_idx < BGM_COUNT; t_idx++) {
        uint32_t count = (uint32_t)(SAMPLERATE * 2.4f);
        int16_t *b = alloc_and_generate(count);
        if (b) {
            float base_f = theme_bases[t_idx];
            for (uint32_t i = 0; i < count; i++) {
                float t = (float)i / (float)SAMPLERATE;
                float beat = (float)((int)(t * 4.0f) % 4);

                /* Bassline */
                float bass_f = base_f * 0.5f * (1.0f + 0.25f * beat);
                float bass = sinf(t * 6.28318531f * bass_f) * 0.45f;

                /* Thematic lead arpeggio */
                float lead_f = base_f;
                if (t_idx == BGM_TITLE || t_idx == BGM_WARP_ROOM) {
                    /* Tropical marimba scale */
                    float notes[] = { 1.0f, 1.25f, 1.5f, 1.75f };
                    lead_f = base_f * notes[(int)(t * 8.0f) % 4];
                } else if (t_idx == BGM_MEDIEVAL) {
                    /* Medieval Dorian flute */
                    float notes[] = { 1.0f, 1.125f, 1.25f, 1.5f };
                    lead_f = base_f * notes[(int)(t * 6.0f) % 4];
                } else if (t_idx == BGM_ARABIAN) {
                    /* Double harmonic minor sitar */
                    float notes[] = { 1.0f, 1.06f, 1.33f, 1.5f };
                    lead_f = base_f * notes[(int)(t * 8.0f) % 4];
                } else if (t_idx == BGM_ORIENTAL) {
                    /* Pentatonic gong */
                    float notes[] = { 1.0f, 1.125f, 1.33f, 1.5f };
                    lead_f = base_f * notes[(int)(t * 6.0f) % 4];
                } else if (t_idx == BGM_EGYPTIAN) {
                    /* Phrygian tomb pulse */
                    float notes[] = { 1.0f, 1.066f, 1.25f, 1.414f };
                    lead_f = base_f * notes[(int)(t * 8.0f) % 4];
                } else if (t_idx == BGM_FUTURE) {
                    /* Synthwave arpeggio */
                    float notes[] = { 1.0f, 1.33f, 1.5f, 2.0f };
                    lead_f = base_f * notes[(int)(t * 12.0f) % 4];
                } else if (t_idx == BGM_BOSS) {
                    /* Heavy boss combat march */
                    float notes[] = { 1.0f, 1.059f, 1.189f, 1.414f };
                    lead_f = base_f * notes[(int)(t * 8.0f) % 4];
                }

                float lead = sinf(t * 6.28318531f * lead_f) * 0.35f;

                /* Rhythm percussive click on quarter beats */
                float beat_sub = fmodf(t * 2.0f, 1.0f);
                float snare = ((float)(rand() % 100) / 100.0f - 0.5f) * expf(-beat_sub * 16.0f) * 0.25f;

                float mix = (bass + lead + snare) * 11000.0f;
                b[i] = (int16_t)mix;
            }
            s_bgm_clips[t_idx].data = b;
            s_bgm_clips[t_idx].sample_count = count;
        }
    }

    int loaded = 0;
    for (int i = 0; i < SFX_COUNT; i++) {
        if (s_clips[i].data) {
            DSP_FlushDataCache(s_clips[i].data,
                               s_clips[i].sample_count * sizeof(int16_t));
            loaded++;
        }
    }
    for (int i = 0; i < BGM_COUNT; i++) {
        if (s_bgm_clips[i].data) {
            DSP_FlushDataCache(s_bgm_clips[i].data,
                               s_bgm_clips[i].sample_count * sizeof(int16_t));
        }
    }
    printf("[crash3ds] ndsp audio initialized with %d/%d SFX and %d BGM loops\n",
           loaded, SFX_COUNT, BGM_COUNT - 1);
}

void audio_exit(void)
{
    if (!s_audio_ready)
        return;

    audio_stop_bgm();

    for (int i = 0; i < MAX_CHANNELS; i++) {
        ndspChnWaveBufClear(i);
        ndspChnReset(i);
    }

    /* Stop/join DSP service processing before releasing sample memory. */
    ndspExit();
    s_audio_ready = 0;
    s_prototype_ready = 0;

    for (int i = 0; i < SFX_COUNT; i++) {
        if (s_clips[i].data) {
            linearFree(s_clips[i].data);
            s_clips[i].data = NULL;
        }
        s_clips[i].sample_count = 0;
    }

    for (int i = 0; i < BGM_COUNT; i++) {
        if (s_bgm_clips[i].data) {
            linearFree(s_bgm_clips[i].data);
            s_bgm_clips[i].data = NULL;
        }
        s_bgm_clips[i].sample_count = 0;
    }

    memset(s_channel_waves, 0, sizeof(s_channel_waves));
    s_next_channel = 0;
}

void audio_play_bgm(BgmTheme theme)
{
    if (!s_audio_ready || theme <= BGM_NONE || theme >= BGM_COUNT)
        return;

    if (theme == s_current_bgm)
        return; /* Already playing */

    sfx_clip *clip = &s_bgm_clips[theme];
    if (!clip->data)
        return;

    ndspChnWaveBufClear(BGM_CHANNEL);
    ndspChnReset(BGM_CHANNEL);
    ndspChnSetFormat(BGM_CHANNEL, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetRate(BGM_CHANNEL, SAMPLERATE);

    float mix[12] = { 0.55f, 0.55f }; /* Stereo mix */
    ndspChnSetMix(BGM_CHANNEL, mix);

    memset(&s_bgm_wave, 0, sizeof(s_bgm_wave));
    s_bgm_wave.data_vaddr = clip->data;
    s_bgm_wave.nsamples = clip->sample_count;
    s_bgm_wave.status = NDSP_WBUF_FREE;
    s_bgm_wave.looping = true; /* Endless DSP hardware loop */

    ndspChnWaveBufAdd(BGM_CHANNEL, &s_bgm_wave);
    s_current_bgm = theme;
}

void audio_stop_bgm(void)
{
    if (!s_audio_ready)
        return;

    ndspChnWaveBufClear(BGM_CHANNEL);
    ndspChnReset(BGM_CHANNEL);
    s_current_bgm = BGM_NONE;
}

void audio_play_sfx(SfxId id)
{
    if (!s_audio_ready || id < 0 || id >= SFX_COUNT)
        return;

    sfx_clip *clip = &s_clips[id];
    if (!clip->data)
        return;

    int ch = s_next_channel;
    s_next_channel = (s_next_channel + 1) % MAX_CHANNELS;

    ndspChnWaveBufClear(ch);
    ndspChnReset(ch);
    ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetRate(ch, SAMPLERATE);
    float mix[12] = { 0.7f, 0.7f };
    ndspChnSetMix(ch, mix);

    ndspWaveBuf *wave = &s_channel_waves[ch];
    memset(wave, 0, sizeof(*wave));
    wave->data_vaddr = clip->data;
    wave->nsamples = clip->sample_count;
    wave->status = NDSP_WBUF_FREE;
    wave->looping = false;

    ndspChnWaveBufAdd(ch, wave);
}
