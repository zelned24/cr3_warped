#ifndef GOOL_DOMAIN_H
#define GOOL_DOMAIN_H
#include <stdint.h>
#define GOOL_DOMAIN_REGIONS 512u
enum {GOOL_DOMAIN_INVALID=-92,GOOL_DOMAIN_UNBOUND=-93,GOOL_DOMAIN_READ_ONLY=-94,
      GOOL_DOMAIN_FULL=-95,GOOL_DOMAIN_BUSY=-96};
typedef struct {uint64_t generation;uint32_t index;} gool_domain_lease;
typedef struct {
    const uint8_t *bytes;uint8_t *mutable_bytes;
    uint64_t generation;
    uint32_t address,size,references;
    gool_domain_lease parent;
    int overlay;
} gool_domain_region;
typedef struct {gool_domain_region regions[GOOL_DOMAIN_REGIONS];uint64_t generation;} gool_domain;

/* Explicit native32-bit address domain. Caller owns stable region storage;
 * original archive regions are readonly and runtime overlays are separate.
 * No pointer casts, heap allocation, implicit zero memory or fabricated globals.
 * Initialize owner to{0} before any mapping. Never reset while consumers exist.
 * Mapping returns one owner lease. Retain for every independent VM/CPU/DMA
 * consumer; release after quiescence. Final release removes the region. Token
 * generations reject stale releases. Raw native addresses intentionally follow
 * native lifetime rules: reusing an address requires all old consumers stopped.
 * Operations belong to one application thread, outside concurrent DMA writes.
 */
int gool_domain_map_readonly(gool_domain *domain,uint32_t address,const void *bytes,
                               uint32_t size,gool_domain_lease *lease);
int gool_domain_map_mutable(gool_domain *domain,uint32_t address,void *bytes,
                              uint32_t size,gool_domain_lease *lease);
/* Explicit aligned mutable subregion of a retained mutable root mapping.
 * Used to expose the VM's actual stack words at the original object address.
 * Parent stays retained until the overlay's final release; overlapping sibling
 * overlays and readonly parent regions reject. Releasing an overlay restores
 * access to parent backing bytes, which the owner must synchronize first if
 * native object-slot reuse needs those latest values. No implicit copy occurs. */
int gool_domain_map_overlay(gool_domain *domain,gool_domain_lease parent,uint32_t address,
                              void *bytes,uint32_t size,gool_domain_lease *lease);
int gool_domain_retain(gool_domain *domain,gool_domain_lease lease);
int gool_domain_release(gool_domain *domain,gool_domain_lease lease);
int gool_domain_references(const gool_domain *domain,gool_domain_lease lease,uint32_t *count);
int gool_domain_empty(const gool_domain *domain);
/* Aligned little-endian accesses, entirely within one live mapped region.
 * Read rejection preserves output; readonly rejection does not modify bytes. */
int gool_domain_read8(const gool_domain *domain,uint32_t address,uint8_t *value);
int gool_domain_read16(const gool_domain *domain,uint32_t address,uint16_t *value);
int gool_domain_read32(const gool_domain *domain,uint32_t address,uint32_t *value);
int gool_domain_write8(gool_domain *domain,uint32_t address,uint8_t value);
int gool_domain_write16(gool_domain *domain,uint32_t address,uint16_t value);
int gool_domain_write32(gool_domain *domain,uint32_t address,uint32_t value);
/* Direct callback adapters for native creation/search/zone services. */
int gool_domain_read(void *context,uint32_t address,uint32_t *value);
int gool_domain_write(void *context,uint32_t address,uint32_t value);
#endif
