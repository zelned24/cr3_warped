#ifndef MODEL_CLIP_H
#define MODEL_CLIP_H
#include <stdint.h>
enum {MODEL_CLIP_INVALID=-168};
typedef struct {
    int32_t camera[3];
    int32_t attributes[5]; /* renderer RGBUV in caller-selected fixed point */
} model_clip_vertex;
typedef struct {model_clip_vertex vertices[4];uint32_t count;} model_clip_polygon;
/* O3DS backend clipping, not a claim about original PS1 clipping policy.
 * Clip one camera-space triangle against Z>=near, where near must be positive.
 * Preserve winding and interpolate all five attributes. Canonical inside-to-
 * outside intersections use unsigned64 products and integer division, rounding
 * displacement toward the inside endpoint. Shared edges give identical integer
 * intersections regardless of traversal direction. No float/heap, overflow of
 * signed32 differences or division by zero. At most four output vertices;
 * remove identical consecutive endpoint duplicates, retain degenerate lines.
 * Output unchanged on invalid input; it may overlap the input triangle.
 */
int model_clip_near(const model_clip_vertex input[3],int32_t near,model_clip_polygon *output);
#endif
