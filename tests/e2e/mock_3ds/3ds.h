/* Mock 3ds.h for host-based E2E test execution */
#ifndef MOCK_3DS_H
#define MOCK_3DS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

typedef int32_t Result;
#define R_FAILED(res) ((res) < 0)
#define R_SUCCEEDED(res) ((res) >= 0)

/* Key codes */
enum {
    KEY_A       = 1u << 0,
    KEY_B       = 1u << 1,
    KEY_SELECT  = 1u << 2,
    KEY_START   = 1u << 3,
    KEY_DRIGHT  = 1u << 4,
    KEY_DLEFT   = 1u << 5,
    KEY_DUP     = 1u << 6,
    KEY_DDOWN   = 1u << 7,
    KEY_R       = 1u << 8,
    KEY_L       = 1u << 9,
    KEY_X       = 1u << 10,
    KEY_Y       = 1u << 11,
    KEY_TOUCH   = 1u << 20
};

typedef struct {
    s16 dx;
    s16 dy;
} circlePosition;

typedef struct {
    u16 px;
    u16 py;
} touchPosition;

/* HID mock state */
static u32 s_mock_hid_held = 0;
static u32 s_mock_hid_down = 0;
static circlePosition s_mock_circle = {0, 0};
static touchPosition s_mock_touch = {0, 0};

static inline u32 hidKeysHeld(void) { return s_mock_hid_held; }
static inline u32 hidKeysDown(void) { return s_mock_hid_down; }
static inline void hidCircleRead(circlePosition *pos) { if (pos) *pos = s_mock_circle; }
static inline void hidTouchRead(touchPosition *pos) { if (pos) *pos = s_mock_touch; }

/* Linear memory mock */
static int s_mock_force_alloc_fail = 0;
static inline void *linearAlloc(size_t size) {
    if (s_mock_force_alloc_fail) return NULL;
    return malloc(size);
}
static inline void linearFree(void *ptr) {
    free(ptr);
}

/* NDSP audio mock */
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

static inline Result ndspInit(void) { return 0; }
static inline void ndspExit(void) {}
static inline void ndspSetOutputMode(int mode) { (void)mode; }
static inline void ndspChnReset(int ch) { (void)ch; }
static inline void ndspChnWaveBufClear(int ch) { (void)ch; }
static inline void ndspChnSetFormat(int ch, int fmt) { (void)ch; (void)fmt; }
static inline void ndspChnSetRate(int ch, float rate) { (void)ch; (void)rate; }
static inline void ndspChnSetMix(int ch, float mix[12]) { (void)ch; (void)mix; }
static inline void ndspChnWaveBufAdd(int ch, ndspWaveBuf *wave) { (void)ch; if (wave) wave->status = NDSP_WBUF_FREE; }
static inline void DSP_FlushDataCache(const void *ptr, uint32_t size) { (void)ptr; (void)size; }

static inline bool aptMainLoop(void) { return true; }

#endif /* MOCK_3DS_H */
