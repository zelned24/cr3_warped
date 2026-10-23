/* Crash Bandicoot: Warped (PS1) -> 3DS port - level blob on the SD card.
 *
 * The default build embeds the 16 MB "levels.bin" blob into the .3dsx
 * (source/levels_bin.s).  On an old 3DS that is a large slice of the
 * application memory, so this module offers an alternative: the very same
 * blob is read from a file on the SD card at start-up and used instead of
 * (or in preference to) the embedded copy.
 *
 * The blob format is *not* changed in any way, it is exactly the file
 * produced by tools/export_levels.py:
 *
 *   'CBWB' u32 magic, u32 version, u32 count, u32 reserved
 *   count * { u32 id, u32 offset, u32 size }
 *   ... level data, each level starting with the 'CBW3' magic ...
 *
 * Before acceptance the reader checks version, unique IDs, directory and
 * non-overlapping level extents, internal IDs, model record extents and palette
 * size. Face indices and gameplay semantics remain the level loader's concern.
 * This API retains the whole converted blob; it does not stream NSF pages.
 */
#ifndef LEVEL_SD_H
#define LEVEL_SD_H

#include <stdint.h>

/* Default location of the blob on the SD card. */
#define LEVEL_SD_DEFAULT_PATH "sdmc:/3ds/crashwarped3ds/levels.bin"

/* Load the level blob from "path" ("sdmc:/..." on the 3DS).
 * Returns 0 on success, -1 if the file is missing, unreadable or invalid.
 * On failure the previously loaded blob (if any) is kept untouched. */
int  level_sd_open(const char *path);

/* Free the buffer allocated by level_sd_open(). */
void level_sd_close(void);

/* 1 when a valid blob is loaded from the SD card, 0 otherwise. */
int  level_sd_available(void);

/* The loaded blob and its size in bytes.
 * level_sd_blob() returns NULL when nothing is loaded. */
const uint8_t *level_sd_blob(void);
uint32_t level_sd_blob_size(void);

/* Number of levels announced by the blob header (0 when not loaded). */
int  level_sd_count(void);

#endif /* LEVEL_SD_H */
