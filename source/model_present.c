#include "model_present.h"
#include <string.h>

int model_present_begin(model_present *o,gool_domain *domain,const model_packet_span spans[2],
    uint32_t first,const model_present_controls *controls) {
    if(!o||o->opened||o->packets.opened||!controls||!controls->draw||!controls->cache||
       !controls->draw->initialized||!controls->draw->recording||controls->draw->sealed||
       !controls->cache->opened||controls->cache->closing||controls->draw_mode>0x3fffu||
       controls->prior_vertices>MODEL_DRAW_VERTICES||
       (controls->binding_count&&!controls->bindings))return MODEL_PRESENT_INVALID;
    uint32_t depth;memcpy(&depth,&controls->depth,sizeof(depth));
    if((depth&0x7fffffffu)>=0x7f800000u)return MODEL_PRESENT_INVALID;
    model_present_controls captured=*controls;
    o->opened=1;o->status=MODEL_PRESENT_PENDING;o->phase=0;o->word=o->pass=o->triangles=o->passes=0;
    o->controls=captured;o->texture=(model_texture_handle){0};
    o->frame_vertices=captured.prior_vertices;
    int error=model_packets_begin(&o->packets,domain,spans,first);
    if(error!=MODEL_PACKETS_PENDING)return o->status=error;
    return o->status;
}
static int advance(model_present *o) {
    o->controls.draw_mode=o->plan.draw_mode;o->word=o->command.next_word;
    if(o->word>o->packets.count)return MODEL_PRESENT_INVALID;
    if(o->word==o->packets.count) {
        int error=model_packets_consume(&o->packets);if(error!=MODEL_PACKETS_PENDING)return error;
        o->phase=0;
    } else o->phase=1;
    return 0;
}
static int evict_unleased(model_texture_cache *cache) {
    for(uint32_t i=0;i<MODEL_TEXTURE_CACHE_SLOTS;i++) {
        model_texture_cache_slot *s=&cache->slots[i];
        if(s->generation&&!s->gpu.references)
            return model_texture_cache_evict(cache,(model_texture_handle){.generation=s->generation,.slot=i});
    }
    return MODEL_PRESENT_DRAIN;
}
int model_present_step(model_present *o,uint32_t budget,uint32_t pixel_budget) {
    if(!o||!o->opened)return MODEL_PRESENT_INVALID;
    if(o->status!=MODEL_PRESENT_PENDING)return o->status;
    while(budget--) {
        int error;
        if(o->phase==0) {
            error=model_packets_step(&o->packets,1);
            if(error==MODEL_PACKETS_DONE)return o->status=MODEL_PRESENT_DONE;
            if(error==MODEL_PACKETS_PACKET){o->word=0;o->phase=1;}
            else if(error!=MODEL_PACKETS_PENDING)return o->status=error;
        } else if(o->phase==1) {
            error=model_packets_decode(&o->packets,o->word,&o->command);if(error)return o->status=error;
            error=model_draw_packet_plan(&o->command,o->controls.draw_mode,&o->plan);if(error)return o->status=error;
            if(o->plan.count*3u>MODEL_DRAW_VERTICES-o->frame_vertices)
                return o->status=MODEL_PRESENT_VERTEX_LIMIT;
            o->pass=0;o->texture=(model_texture_handle){0};
            if(!o->plan.count) {error=advance(o);if(error)return o->status=error;}
            else if(o->command.textured) {
                error=model_packets_bound_material(&o->command,o->packets.address,o->word,
                    o->controls.bindings,o->controls.binding_count,&o->material);
                if(error)return o->status=error;
                o->phase=2;
            } else o->phase=3;
        } else if(o->phase==2) {
            error=model_texture_cache_request(o->controls.cache,&o->material,&o->texture);
            if(error==MODEL_TEXTURE_CACHE_DONE)o->phase=3;
            else if(error==NSF_STREAM_FULL)return MODEL_PRESENT_PENDING;
            else if(error==MODEL_TEXTURE_CACHE_FULL) {
                error=evict_unleased(o->controls.cache);
                if(error==MODEL_PRESENT_DRAIN)return error;
                if(error)return o->status=error;
            } else if(error==MODEL_TEXTURE_CACHE_PENDING||error==MODEL_TEXTURE_CACHE_BUSY) {
                error=model_texture_cache_poll(o->controls.cache,pixel_budget);
                if(error<0)return o->status=error;
            } else return o->status=error;
        } else if(o->phase==3) {
            error=model_draw_record_packet_triangle(o->controls.draw,&o->command,
                o->command.textured?&o->material:NULL,&o->controls.viewport,o->controls.depth,
                &o->plan.states[o->pass],o->command.textured?o->controls.cache:NULL,o->texture);
            if(error==MODEL_DRAW_CAPACITY)return MODEL_PRESENT_DRAIN;
            if(error==MODEL_TEXTURE_CACHE_PENDING){o->phase=2;continue;}
            if(error)return o->status=error;
            o->frame_vertices+=3u;o->passes++;o->pass++;
            if(o->pass==o->plan.count) {
                o->triangles++;error=advance(o);if(error)return o->status=error;
            }
        } else return o->status=MODEL_PRESENT_INVALID;
    }
    return MODEL_PRESENT_PENDING;
}
int model_present_close(model_present *o) {
    if(!o)return MODEL_PRESENT_INVALID;
    int error=model_packets_close(&o->packets);if(error)return error;
    *o=(model_present){0};return 0;
}
