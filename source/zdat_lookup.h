#ifndef ZDAT_LOOKUP_H
#define ZDAT_LOOKUP_H
#include "zdat_resources.h"
/* Original31868/317C0 half-open zone box, with wrapping origin/extent sums
 * and shifts by8 before signed comparisons. Reads only required components;
 * contains output remains unchanged on failure. Entry and point are mapped
 * native addresses, not host pointers or approximated world coordinates. */
int zdat_lookup_contains(gool_domain *domain,uint32_t entry,uint32_t point,int *contains);
/* Original28D8C:reverse flagged adjacency order, then actual80065D5C fallback
 * if different. All searched neighbors must already be READY in the zone bank;
 * explicit EID/address acquisition retains each candidate during box reads.
 * No synchronous SD read, guessed first zone, or fabricated successful handle.
 * Result is a retained entry or native -23 (no match). On match transfer lease
 * into the destination's ownership before releasing this temporary borrow.
 * result/lease publish only on success; caller initializes lease to{0}.
 * neighbor_limit is a port bound; bank/domain/timeline remain stable.
 */
int zdat_lookup_find(zdat_resources *bank,uint32_t initial,uint32_t point,
    uint32_t neighbor_limit,int32_t *result,zdat_resource_lease *lease);
#endif
