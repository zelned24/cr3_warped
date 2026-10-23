#ifndef NSF_STREAM_H
#define NSF_STREAM_H
#include "nsf_page.h"

#define NSF_STREAM_SLOTS 4u
#define NSF_STREAM_ENCODED_LIMIT (2u*NSF_PAGE_BYTES)
typedef struct {uint64_t generation;uint32_t slot;} nsf_stream_ticket;
typedef enum {
    NSF_STREAM_QUEUED,NSF_STREAM_READING,NSF_STREAM_READY,
    NSF_STREAM_FAILED,NSF_STREAM_CANCELLED
} nsf_stream_state;
enum {NSF_STREAM_OK=0,NSF_STREAM_INVALID=-1,NSF_STREAM_FULL=-2,
      NSF_STREAM_BUSY=-3,NSF_STREAM_IO=-4,NSF_STREAM_FORMAT=-5,
      NSF_STREAM_CACHE=-6};

/* One stream session, four fixed linear pages, one bounded input staging area,
 * one libctru worker with an 8KiB stack. libctru owns its one thread allocation
 * and TLS/reentrancy setup. Every file operation occurs on that worker.
 * Open and final close/free exclude all concurrent API calls. While an open
 * session remains allocated, request/poll/acquire/release/cancel are protected
 * by the session lock. Services requiring GSP must already be initialized.
 * open starts the worker; missing/unreadable files fail requests asynchronously.
 * Offsets/encoded lengths are explicit file spans, never page_index*64KiB for
 * compressed pages. Successful decoding must consume exactly the requested span.
 * No complete NSF blob, per-request allocation or speculative page index exists. */
int nsf_stream_open(const char *path);
int nsf_stream_request(uint64_t offset,uint32_t encoded_size,nsf_stream_ticket *ticket);
int nsf_stream_poll(nsf_stream_ticket ticket,nsf_stream_state *state,int *error);
/* READY acquisition adds one reference and returns an immutable borrowed page.
 * The worker flushes CPU cache before READY publication. Keep that reference
 * until GPU/DSP DMA has finished. Cancellation never revokes an existing borrow.
 * Every request owns one initial reference; release that plus each acquisition.
 * Released-to-zero tickets cannot be reused; generations survive session close. */
int nsf_stream_acquire(nsf_stream_ticket ticket,const uint8_t **page);
int nsf_stream_release(nsf_stream_ticket ticket);
int nsf_stream_cancel(nsf_stream_ticket ticket);
/* Cancels pending work and joins worker. Returns BUSY while any ticket/borrow
 * references remain; storage stays valid and release/poll remain available.
 * Release all references and call close again to free the fixed linear pool.
 * Never frees a live DMA borrow or terminates a thread during file I/O. */
int nsf_stream_close(void);
#endif
