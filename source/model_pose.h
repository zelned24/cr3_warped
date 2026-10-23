#ifndef MODEL_POSE_H
#define MODEL_POSE_H
#include "model_vertices.h"

enum {MODEL_POSE_DONE=0,MODEL_POSE_PENDING=1,MODEL_POSE_INVALID=-163};
/* Original vertex-cache pair at scratch140/144: packed signed16 X/Y and
 * a full wrapping Z word. GTE consumes low signed16 of Z; retain upper bits. */
typedef struct {uint32_t xy,z;} model_pose_vertex;
typedef struct {
    const model_vertex_bytes *first,*second;
    model_pose_vertex *output;
    uint32_t count,decoded,origin_xy,origin_z;
    model_resources *bank;
    model_resource_lease geometry_identity;
    uint32_t geometry;
    int active,opened,status,paired;
} model_pose;
/* Concrete SCUS_942.44 paired path3F6D4..3F728 +3E30C..3E344:average signed
 * X/Y origins with arithmetic shift1, average wrapping full+4 origin words,
 * sum unsigned byte coordinates, shift4, add packed origins and maskFFFEFFFF.
 * No arbitrary interpolation fraction, float rounding or chosen frame index.
 * Both decoders must be DONE for the same TGEO generation and count. Their
 * independent output buffers remain immutable until DONE/close; metadata is
 * captured synchronously, so decoder owners/page leases may close afterward.
 * Stable initially{0} owner; caller-owned output never aliases input/owners.
 * This is the paired command's Q5 vertex input, before native object/camera
 * matrix, GTE saturation/projection, lighting, fog or PICA conversion.
 */
int model_pose_pair_begin(model_pose *owner,const model_vertices *first,
    const model_vertices *second,model_pose_vertex *output,uint32_t capacity);
/* Original single-frame3F6AC/3D26C path:unmodified packed origins, byte
 * coordinates shifted5, mask1FE01FE0 followed by native XY maskFFFEFFFF.
 * Same source/output lifetime and capacity rules as paired preparation. */
int model_pose_single_begin(model_pose *owner,const model_vertices *frame,
    model_pose_vertex *output,uint32_t capacity);
int model_pose_step(model_pose *owner,uint32_t vertex_budget);
int model_pose_close(model_pose *owner);
/* Decode exact signed16 GTE input words into Q5 integer components. */
int model_pose_components(const model_pose_vertex *vertex,int32_t output[3]);
#endif
