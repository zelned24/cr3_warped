#ifndef AUDIO_NATIVE_ENGINE_H
#define AUDIO_NATIVE_ENGINE_H
#include "audio_native.h"

/* Original SpuVoiceAttr storage. Only mask-selected fields are valid; adapters
 * consume/copy within the synchronous call. Layout is preserved byte-for-byte. */
typedef struct {uint8_t bytes[64];} AudioNativeSpuAttributes;
typedef struct {
    gool_native_registry *registry;
    uint32_t gp; /* actual native engine GP; no default global coordinates */
    void *context;
    AudioVoiceNativeVolume volume;
    AudioVoiceNativeRandom random;
    AudioVoiceNativeBank bank;
    AudioVoiceNativeResource resource;
    int (*transform)(void *context,const uint32_t input[3],uint32_t output[3],uint32_t mode);
    int (*spatial)(void *context,uint32_t state,uint32_t position,int16_t volume,int16_t output[2]);
    int (*curve)(void *context,uint32_t table,int32_t original_volume,int16_t master,int16_t volume);
    int (*attributes)(void *context,const AudioNativeSpuAttributes *attributes);
    int (*reverb)(void *context,uint32_t enabled,uint32_t voice_mask);
    int (*key)(void *context,uint32_t enabled,uint32_t voice_mask);
} AudioNativeEngine;

/* Actual33864,33C54,334D0 compositions. Compatible with audio_native_services
 * play/control callbacks and retirement stop_audio respectively. Synchronous
 * callbacks must complete real services; absent services reject at their actual
 * call boundary. Errors preserve earlier native writes and cannot be replayed.
 * This does not manufacture an INST bank, SPU-address mapping or NDSP output. */
int audio_native_engine_play(void *context,uint32_t node,uint32_t sample,int16_t volume,int32_t *result);
int audio_native_engine_control(void *context,uint32_t target,uint32_t selector_options,
    uint32_t source,uint32_t node,int32_t *result);
int audio_native_engine_stop(void *context,uint32_t node);
#endif
