#ifndef GOOL_NATIVE_SHADING_H
#define GOOL_NATIVE_SHADING_H
#include "gool_domain.h"
enum {GOOL_NATIVE_SHADING_INVALID=-169};
typedef struct {
    uint32_t light_matrix[5],far_color[3],rgba,packet_flags,attribute_scratch;
    int backface;
} gool_native_shading_controls;
typedef struct {int32_t mac[3];int16_t ir[3];uint32_t rgba,flags;} gool_native_shading_color;
typedef struct {
    int32_t normal_ir[3];
    uint32_t rgb,coefficient,normal_flags,highlight;
    gool_native_shading_color depth_cued;
} gool_native_shading_result;
/* Exact128-byte SCUS_942.44 highlight table at8005D268, readonly, static.
 * Caller owns returned mapping lease and releases it after shader consumers.
 */
int gool_native_shading_map(gool_domain *domain,gool_domain_lease *lease);
/* DPCS(sf1,lm0) arithmetic from actual RGBC/FC/IR0 inputs. Raw IR0 write is
 * sign-extended16, not clamped. Preserve intermediate signed IR saturation,
 * command flags, final MAC/IR and byte-saturated RGB with original CODE.
 * No FIFO or timing effects. Independent PSX-SPX formula implementation. */
int gool_native_shading_depthcue(uint32_t rgba,const uint32_t far_color[3],uint16_t coefficient,
    gool_native_shading_color *output);
/* Concrete native3DAF4..3DC04 /3E920 path for normal modes3/4, using the first
 * triangle normal exactly as the original flat-color primitive. Decode s8*32,
 * MVMVA(light,IR,zero,sf1,lm0), native wrapping intensity/fog subtraction,
 * DPCS and original table highlight/backface attenuation. Backface is the
 * actual caller's NCLIP/packet XOR sign decision; no light/camera is guessed.
 * Outputs normal-command and final color-command flags separately.
 * Output unchanged on failure; static table mapping must exist if accessed.
 */
int gool_native_shading_normal(gool_domain *domain,const gool_native_shading_controls *controls,
    uint32_t normal,gool_native_shading_result *output);
#endif
