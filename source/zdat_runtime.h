#ifndef ZDAT_RUNTIME_H
#define ZDAT_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
enum { ZDAT_INVALID=-60,ZDAT_FORMAT=-61,ZDAT_MISSING=-62,ZDAT_RANGE=-63 };
typedef struct {const uint8_t *data;uint32_t size;} zdat_span;
typedef struct {zdat_span bytes;uint16_t count;} zdat_database;
typedef struct {
    zdat_span bytes;
    uint32_t sizes_offset,times_offset,values_offset;
    uint16_t key,count;
    uint8_t format,stride;
} zdat_series;
typedef struct {
    const zdat_span *items;
    uint32_t item_count,zone_index,sky_eid,world_eids[6];
    uint32_t info_count,database_count,entity_count,other_count;
    int32_t offset[3];
    int has_offset;
} zdat_zone;
enum { ZDAT_ENTITY_FILE=1,ZDAT_ENTITY_OBJECT=2,ZDAT_ENTITY_LOOKUP=4,ZDAT_ENTITY_GEOMETRY=8 };
typedef struct {
    zdat_database database;
    zdat_series name,path,parameters;
    int32_t file_index,object_id,lookup_index,geometry_format,shift;
    uint32_t present;
} zdat_entity;

/* Immutable caller-owned byte spans and the item-span array must outlive every
 * derived view. Populate items from nsf_resource_item or CGA sections, retaining
 * their owning page lease/archive. No allocation, activation or coordinate-space
 * conversion occurs. Zone binding requires the original info_count2 layout
 * found in all1717 supplied zones. Functions preserve outputs on failure. */
int zdat_database_bind(zdat_database *out,zdat_span bytes);
int zdat_series_at(const zdat_database *database,uint32_t index,zdat_series *out);
int zdat_series_find(const zdat_database *database,uint16_t key,zdat_series *out);
/* value selects a series value; element selects one scalar/vector inside it.
 * Components=1 or3; unused components are zero. Timestamp is zero when absent. */
int zdat_series_value(const zdat_series *series,uint32_t value,uint32_t element,
                      int32_t out[3],uint32_t *components);
int zdat_series_length(const zdat_series *series,uint32_t value,uint32_t *length,uint16_t *time);
int zdat_zone_bind(zdat_zone *out,const zdat_span *items,uint32_t count);
int zdat_zone_database(const zdat_zone *zone,uint32_t index,zdat_database *out);
int zdat_zone_entity(const zdat_zone *zone,uint32_t index,zdat_entity *out);
int zdat_zone_other_eid(const zdat_zone *zone,uint32_t index,uint32_t *eid);
/* Reference placement units: (zone_offset + raw_point * 2^shift) *256.
 * int64 output prevents overflow; WGEO conversion remains an engine concern. */
int zdat_entity_position(const zdat_zone *zone,const zdat_entity *entity,
                          uint32_t point,int32_t raw[3],int64_t reference[3]);
/* Actual NSD class table at 0x520 + directory_count*8 +0x10,128 EIDs.
 * State indirection uses signed16 GOOL item3 after signal_count entries;
 * returns1 for native no-state values (<0 or255),0 for an in-range state. */
int zdat_class_eid(zdat_span nsd,int32_t file_index,uint32_t *eid);
int zdat_entity_state(const zdat_entity *entity,zdat_span indices,
                       uint32_t signal_count,uint32_t state_count,uint32_t *state);
#endif
