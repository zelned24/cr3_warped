#ifndef AUDIO_NATIVE_H
#define AUDIO_NATIVE_H
#include "audio_voice.h"
#include "gool_native_actor.h"

typedef struct audio_native_services {
    void *context;
    /* Actual35410 resolver for non-direct operands, including its literal
     * scratch policy. Direct E00..FFF and E1F are handled here using raw SP. */
    int (*address)(void *context,uint32_t node,uint32_t operand,uint32_t *address);
    /* Actual33864 or33C54 services: synchronous0 success, negative failure.
     * Sample/source arguments remain native addresses, never inferred EIDs.
     * No default sample mapping, voice allocation or INST sink is supplied. */
    int (*play)(void *context,uint32_t node,uint32_t sample,int16_t volume,int32_t *result);
    int (*control)(void *context,uint32_t target,uint32_t selector_options,
        uint32_t source,uint32_t node,int32_t *result);
    /* Optional real template/environment ownership for concrete target0
     * controls. Unsupported selectors/active voices require control above.
     * Reset uses actual33000 engine reads through registry.domain. */
    AudioVoiceTemplate *voice_template;
    AudioVoiceEnvironment *environment;
} audio_native_services;

int audio_native_reset(gool_native_registry *registry,const audio_native_services *services);
/* Original3780C/378F8, after38DD8 spills registers. Mutates raw BC and result
 * stores in native order. Errors retain prior pops/writes and must not replay. */
int audio_native_apply(gool_native_registry *registry,uint32_t node,uint32_t instruction,
    const audio_native_services *services);
#endif
