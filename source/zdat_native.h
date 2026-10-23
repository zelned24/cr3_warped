#ifndef ZDAT_NATIVE_H
#define ZDAT_NATIVE_H
#include "gool_domain.h"
#define ZDAT_NATIVE_CHAIN_MAX 128u
enum {ZDAT_NATIVE_INVALID=-110,ZDAT_NATIVE_RANGE=-111,ZDAT_NATIVE_CYCLE=-112};
typedef struct {gool_domain *domain;uint32_t chain_limit;} zdat_native;
typedef struct {uint32_t address,descriptor,length,end;uint8_t format,stride;} zdat_native_value;
/* Original32710 direct-index mode2, used by placement creation. Binary search
 * compares signed16 keys/count; missing keys follow the actual database+8 link.
 * An existing key with out-of-range value/element returns address0 immediately,
 * without searching a parent. Input/header/array extents and fallback cycles
 * are checked before access. No time interpolation or flag80 guess occurs.
 * Database links must already be native-address runtime overlays, not raw EIDs.
 * Caller retains every mapped source/database across calls on the owner thread.
 * Outputs change only on success; address0 is the native missing-data result. */
int zdat_native_select(const zdat_native *source,uint32_t database,int32_t key,
                       uint32_t value,uint32_t element,zdat_native_value *out);
/* Original32710 mode1, element0: exact signed(position>>8) time lookup,
 * including variable-length lists and linked-database fallback. A missing key,
 * absent timing array, in-between time or empty list returns address0. Caller
 * retains the database chain and returned words throughout consumption. */
int zdat_native_select_exact(const zdat_native *source,uint32_t database,int32_t key,
                            uint32_t position,zdat_native_value *out);
/* Original32DF8(db,key,0,0,0,2,default0),32EDC(db,4B,0,2), and creation's
 * direct32710 path/parameter calls. These signatures bind gool_create services.
 * Scalar decoding intentionally matches native byte/halfword cases and default
 * word load, including signed extension; it is not a vector reinterpretation. */
/* Original21330..21434 database ownership/link prefix. Camera databases are
 * triples: all get zone+4 ownership, latter two inherit the first through+8.
 * Entity databases get ownership only. The later adjacency/entry-init service
 * is separate and must run with the real previous zone. Mutable runtime copies
 * and a translated native item table are required. Errors retain prior writes. */
int zdat_native_link_zone(zdat_native *source,uint32_t entry);
int zdat_native_scalar(void *context,uint32_t database,uint32_t key,uint32_t *value);
int zdat_native_geometry(void *context,uint32_t database,uint32_t *value);
int zdat_native_path(void *context,uint32_t database,uint32_t *address);
int zdat_native_path_count(void *context,uint32_t database,uint32_t *count);
int zdat_native_parameters(void *context,uint32_t database,uint32_t *address,uint32_t *count);
#endif
