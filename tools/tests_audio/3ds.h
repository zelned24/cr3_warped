#ifndef TEST_AUDIO_3DS_H
#define TEST_AUDIO_3DS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int32_t Result;
#define R_FAILED(r) ((r) < 0)
#define NDSP_OUTPUT_STEREO 1
#define NDSP_FORMAT_MONO_PCM16 5
#define NDSP_WBUF_FREE 0
typedef struct ndspWaveBuf {
    void *data_vaddr;
    uint32_t nsamples;
    int status;
    bool looping;
    struct ndspWaveBuf *next;
} ndspWaveBuf;
void *linearAlloc(size_t size);
void linearFree(void *ptr);
Result ndspInit(void);
void ndspExit(void);
void ndspSetOutputMode(int mode);
void ndspChnReset(int ch);
void ndspChnWaveBufClear(int ch);
void ndspChnSetFormat(int ch, int format);
void ndspChnSetRate(int ch, float rate);
void ndspChnSetMix(int ch, float mix[12]);
void DSP_FlushDataCache(const void *ptr, uint32_t size);
void ndspChnWaveBufAdd(int ch, ndspWaveBuf *wave);
#endif
