#ifndef LEVEL_RESOURCES_H
#define LEVEL_RESOURCES_H
#include "nsf_resource.h"

#define LEVEL_RESOURCES_DEFAULT_DIRECTORY "sdmc:/3ds/crashwarped3ds/original"
enum { LEVEL_RESOURCES_INVALID=-50, LEVEL_RESOURCES_IO=-51,
       LEVEL_RESOURCES_FORMAT=-52 };

/* One application-thread owner of the original NSF session and its immutable
 * NSD. Directory contains the exact packaged S%07lX.NSD/NSF names. No complete
 * archive is loaded. Stop resource consumers and release their page/ticket
 * references before replacement. BUSY close preserves the NSD and level ID;
 * release outstanding references and retry, without advancing that session.
 * The module does not activate objects or bind gameplay/audio event mappings. */
int level_resources_open(const char *directory,uint32_t level_id);
int level_resources_close(void);
/* Retained storage identity, not a claim that a BUSY-closed worker can run. */
int level_resources_level(void);
/* Borrowed validated directory; expires on successful close/replacement.
 * Retained metadata after BUSY close does not restart its stopped worker. */
int level_resources_nsd(nsd_resource_index *out);
#endif
