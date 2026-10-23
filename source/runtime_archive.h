#ifndef RUNTIME_ARCHIVE_H
#define RUNTIME_ARCHIVE_H
#include <stddef.h>
#include <stdint.h>

#define CGA_MAX_BYTES (32u * 1024u * 1024u)
#define CGA_MAX_ENTRIES 8192u
#define CGA_MAX_SECTIONS 256u

/* Initialize with {0}. Views borrow their storage; do not modify/free it
 * until all consumers are done. open owns storage until close/replacement.
 * All views and section pointers expire on successful replacement/close.
 * Serialized input is unaligned little endian. No struct casts are used. */
typedef struct {
    const uint8_t *data;
    uint8_t *owned;
    uint32_t size, level, count, nsd_offset, nsd_size;
} cga_archive;

typedef struct {
    uint32_t eid, type, section_count;
} cga_entry;

/* Rejection leaves a valid existing archive untouched. expected_level must
 * match 0..63. bind rejects rebinding storage owned by the same archive. */
int cga_bind(cga_archive *archive,const void *data,size_t size,uint32_t expected_level);
int cga_open(cga_archive *archive,const char *path,uint32_t expected_level);
void cga_close(cga_archive *archive);
int cga_entry_at(const cga_archive *archive,uint32_t index,cga_entry *out);
int cga_find(const cga_archive *archive,uint32_t eid,uint32_t *index);
int cga_section(const cga_archive *archive,uint32_t entry,uint32_t section,
                const uint8_t **data,uint32_t *size);
int cga_nsd(const cga_archive *archive,const uint8_t **data,uint32_t *size);
#endif
