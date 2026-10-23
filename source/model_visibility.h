#ifndef MODEL_VISIBILITY_H
#define MODEL_VISIBILITY_H
#include "model_project.h"
typedef struct {
    uint32_t area,area_flags,depth_mac,depth_flags,screen_reject;
    uint16_t order_depth;
    int admitted,area_written,depth_written,backface;
} model_visibility;
/* Original3CD38..3CDA4 triangle admission. x/y and depths are the actual SXY
 * and SZ1..3 values, in native command order BEFORE the backend winding swap.
 * Preserve one-sided bit28, packet/command-bit XOR sign, exact packed512x217
 * screen reject and AVSZ3 MAC0 before the screen branch. The original stores
 * raw AVSZ3 MAC0 to scratch120 only when admitted, not saturated OTZ.
 * zsf3 is the actual signed GTE control value; no OT size/default is chosen.
 * This is original native screen/backface policy, not O3DS near-plane clipping
 * or final ordering-table insertion. Flags are separate command results;
 * unwritten command results are marked explicitly. Output unchanged on error.
 */
int model_visibility_native(uint32_t command,uint32_t packet_flags,const int16_t x[3],
    const int16_t y[3],const uint16_t depths[3],int16_t zsf3,model_visibility *output);
#endif
