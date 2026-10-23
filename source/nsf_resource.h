#ifndef NSF_RESOURCE_H
#define NSF_RESOURCE_H
#include "nsf_stream.h"

#define NSD_RESOURCE_MAX_BYTES 65536u
enum {NSF_RESOURCE_OK=0,NSF_RESOURCE_INVALID=-20,NSF_RESOURCE_MISSING=-21,
      NSF_RESOURCE_FORMAT=-22,NSF_RESOURCE_TEXTURE=-23};
typedef struct {const uint8_t *data;uint32_t size,count;} nsd_resource_index;
typedef struct {nsf_stream_ticket page;uint32_t eid,cid;} nsf_resource_ticket;
typedef struct {
    const uint8_t *page,*entry;
    uint32_t eid,page_type,entry_type,entry_size,item_count;
    nsf_stream_ticket ticket;
    int leased;
} nsf_resource_view;

/* Borrowed immutable original NSD bytes. Reject malformed/duplicate directory
 * entries before binding. Replacement on failure leaves the old view intact.
 * The 64KiB cap is a port budget, not an inferred original format limit. */
int nsd_resource_bind(nsd_resource_index *index,const void *data,size_t size);
int nsd_resource_find(const nsd_resource_index *index,uint32_t eid,uint32_t *cid);
/* Validate a complete decoded page before exposing any entry/item. Texture
 * pages have page_type1, no fabricated entry/items, and remain available via
 * view.page. Their header+4 is texture EID, unlike the CID of other pages.
 * Initialize output views to {0}; an active lease cannot be overwritten.
 * Pure views borrow caller bytes; never pass them to resource_release_view. */
int nsf_resource_parse_page(const void *page,size_t size,uint32_t cid,uint32_t eid,
                            nsf_resource_view *view);
int nsf_resource_item(const nsf_resource_view *view,uint32_t item,
                      const uint8_t **data,uint32_t *size);

/* One shared NSF worker session. This entry explicitly targets RAW archives:
 * the supplied 42 NSFs contain raw64KiB pages, CID>>1 selects physical page.
 * Compressed archives require an explicit span index and are not silently
 * interpreted using this raw layout. Original NSD storage must outlive close.
 * All operations here belong to the owner application thread. */
int nsf_resource_open_raw(const char *path,const void *nsd,size_t nsd_size);
int nsf_resource_request(uint32_t eid,nsf_resource_ticket *ticket);
int nsf_resource_poll(nsf_resource_ticket ticket,nsf_stream_state *state,int *error);
/* Acquisition adds a page borrow. Keep the stable, noncopied view until every
 * CPU/DMA consumer finishes. The ticket's initial reference is independent;
 * release it even after successful acquisition. On failure no view is lent. */
int nsf_resource_acquire(nsf_resource_ticket ticket,nsf_resource_view *view);
int nsf_resource_release_view(nsf_resource_view *view);
int nsf_resource_release_ticket(nsf_resource_ticket ticket);
int nsf_resource_cancel(nsf_resource_ticket ticket);
/* BUSY retains index/pages while any ticket or view reference remains. Release
 * them and close again. No live source buffer is revoked from audio/GPU use. */
int nsf_resource_close(void);
#endif
