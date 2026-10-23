/* Host-only link stub for the libctru NSF worker.
 *
 * The GOOL/VM native regressions exercise memory, state and dispatch
 * semantics; they do not perform SD-card I/O.  This stub keeps the real
 * nsf_resource production code linkable under the host compiler by reporting
 * an explicit I/O failure for every stream operation instead of fabricating
 * page contents.  Console builds never use this file: the Makefile only
 * compiles translation units from the source directory.
 */
#include "nsf_stream.h"
#include <string.h>

int nsf_stream_open(const char *path)
{
    (void)path;
    return NSF_STREAM_IO;
}

int nsf_stream_request(uint64_t offset, uint32_t encoded_size,
                       nsf_stream_ticket *ticket)
{
    (void)offset;
    (void)encoded_size;
    if (ticket) memset(ticket, 0, sizeof(*ticket));
    return NSF_STREAM_IO;
}

int nsf_stream_poll(nsf_stream_ticket ticket, nsf_stream_state *state,
                    int *error)
{
    (void)ticket;
    if (state) *state = NSF_STREAM_FAILED;
    if (error) *error = NSF_STREAM_IO;
    return NSF_STREAM_OK;
}

int nsf_stream_acquire(nsf_stream_ticket ticket, const uint8_t **page)
{
    (void)ticket;
    if (page) *page = NULL;
    return NSF_STREAM_BUSY;
}

int nsf_stream_release(nsf_stream_ticket ticket)
{
    (void)ticket;
    return NSF_STREAM_INVALID;
}

int nsf_stream_cancel(nsf_stream_ticket ticket)
{
    (void)ticket;
    return NSF_STREAM_INVALID;
}

int nsf_stream_close(void)
{
    return NSF_STREAM_OK;
}
