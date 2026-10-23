/* Mock citro3d.h for host-based E2E test execution */
#ifndef MOCK_CITRO3D_H
#define MOCK_CITRO3D_H

#include <stdint.h>
#include <stdbool.h>

#define GPU_TRIANGLES 0
#define GPU_FLOAT 0
#define GPU_PRIMARY_COLOR 0
#define GPU_REPLACE 0

typedef struct {
    float r[4][4];
} C3D_Mtx;

typedef struct {
    int width, height;
} C3D_RenderTarget;

typedef struct {
    void *DVLE;
} DVLB_s;

typedef struct {
    void *vertexShader;
} shaderProgram_s;

typedef struct {
    int dummy;
} C3D_AttrInfo;

typedef struct {
    int dummy;
} C3D_TexEnv;

static inline void C3D_DrawArrays(int primitive, int first, int count) {
    (void)primitive; (void)first; (void)count;
}

static inline void Mtx_OrthoTilt(C3D_Mtx *mtx, float left, float right, float bottom, float top, float near, float far, bool isLeftHanded) {
    (void)mtx; (void)left; (void)right; (void)bottom; (void)top; (void)near; (void)far; (void)isLeftHanded;
}

static inline void shaderProgramInit(shaderProgram_s *sp) { (void)sp; }
static inline void shaderProgramSetVsh(shaderProgram_s *sp, void *dvle) { (void)sp; (void)dvle; }
static inline void C3D_BindProgram(shaderProgram_s *sp) { (void)sp; }
static inline int shaderInstanceGetUniformLocation(void *vsh, const char *name) { (void)vsh; (void)name; return 0; }
static inline C3D_AttrInfo *C3D_GetAttrInfo(void) { static C3D_AttrInfo a; return &a; }
static inline void AttrInfo_Init(C3D_AttrInfo *a) { (void)a; }
static inline void AttrInfo_AddLoader(C3D_AttrInfo *a, int reg, int type, int count) { (void)a; (void)reg; (void)type; (void)count; }
static inline C3D_TexEnv *C3D_GetTexEnv(int stage) { (void)stage; static C3D_TexEnv e; return &e; }
static inline void C3D_TexEnvInit(C3D_TexEnv *e) { (void)e; }
static inline void C3D_TexEnvSrc(C3D_TexEnv *e, int dummy, int src, int dummy2, int dummy3) { (void)e; (void)dummy; (void)src; (void)dummy2; (void)dummy3; }
static inline void C3D_TexEnvFunc(C3D_TexEnv *e, int dummy, int func) { (void)e; (void)dummy; (void)func; }

#endif /* MOCK_CITRO3D_H */
