#ifndef MODEL_PROJECT_H
#define MODEL_PROJECT_H
#include "model_pose.h"
#include "gool_native_render.h"

enum {MODEL_PROJECT_INVALID=-167};
typedef struct {
    uint32_t rotation[5],translation[3],offset_x,offset_y,depth_bias;
    uint16_t distance;
    int16_t depth_scale;
} model_project_controls;
typedef struct {
    int32_t camera[3];
    int16_t ir[3],x,y;
    uint16_t depth,cue;
    uint32_t quotient,mac0,flags;
    int cue_written;
} model_project_vertex;
typedef struct {
    uint16_t depth[4],cue;
    int16_t x[3],y[3];
    uint32_t mac0;
} model_project_fifo;
typedef struct {model_project_vertex vertices[3];uint32_t flags;} model_project_triangle;
typedef struct {uint32_t mac0,flags;uint16_t depth;} model_project_order_depth;
/* Integer RTPS(sf=1,lm=0) result arithmetic, the form used by the original
 * renderer. Independent implementation from PSX-SPX hardware formulas:
 * https://psx-spx.consoledev.net/geometrytransformationenginegte/
 * Explicit real controls,44-bit intermediate wrap, MAC32 truncation, IR/depth/
 * screen/cue saturation, UNR reciprocal and corresponding FLAG bits.
 * Camera MACs remain available for software near-plane policy. No discard or
 * invented viewport/aspect ratio, FIFO mutation, coprocessor timing or PICA
 * projection is supplied. Inputs/outputs are stable caller-owned values;
 * output unchanged on invalid arguments and can alias input storage.
 */
int model_project_rtps(const model_project_controls *controls,const model_pose_vertex *vertex,
    model_project_vertex *output);
/* Project an already transformed camera MAC vector, including new near-clip
 * intersections. Does not run the rotation or44-bit accumulation again; flags
 * cover only this projection's IR/depth/division/screen/cue stages. No FIFO
 * writes. This is a backend operation, not an extra native RTPS instruction. */
int model_project_camera(const model_project_controls *controls,const int32_t camera[3],
    model_project_vertex *output);
/* Explicit FIFO owner contains the actual prior SZ/SXY/cue/MAC0 values.
 * RTPT pushes all three vertices but computes depth cue only for the last,
 * and aggregates flags for the complete command. First two result vertices
 * have cue_written0; their cue/mac0 are not coprocessor writes.
 * FIFO and output must be disjoint; unchanged on failure. No timing model. */
int model_project_rtps_fifo(const model_project_controls *controls,const model_pose_vertex *vertex,
    model_project_fifo *fifo,model_project_vertex *output);
int model_project_rtpt(const model_project_controls *controls,const model_pose_vertex vertices[3],
    model_project_fifo *fifo,model_project_triangle *output);
/* Native NCLIP signed32 MAC0/overflow flags. No automatic culling policy. */
int model_project_nclip(const int16_t x[3],const int16_t y[3],uint32_t *mac0,uint32_t *flags);
/* AVSZ3/AVSZ4:actual unsigned SZ words and signed native ZSF3/4. Retains
 * wrapping MAC0, saturated OTZ and command flags; no invented OT bucket size. */
int model_project_average_depth(const uint16_t *depth,uint32_t count,int16_t scale,
    model_project_order_depth *output);
/* Capture prepared native rotation/TR words with actual GTE OFX/OFY/H/DQA/DQB.
 * These projection registers are explicit; no camera defaults are chosen. */
int model_project_bind(const gool_native_render_output *prepared,uint32_t offset_x,uint32_t offset_y,
    uint16_t distance,int16_t depth_scale,uint32_t depth_bias,model_project_controls *output);
#endif
