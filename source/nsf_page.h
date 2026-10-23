#ifndef NSF_PAGE_H
#define NSF_PAGE_H
#include <stddef.h>
#include <stdint.h>

#define NSF_PAGE_BYTES 65536u
enum {NSF_PAGE_OK=0,NSF_PAGE_INVALID=-1};

/* Decode one raw0x1234 or compressed0x1235 page from a bounded source view.
 * source_size may include following pages; consumed receives this page's
 * exact physical byte count, including compressed-header skip and raw tail.
 * Destination needs NSF_PAGE_BYTES bytes. Buffers and consumed storage must
 * not overlap. On rejection, destination and consumed remain unchanged.
 * The validation pass uses no output buffer or heap; decoding uses only the
 * caller's single page buffer. The caller must keep source immutable across
 * both passes. Page checksums, entries and page types are not interpreted. */
int nsf_page_decode(const void *source,size_t source_size,
                    void *destination,size_t destination_size,size_t *consumed);
#endif
