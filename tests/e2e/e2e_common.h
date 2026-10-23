/* Crash Bandicoot 3: Warped (3DS Port) — E2E Test Suite Common Infrastructure
 *
 * Provides host test doubles, memory canary validators, hardware abstractions,
 * and test harness helpers for opaque-box requirement verification.
 */
#ifndef E2E_COMMON_H
#define E2E_COMMON_H

#include <assert.h>
#ifdef NDEBUG
#error E2E tests require assertions enabled!
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <float.h>

#include <3ds.h>
#include <citro3d.h>

/* Hardware screen resolutions */
#define SCREEN_W 400
#define SCREEN_H 240
#define BOTTOM_W 320
#define BOTTOM_H 240

/* Buffer capacities from production engine */
#define TOP_UI_CAPACITY 49152
#define BOTTOM_UI_CAPACITY 24576
#define PROP_VERTEX_CAPACITY 8190

/* 64-bit Memory Canary Sentinel */
#define CANARY_MAGIC UINT64_C(0x92ABCDEF12345678)

typedef struct {
    uint64_t before;
    uint8_t payload[256 * 1024];
    uint64_t after;
} CanaryBuffer;

static inline void canary_init(CanaryBuffer *cb) {
    cb->before = CANARY_MAGIC;
    cb->after = CANARY_MAGIC;
}

static inline void canary_verify(const CanaryBuffer *cb) {
    assert(cb->before == CANARY_MAGIC && "Canary buffer head corrupted!");
    assert(cb->after == CANARY_MAGIC && "Canary buffer tail corrupted!");
}

/* Linear memory mock with tracking */
static int s_mock_alloc_count = 0;
static size_t s_mock_bytes_allocated = 0;

static inline void *mock_linearAlloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr) {
        s_mock_alloc_count++;
        s_mock_bytes_allocated += size;
    }
    return ptr;
}

static inline void mock_linearFree(void *ptr) {
    if (ptr) {
        s_mock_alloc_count--;
        free(ptr);
    }
}

/* Test lifecycle counters and logging */
static int g_test_count = 0;
static int g_test_pass_count = 0;

#define TEST_BEGIN(name) \
    do { \
        g_test_count++; \
        printf("  [RUN ] %s ... ", (name)); \
        fflush(stdout); \
    } while(0)

#define TEST_PASS(name) \
    do { \
        g_test_pass_count++; \
        printf("PASSED\n"); \
        fflush(stdout); \
    } while(0)

#endif /* E2E_COMMON_H */
