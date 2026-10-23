/* Crash Bandicoot: Warped (PS1) -> 3DS port - level blob on the SD card.
 *
 * See level_sd.h for the description of the API and of the blob format.
 * The file that is read here is byte for byte the same file that the
 * default build embeds (game_data/levels.bin), so the level data, the world
 * scale and the physics are exactly the same in both variants.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "level_sd.h"

#define BLOB_MAGIC        "CBWB"   /* magic of the whole blob          */
#define LEVEL_MAGIC       "CBW3"   /* magic of a single level          */
#define BLOB_HEADER_SIZE  16       /* magic + version + count + rsvd   */
#define BLOB_ENTRY_SIZE   12       /* id + offset + size               */
#define MAX_LEVELS        64       /* same limit as the embedded blob  */

/* Upper limit for the allocation.  The real file is ~16 MB; this cap only
 * exists so that a corrupted (or wrong) file cannot make us allocate an
 * absurd amount of memory on a 3DS with 64 MB of application memory. */
#define MAX_BLOB_SIZE     (32u * 1024u * 1024u)

static uint8_t *s_buf;        /* the blob, malloc'ed                 */
static uint32_t s_size;       /* its size in bytes                   */
static uint32_t s_count;      /* number of levels in the header     */
static int      s_ready;      /* 1 when a valid blob is loaded      */

/* Little endian 32 bit read that does not care about the alignment of
 * "p" (the 3DS is little endian, like the file format). */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Match the packed geometry layout consumed by level_load. This validates
 * record extents, not the meaning of individual faces or GOOL assets. */
static int level_valid(const uint8_t *data, uint32_t size, uint32_t id)
{
    if (size < 20 || memcmp(data, LEVEL_MAGIC, 4) || rd32(data + 4) != id)
        return 0;
    uint32_t models = rd32(data + 8);
    uint32_t colors = rd32(data + 12);
    if (!models || models > 160 || !colors || colors > 65536 ||
        (uint64_t)colors * 4 + 20 > size)
        return 0;
    uint32_t palette = size - colors * 4;
    uint32_t metadata = rd32(data + 16);
    uint32_t geometry_end = metadata ? metadata : palette;
    if (geometry_end < 20 || geometry_end > palette)
        return 0;
    if (metadata) {
        if (palette - metadata != 36 + models * 4 ||
            memcmp(data + metadata, "LMD1", 4) ||
            rd32(data + metadata + 4) > 1 ||
            rd32(data + metadata + 32) != models)
            return 0;
        for (uint32_t i = 0; i < models; i++)
            if (rd32(data + metadata + 36 + i * 4) > 1)
                return 0;
    }
    uint32_t offset = 20;
    for (uint32_t i = 0; i < models; i++) {
        if (offset > geometry_end || geometry_end - offset < 24)
            return 0;
        const uint8_t *model = data + offset;
        uint32_t vertices = rd32(model + 12);
        uint64_t bytes = 24ull + (uint64_t)vertices * 8 +
                         (uint64_t)rd32(model + 16) * 8 +
                         (uint64_t)rd32(model + 20) * 10;
        if (vertices > 65536 || bytes > geometry_end - offset)
            return 0;
        offset += (uint32_t)bytes;
    }
    return offset == geometry_end;
}

/* Validate the blob: magic, level count and every directory entry.
 * Returns 1 when the blob is usable, 0 otherwise.  Nothing is read outside
 * of [buf, buf + size). */
static int blob_valid(const uint8_t *buf, uint32_t size)
{
    if (!buf || size < BLOB_HEADER_SIZE)
        return 0;
    if (memcmp(buf, BLOB_MAGIC, 4) != 0 || rd32(buf + 4) != 1)
        return 0;

    uint32_t count = rd32(buf + 8);
    if (count == 0 || count > MAX_LEVELS)
        return 0;
    /* the directory itself must fit into the file */
    uint32_t directory_end = BLOB_HEADER_SIZE + count * BLOB_ENTRY_SIZE;
    if (directory_end > size)
        return 0;

    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = buf + BLOB_HEADER_SIZE + (size_t)i * BLOB_ENTRY_SIZE;
        uint32_t id = rd32(e);
        uint32_t off = rd32(e + 4);
        uint32_t sz  = rd32(e + 8);
        /* the level data must live inside the blob */
        if (id >= MAX_LEVELS || off < directory_end || sz < 20)
            return 0;
        if ((uint64_t)off + (uint64_t)sz > (uint64_t)size)
            return 0;
        /* and it must really be a level */
        if (!level_valid(buf + off, sz, id))
            return 0;
        for (uint32_t j = 0; j < i; j++) {
            const uint8_t *previous = buf + BLOB_HEADER_SIZE + j * BLOB_ENTRY_SIZE;
            uint32_t previous_off = rd32(previous + 4);
            uint32_t previous_size = rd32(previous + 8);
            if (id == rd32(previous) ||
                ((uint64_t)off < (uint64_t)previous_off + previous_size &&
                 (uint64_t)previous_off < (uint64_t)off + sz))
                return 0;
        }
    }
    return 1;
}

int level_sd_open(const char *path)
{
    if (!path || !*path)
        return -1;

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long len = ftell(f);
    if (len < BLOB_HEADER_SIZE || (unsigned long)len > (unsigned long)MAX_BLOB_SIZE) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return -1;
    }
    /* read in chunks: the 3DS file system may not accept a single huge
     * transfer, and this keeps the progress bounded */
    size_t got = 0;
    while (got < (size_t)len) {
        size_t want = (size_t)len - got;
        if (want > (64u * 1024u))
            want = 64u * 1024u;
        size_t n = fread(buf + got, 1, want, f);
        if (n == 0)
            break;
        got += n;
    }
    int read_failed = ferror(f);
    int close_failed = fclose(f) != 0;

    if (got != (size_t)len || read_failed || close_failed ||
        !blob_valid(buf, (uint32_t)len)) {
        free(buf);
        return -1;
    }

    /* the new blob is valid: it replaces the previous one */
    level_sd_close();
    s_buf   = buf;
    s_size  = (uint32_t)len;
    s_count = rd32(buf + 8);
    s_ready = 1;
    return 0;
}

void level_sd_close(void)
{
    if (s_buf)
        free(s_buf);
    s_buf   = NULL;
    s_size  = 0;
    s_count = 0;
    s_ready = 0;
}

int level_sd_available(void)
{
    return (s_ready && s_buf) ? 1 : 0;
}

const uint8_t *level_sd_blob(void)
{
    return s_buf;
}

uint32_t level_sd_blob_size(void)
{
    return s_size;
}

int level_sd_count(void)
{
    return (int)s_count;
}
