#ifndef GOOL_DATA_H
#define GOOL_DATA_H
#include "runtime_archive.h"

typedef struct gool_data gool_data;
typedef struct {
    void *context;
    int (*open)(void *context,uint32_t eid,gool_data *out);
    int (*lookup)(void *context,uint32_t lookup_index,gool_data *out);
    int (*resolve)(void *context,uint32_t address,gool_data *out,uint32_t *pc);
} gool_data_source;
typedef struct {const uint8_t *data;uint32_t size;} gool_data_item;
/* Borrowed immutable views. The owning archive or resource leases and source
 * resolver must outlive all views. Resolvers expose already retained data;
 * unavailable data is an error, never a synchronous fake load or zero library.
 * This module reads data; it is not a VM and never executes an instruction. */
struct gool_data {
    const cga_archive *archive;
    uint32_t eid, entry, lookup_index, category, stack_start, declared_signals;
    const uint8_t *code,*constants,*indices,*states,*models;
    const uint8_t *original_constants; /* stable EID provenance before native fixups */
    uint32_t code_words,constant_words,signal_count,object_count;
    uint32_t state_count,model_bytes;
    const gool_data_source *source;
    uint32_t native_constant_address;
    uint32_t native_code_address; /*0 selects existing tagged port code ABI*/
};

typedef struct {
    uint32_t motion_flags,interact_flags,alternate_eid;
    uint16_t alternate_index,on_signal,pre_run,script;
} gool_state_data;

typedef enum { GOOL_SCRIPT_MAIN,GOOL_SCRIPT_PRE_RUN,GOOL_SCRIPT_SIGNAL } gool_script_kind;

int gool_data_open(gool_data *out,const cga_archive *archive,uint32_t eid);
int gool_data_lookup(gool_data *out,const cga_archive *archive,uint32_t lookup_index);
/* Allocation-free original NSF item binding, shared validation with CGA.
 * Stable lookup_index identifies this behavior in the owner's code domain;
 * it must be assigned from the real level catalog, never request arrival order.
 * Failure leaves out unchanged. Source's callbacks resolve within that level. */
int gool_data_bind_items(gool_data *out,uint32_t eid,uint32_t entry,uint32_t lookup_index,
                           const gool_data_item *items,uint32_t count,const gool_data_source *source);
int gool_data_open_relative(gool_data *out,const gool_data *scope,uint32_t eid);
int gool_data_lookup_relative(gool_data *out,const gool_data *scope,uint32_t lookup_index);
int gool_data_constant(const gool_data *data,uint32_t index,uint32_t *value);
int gool_data_instruction(const gool_data *data,uint32_t pc,uint32_t *instruction);
int gool_data_signal(const gool_data *data,uint32_t index,int32_t *value);
int gool_data_object(const gool_data *data,uint32_t index,int32_t *value);
int gool_data_state(const gool_data *data,uint32_t index,gool_state_data *out);
/* 0=resolved, 1=absent optional handler, -1=invalid/missing file or PC.
 * Resolve alternate behavior in the same level archive; never reuse a table
 * from a different level merely because its EID matches. */
int gool_data_script(const gool_data *base,uint32_t state,gool_script_kind kind,
                     gool_data *owner,uint32_t *pc);
#endif
