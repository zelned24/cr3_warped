#ifndef AUDIO_VOICE_H
#define AUDIO_VOICE_H
#include <stddef.h>
#include <stdint.h>

/* Native SCUS-94244 template at 0x8006CF78; no host pointers in this span. */
typedef struct { uint8_t bytes[76]; } AudioVoiceTemplate;
typedef struct {
    uint16_t volume_operand; /* resolve first; signed little-endian 16-bit load */
    uint16_t sample_operand; /* resolve second; reference pointer passed to native backend */
} AudioVoicePlay;
typedef struct {
    uint16_t source_operand;
    uint8_t target_member; /* zero selects template; 31 pops after source resolution */
    uint8_t selector; /* effective backend selector: encoded15 becomes0 + option0x80000000 */
    uint32_t options;
} AudioVoiceControl;
typedef struct {
    int16_t master_volume;
    int32_t ramp_ticks;
    uint8_t spatial_mode; /* captured 0x80033000 result; native entry points refresh it */
} AudioVoiceEnvironment;

/* Synchronous read of actual native-address engine storage. Return0 after
 * writing one little-endian word, negative on failure. No implicit globals,
 * level selection or default read values are supplied by the audio module. */
typedef int (*AudioVoiceNativeRead)(void *context, uint32_t address, uint32_t *word);
typedef int (*AudioVoiceNativeWrite)(void *context,uint32_t address,uint32_t word);
/* Original52844 SPU register snapshot and32160 bounded random service.
 * Called only when original3359C reaches the corresponding branch. */
typedef int (*AudioVoiceNativeVolume)(void *context,uint32_t channel_mask,int16_t *left,int16_t *right);
typedef int (*AudioVoiceNativeRandom)(void *context,uint32_t upper_bound,int32_t *value);
typedef int (*AudioVoiceNativeBank)(void *context,uint32_t eid,uint32_t *bank);
typedef int (*AudioVoiceNativeResource)(void *context,uint32_t reference_address,
    uint32_t mode,uint32_t create,uint32_t *resource);
enum { AUDIO_VOICE_OK = 0, AUDIO_VOICE_INVALID = -1, AUDIO_VOICE_SERVICE_REQUIRED = -2 };

int audio_voice_decode_control(uint32_t instruction, AudioVoiceControl *out);
int audio_voice_decode_play(uint32_t instruction, AudioVoicePlay *out);
/* Original80033000..8003304C and original35-entry dispatch at8001057C.
 * Read actual [80061044]+8 level identifier; output5 or6. Output unchanged
 * on failed mapping/read. The original leaf has no external calls. */
int audio_voice_spatial_mode_native(void *context, AudioVoiceNativeRead read, uint8_t *mode);
/* Original3359C: first_voice is the actual engine gp+28C value, not a port
 * channel reservation. Reads original80-byte voice rows at8006C7D8 and live
 * template priority8006CF91. Selects first free voice or steals by native
 * priority/SPU-volume/RNG rules; writes -1 on native refusal. No row mutation,
 * synthetic amplitude, implicit RNG or NDSP allocation. Error preserves result. */
int audio_voice_select_native(void *context,AudioVoiceNativeRead read,
    AudioVoiceNativeVolume volume,AudioVoiceNativeRandom random,uint32_t first_voice,
    int32_t *selected_voice);
/* Original33760 sample-reference classification. Executes actual15414 bank
 * lookup and, if absent,142B4(reference,0,1) through explicit services. Preserves
 * source rereads and template flag/+C/+14 stores, including partial effects.
 * native_result is the original return word (including native -14 refusal),
 * distinct from a provider/mapping error. No EID-to-SPU address is fabricated. */
int audio_voice_resolve_native(void *context,AudioVoiceNativeRead read,
    AudioVoiceNativeWrite write,AudioVoiceNativeBank bank,AudioVoiceNativeResource resource,
    uint32_t template_address,uint32_t sample_address,int32_t *native_result);
/* Preserve native untouched bytes/flag bits when resetting an existing template. */
int audio_voice_template_reset(AudioVoiceTemplate *state, unsigned spatial_mode);
/* Native33050 reset using actual engine level storage, with no caller-selected
 * spatial mode. Capture the environment mode only after the reset succeeds. */
int audio_voice_template_reset_native(AudioVoiceTemplate *state, AudioVoiceEnvironment *environment,
                                      void *context, AudioVoiceNativeRead read);
/* Caller resolves source into a bounded little-endian span before this call.
 * Targets other than zero, transformed position selector 2 and bank query 14
 * require engine services and leave state/environment/result unchanged.
 * No SPU/NDSP allocation, playback, object lookup or fabricated positions. */
int audio_voice_template_control(AudioVoiceTemplate *state, AudioVoiceEnvironment *environment,
                                 const AudioVoiceControl *control, const uint8_t *source,
                                 size_t source_size, int32_t *result);
/* Selector8 source1 executes the original33000 leaf at the actual command
 * point. Other selectors do not read level state. Target/member services retain
 * the existing mandatory-provider rejection; failures preserve all outputs. */
int audio_voice_template_control_native(AudioVoiceTemplate *state, AudioVoiceEnvironment *environment,
                                        const AudioVoiceControl *control, const uint8_t *source,
                                        size_t source_size, int32_t *result,
                                        void *context, AudioVoiceNativeRead read);
#endif
