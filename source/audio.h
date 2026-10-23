/* Crash Bandicoot: Warped - 3DS Hardware Audio System (ndsp).
 *
 * Implements retro sound effects (Jump, Spin, Crate Break, Wumpa Pickup,
 * TNT Countdown, Explosion, Menu Select) using the Nintendo 3DS DSP service.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>
#include <stdint.h>

enum { AUDIO_ADPCM_BLOCK_BYTES = 16, AUDIO_ADPCM_BLOCK_SAMPLES = 28 };

typedef struct {
    int16_t recent;
    int16_t older;
} AudioAdpcmHistory;

/* Decode one SPU block into caller-owned PCM16 storage without allocation.
 * History persists across consecutive blocks and explicit loop jumps. Flags
 * are returned unchanged; the caller controls end/repeat/loop addressing.
 * Invalid arguments leave output, history and flags unchanged. Returns 1 on
 * success, 0 on invalid arguments. Input/output/state must not overlap. */
int audio_decode_adpcm_block(const uint8_t *data, size_t data_size,
                             int16_t *pcm, size_t pcm_capacity,
                             AudioAdpcmHistory *history, uint8_t *flags);

typedef enum {
    SFX_JUMP = 0,
    SFX_SPIN,
    SFX_CRATE_BREAK,
    SFX_WUMPA,
    SFX_TNT_TICK,
    SFX_EXPLODE,
    SFX_MENU_SELECT,
    SFX_CRYSTAL,
    SFX_AKU_AKU,
    SFX_BAZOOKA,
    SFX_BOSS_HIT,
    SFX_LEVEL_CLEAR,
    SFX_COUNT
} SfxId;

typedef enum {
    BGM_NONE = 0,
    BGM_TITLE,
    BGM_WARP_ROOM,
    BGM_MEDIEVAL,
    BGM_ARABIAN,
    BGM_ORIENTAL,
    BGM_EGYPTIAN,
    BGM_FUTURE,
    BGM_BOSS,
    BGM_COUNT
} BgmTheme;

/* Initialize 3DS DSP audio service and audio buffers */
void audio_init(void);

/* Initialize only the shared NDSP service for original resource/sequence
 * providers. Allocates no synthetic clips and starts no BGM. Returns the
 * libctru Result unchanged; zero means ready. Use audio_exit after all native
 * stream/sequence owners have stopped, then release their linear pools. */
int32_t audio_init_native(void);

/* Free audio buffers and shutdown DSP service */
void audio_exit(void);

/* Play a sound effect */
void audio_play_sfx(SfxId id);

/* Play / loop background music stream on dedicated DSP channel */
void audio_play_bgm(BgmTheme theme);
void audio_stop_bgm(void);

#endif /* AUDIO_H */
