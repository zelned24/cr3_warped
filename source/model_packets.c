#include "model_packets.h"

static int span_valid(const gool_domain *d,const model_packet_span *s) {
    if(!s->address||!s->bytes||((s->address|s->bytes)&3u)||
       s->lease.index>=GOOL_DOMAIN_REGIONS||!s->lease.generation||
       (uint64_t)s->address+s->bytes>UINT64_C(0x100000000)||
       (uint64_t)(s->address&0xffffffu)+s->bytes>0x1000000u)return 0;
    const gool_domain_region *r=&d->regions[s->lease.index];
    return r->references&&r->generation==s->lease.generation&&s->address>=r->address&&
        (uint64_t)s->address+s->bytes<=(uint64_t)r->address+r->size;
}
static int resolve(const model_packets *o,uint32_t link,uint32_t *address,uint32_t *remaining) {
    if(link&3u)return MODEL_PACKETS_INVALID;
    for(uint32_t i=0;i<2;i++) {
        const model_packet_span *s=&o->spans[i];uint32_t low=s->address&0xffffffu;
        if(link>=low&&link-low<s->bytes) {
            *address=s->address+(link-low);*remaining=s->bytes-(link-low);return 0;
        }
    }
    return MODEL_PACKETS_INVALID;
}
int model_packets_begin(model_packets *o,gool_domain *d,const model_packet_span spans[2],uint32_t first) {
    if(!o||o->opened||o->retained[0]||o->retained[1]||!d||!spans||first>0xffffffu||
       (first!=0xffffffu&&(first&3u))||!span_valid(d,&spans[0])||!span_valid(d,&spans[1]))
        return MODEL_PACKETS_INVALID;
    uint32_t a=spans[0].address&0xffffffu,b=spans[1].address&0xffffffu;
    if((uint64_t)a+spans[0].bytes>b&&(uint64_t)b+spans[1].bytes>a)return MODEL_PACKETS_INVALID;
    model_packet_span captured[2]={spans[0],spans[1]};
    *o=(model_packets){.domain=d,.spans={captured[0],captured[1]},.cursor=first,
        .node_limit=captured[0].bytes/4u+captured[1].bytes/4u,.opened=1,.status=MODEL_PACKETS_PENDING};
    for(uint32_t i=0;i<2;i++) {
        int error=gool_domain_retain(d,o->spans[i].lease);
        if(error)return o->status=error;
        o->retained[i]=1;
    }
    return o->status;
}
int model_packets_step(model_packets *o,uint32_t budget) {
    if(!o||!o->opened)return MODEL_PACKETS_INVALID;
    if(o->status!=MODEL_PACKETS_PENDING)return o->status;
    while(budget--) {
        if(!o->loading) {
            if(o->cursor==0xffffffu)return o->status=MODEL_PACKETS_DONE;
            if(o->nodes>=o->node_limit)return o->status=MODEL_PACKETS_CYCLE;
            uint32_t address,remaining,tag;
            int error=resolve(o,o->cursor,&address,&remaining);if(error)return o->status=error;
            error=gool_domain_read32(o->domain,address,&tag);if(error)return o->status=error;
            uint32_t count=tag>>24;
            if(remaining<4u||count>(remaining-4u)/4u)return o->status=MODEL_PACKETS_INVALID;
            o->nodes++;o->address=address;o->next=tag&0xffffffu;o->count=count;o->copied=0;
            if(!count){o->cursor=o->next;continue;}
            o->loading=1;
        } else {
            uint32_t value;int error=gool_domain_read32(o->domain,o->address+4u+o->copied*4u,&value);
            if(error)return o->status=error;
            o->words[o->copied++]=value;
            if(o->copied==o->count)return o->status=MODEL_PACKETS_PACKET;
        }
    }
    return MODEL_PACKETS_PENDING;
}
int model_packets_consume(model_packets *o) {
    if(!o||!o->opened||o->status!=MODEL_PACKETS_PACKET||!o->loading||o->copied!=o->count)
        return MODEL_PACKETS_INVALID;
    o->cursor=o->next;o->loading=0;o->status=MODEL_PACKETS_PENDING;return o->status;
}
int model_packets_close(model_packets *o) {
    if(!o)return MODEL_PACKETS_INVALID;
    for(uint32_t i=0;i<2;i++)if(o->retained[i]) {
        int error=gool_domain_release(o->domain,o->spans[i].lease);if(error)return error;
        o->retained[i]=0;
    }
    *o=(model_packets){0};return 0;
}
int model_packets_decode(const model_packets *o,uint32_t word,model_packet_command *output) {
    if(!o||!o->opened||o->status!=MODEL_PACKETS_PACKET||!o->loading||o->copied!=o->count||
       !output||word>=o->count)return MODEL_PACKETS_INVALID;
    const uint32_t *p=&o->words[word];
    uint8_t opcode=(uint8_t)(p[0]>>24);
    model_packet_command next={.opcode=opcode,.next_word=word+1u};
    if(!opcode)next.kind=MODEL_PACKET_NOP;
    else if(opcode==0xe1u){next.kind=MODEL_PACKET_DRAW_MODE;next.draw_mode=p[0]&0xffffffu;}
    else {
        uint32_t family=opcode&0xfcu;
        if(family!=0x20u&&family!=0x24u&&family!=0x30u&&family!=0x34u)
            return MODEL_PACKETS_INVALID;
        next.kind=MODEL_PACKET_TRIANGLE;next.textured=(opcode>>2)&1u;
        next.gouraud=(opcode>>4)&1u;next.semi_transparent=(opcode>>1)&1u;
        next.raw_texture=opcode&1u;
        uint32_t words=next.gouraud?(next.textured?9u:6u):(next.textured?7u:4u);
        if(words>o->count-word)return MODEL_PACKETS_INVALID;
        uint32_t cursor=0,color=p[cursor++]&0xffffffu;
        for(uint32_t i=0;i<3;i++) {
            if(i&&next.gouraud)color=p[cursor++]&0xffffffu;
            next.colors[i]=color;next.sxy[i]=p[cursor++];
            if(next.textured)next.uv[i]=p[cursor++];
        }
        next.next_word=word+words;
    }
    *output=next;return 0;
}
int model_packets_material(const model_packet_command *c,const uint32_t *eids,
    uint32_t count,model_material_uv *output) {
    if(!c||c->kind!=MODEL_PACKET_TRIANGLE||!c->textured||!eids||!count||count>8u||!output)
        return MODEL_PACKETS_INVALID;
    model_material_uv next={.clut=(uint16_t)(c->uv[0]>>16),.texpage=(uint16_t)(c->uv[1]>>16),
        .min_u=255u,.min_v=255u};
    next.texture_index=(next.texpage>>10)&7u;
    uint32_t depth=(next.texpage>>7)&3u;
    if(next.texture_index>=count||depth==3u||!(eids[next.texture_index]&1u))return MODEL_PACKETS_INVALID;
    next.texture_eid=eids[next.texture_index];next.depth=(uint8_t)(4u<<depth);
    next.blend=(next.texpage>>5)&3u;
    for(uint32_t i=0;i<4;i++) {
        uint32_t uv=i==3u?c->uv[2]>>16:c->uv[i];
        next.u[i]=(uint8_t)uv;next.v[i]=(uint8_t)(uv>>8);
        if(next.u[i]<next.min_u)next.min_u=next.u[i];
        if(next.u[i]>next.max_u)next.max_u=next.u[i];
        if(next.v[i]<next.min_v)next.min_v=next.v[i];
        if(next.v[i]>next.max_v)next.max_v=next.v[i];
    }
    *output=next;return 0;
}
int model_packets_bound_material(const model_packet_command *c,uint32_t packet,uint32_t word,
    const model_packet_binding *bindings,uint32_t count,model_material_uv *output) {
    if(!c||c->kind!=MODEL_PACKET_TRIANGLE||!c->textured||!bindings||!count||!output)
        return MODEL_PACKETS_INVALID;
    uint32_t lo=0,hi=count;
    while(lo<hi) {
        uint32_t mid=lo+(hi-lo)/2u;const model_packet_binding *b=&bindings[mid];
        if(b->packet<packet||(b->packet==packet&&b->word<word))lo=mid+1u;else hi=mid;
    }
    if(lo==count||bindings[lo].packet!=packet||bindings[lo].word!=word)return MODEL_PACKETS_INVALID;
    uint32_t eids[8]={0};eids[(c->uv[1]>>26)&7u]=bindings[lo].texture_eid;
    return model_packets_material(c,eids,8u,output);
}
