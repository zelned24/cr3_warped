#ifndef GOOL_CATALOG_H
#define GOOL_CATALOG_H
#include "gool_resources.h"
typedef struct {
    gool_resource_catalog entries[1024];
    uint32_t level,count;
} gool_catalog;
/* GLC1 compact behavior index, generated from original per-level CGA ordering.
 * Validates CRC, exact length, stable ordinal IDs, unique odd EIDs and each EID
 * against the original NSD before publishing. Uses one static staging catalog;
 * owner application thread only, nonreentrant. No complete CGA or heap needed.
 * Failure preserves output. Close old bank/consumers before replacing a catalog
 * whose entries are borrowed by their source resolver. */
int gool_catalog_load(gool_catalog *out,const char *path,uint32_t level,
                        const nsd_resource_index *nsd);
#endif
