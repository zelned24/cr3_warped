#include "gool_native_geometry.h"
#include "gool_native_engine.h"
#include "gool_atan.h"
#include "gool_search.h"
#include "zdat_lookup.h"
#include "gool_native_bounds.h"
#include "gool_native_pool.h"

static int32_t signed_word(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
static uint32_t shift8(uint32_t value) {
    return (value>>8)|((value&0x80000000u)?0xff000000u:0);
}
static uint32_t shift16(uint32_t value) {
    return (value>>16)|((value&0x80000000u)?0xffff0000u:0);
}
static uint32_t quarter(uint32_t value) {
    if(value&0x80000000u)value+=3u;
    return (value>>2)|((value&0x80000000u)?0xc0000000u:0);
}
static uint32_t sixteenth(uint32_t value) {
    if(value&0x80000000u)value+=15u;
    return (value>>4)|((value&0x80000000u)?0xf0000000u:0);
}
static int divide(uint32_t numerator,uint32_t denominator,uint32_t *result) {
    if(!denominator||(numerator==0x80000000u&&denominator==UINT32_MAX))return GOOL_VM_ARITHMETIC;
    *result=(uint32_t)(signed_word(numerator)/signed_word(denominator));return 0;
}
/* Native3D9DC restoring integer square root, including wrapped input sums. */
static uint32_t square_root(uint32_t value) {
    uint32_t result=0,remainder=0;
    for(unsigned i=0;i<16;i++) {
        remainder|=value>>30;value<<=2;
        uint32_t trial=remainder-((result<<2)+1);result<<=1;
        if(signed_word(trial)>=0){result++;remainder=trial;}
        remainder<<=2;
    }
    return result;
}

static int valid(gool_native_registry *r,uint32_t node,uint32_t instruction) {
    return r&&r->domain&&r->actors&&node&&!(node&3u)&&node<=UINT32_MAX-0x120u&&
           (instruction>>24)==0x1cu;
}
static int read_word(gool_native_registry *r,uint32_t address,uint32_t *value) {
    return gool_native_registry_read(r,address,value);
}
static int write_word(gool_native_registry *r,uint32_t address,uint32_t value) {
    return gool_native_registry_write(r,address,value);
}
static int stack(gool_native_registry *r,uint32_t node,uint32_t *sp) {
    return gool_domain_read32(r->domain,node+0xbcu,sp);
}
static int set_stack(gool_native_registry *r,uint32_t node,uint32_t sp) {
    if(sp&3u)return GOOL_NATIVE_GEOMETRY_INVALID;
    for(uint32_t i=0;i<r->count;i++) {
        gool_native_actor *a=r->actors[i];
        if(a&&a->attached&&a->node==node&&
           (sp<a->stack_address||(uint64_t)sp>(uint64_t)a->stack_address+a->stack_words*4u))
            return GOOL_VM_STACK;
    }
    return gool_domain_write32(r->domain,node+0xbcu,sp);
}
static int push(gool_native_registry *r,uint32_t node,uint32_t value) {
    uint32_t sp;int error=stack(r,node,&sp);if(error)return error;
    if(sp>UINT32_MAX-4u)return GOOL_VM_STACK;
    for(uint32_t i=0;i<r->count;i++) {
        gool_native_actor *a=r->actors[i];
        if(a&&a->attached&&a->node==node&&
           (sp<a->stack_address||(uint64_t)sp+4u>(uint64_t)a->stack_address+a->stack_words*4u))
            return GOOL_VM_STACK;
    }
    error=gool_domain_write32(r->domain,sp,value);if(error)return error;
    return set_stack(r,node,sp+4u);
}
static int pop_address(gool_native_registry *r,uint32_t node,uint32_t *address) {
    uint32_t sp;int error=stack(r,node,&sp);if(error)return error;
    if(sp<4u)return GOOL_VM_STACK;
    error=set_stack(r,node,sp-4u);if(error)return error;
    *address=sp-4u;return 0;
}
static int link(gool_native_registry *r,uint32_t node,uint32_t instruction,uint32_t *target) {
    return read_word(r,node+0x40u+((instruction>>10)&0x1cu),target);
}
static int occupancy(gool_native_registry *r,uint32_t operand,uint32_t *address,uint32_t *bits) {
    uint32_t value;int error=read_word(r,operand,&value);if(error)return error;
    *address=0x8006b688u+(value>>8)*4u;
    return read_word(r,*address,bits);
}
static int remaining(gool_native_registry *r,uint32_t node,uint32_t instruction,uint32_t operand,
                      const gool_native_geometry_services *s) {
    if(!s||!s->remaining)return GOOL_NATIVE_GEOMETRY_UNSUPPORTED;
    int error=s->remaining(s->context,r,node,instruction,operand);
    return error>0?GOOL_NATIVE_GEOMETRY_INVALID:error;
}
static int tree_read(void *context,uint32_t object,uint32_t offset,uint32_t *value) {
    return read_word(context,object+offset,value);
}
static int tree_write(void *context,uint32_t object,uint32_t offset,uint32_t value) {
    return write_word(context,object+offset,value);
}
typedef struct {
    gool_native_registry *registry;
    uint32_t excluded,mask,signal,best,distance;
} nearest_context;
static int nearest(void *context,uint32_t candidate,int *matched) {
    nearest_context *n=context;gool_native_registry *r=n->registry;
    uint32_t behavior,header,category,delta[3],distance,table,flags,mode,status;
    *matched=0;if(candidate==n->excluded)return 0;
    int error=read_word(r,candidate+0xc,&behavior);if(error)return error;
    error=read_word(r,behavior+0x10,&header);if(error)return error;
    error=read_word(r,header+4,&category);if(error)return error;
    if(!((1u<<((category>>8)&31u))&n->mask))return 0;
    for(uint32_t i=0;i<3;i++) {
        uint32_t a,b;
        error=read_word(r,candidate+0x60+i*4,&a);if(error)return error;
        error=read_word(r,n->excluded+0x60+i*4,&b);if(error)return error;
        delta[i]=a-b;if(delta[i]&0x80000000u)delta[i]=0u-delta[i];
    }
    uint32_t largest;
    if(signed_word(delta[0])>=signed_word(delta[1])&&signed_word(delta[0])>=signed_word(delta[2]))largest=0;
    else if(signed_word(delta[1])>=signed_word(delta[0])&&signed_word(delta[1])>=signed_word(delta[2]))largest=1;
    else largest=2;
    uint32_t other=0;for(uint32_t i=0;i<3;i++)if(i!=largest)other+=delta[i];
    distance=delta[largest]+quarter(other);
    if(signed_word(distance)>=signed_word(n->distance))return 0;
    if(n->signal!=255u) {
        error=read_word(r,behavior+0x1c,&table);if(error)return error;
        uint16_t route;
        error=gool_domain_read16(r->domain,table+(shift8(n->signal)<<1),&route);if(error)return error;
        if(route==255u) {
            int accept=0;
            if(n->signal==0x300u) {
                error=read_word(r,candidate+0x108,&mode);if(error)return error;
                error=read_word(r,candidate+0xb0,&flags);if(error)return error;
                accept=!(mode-2u<3u)&&!(flags&2u);
            }
            if(!accept&&n->signal==0xa00u) {
                error=read_word(r,candidate+0x100,&status);if(error)return error;
                accept=!(status&0x200u);
            }
            if(!accept&&n->signal==0x1d00u) {
                error=read_word(r,candidate+0x100,&status);if(error)return error;
                accept=!(status&0x00800000u);
            }
            if(!accept)return 0;
        } else if(!(route&0x8000u)) {
            uint32_t motion;
            error=read_word(r,behavior+0x20,&table);if(error)return error;
            error=read_word(r,candidate+0x108,&mode);if(error)return error;
            error=read_word(r,candidate+0xb0,&flags);if(error)return error;
            error=read_word(r,table+(uint32_t)route*16u,&motion);if(error)return error;
            if(mode-2u<3u)flags|=0x402u;
            if(flags&motion)return 0;
        }
    }
    n->best=candidate;n->distance=distance;return 0;
}
int gool_native_geometry_operand(gool_native_registry *r,uint32_t node,uint32_t instruction,
                               const gool_native_geometry_services *s,uint32_t *out) {
    if(!valid(r,node,instruction)||!out)return GOOL_NATIVE_GEOMETRY_INVALID;
    uint32_t raw=instruction&0xfffu,address;int error;
    if((raw&0xe00u)==0xe00u) {
        if(raw==0xe1fu){error=pop_address(r,node,&address);if(error)return error;}
        else address=node+0x40u+(raw&0x1ffu)*4u;
    } else {
        if(!s||!s->address)return GOOL_NATIVE_GEOMETRY_UNSUPPORTED;
        error=s->address(s->context,node,raw,&address);
        if(error)return error>0?GOOL_NATIVE_GEOMETRY_INVALID:error;
    }
    *out=address;return 0;
}
int gool_native_geometry_apply(gool_native_registry *r,uint32_t node,uint32_t instruction,
                               const gool_native_geometry_services *s) {
    uint32_t address;int error=gool_native_geometry_operand(r,node,instruction,s,&address);
    if(error)return error;
    return gool_native_geometry_resolved(r,node,instruction,address,s);
}
int gool_native_geometry_resolved(gool_native_registry *r,uint32_t node,uint32_t instruction,
    uint32_t operand,const gool_native_geometry_services *s) {
    if(!valid(r,node,instruction))return GOOL_NATIVE_GEOMETRY_INVALID;
    uint32_t selector=(instruction>>20)&15u,sub=(instruction>>15)&31u;
    uint32_t target,value,address,bits,sp;int error;
    if(!selector)return 0; /* Native table range check, after address resolution. */
    switch(selector) {
    case 1:case 6: {
        /* Native35DC0/10780: selector1 measures self, selector6 measures the
         * supplied vector. Bit3 selects the original camera/global vector. */
        uint32_t kind=sub&7u,first=selector==6u?operand:node+0x60u,second;
        if(sub&8u)second=0x8005c1a4u;
        else {
            error=link(r,node,instruction,&second);if(error)return error;
            second+=0x60u;
        }
        if(kind==5u)return 0; /* Original table entry36B64, no vector fetch. */
        int flat=kind==2u||kind==3u||kind==6u||kind==7u;
        uint32_t difference[3]={0};
        for(uint32_t i=0;i<3;i++) {
            if(flat&&i==1u)continue;
            uint32_t a,b;
            error=read_word(r,first+i*4u,&a);if(error)return error;
            error=read_word(r,second+i*4u,&b);if(error)return error;
            difference[i]=a-b;
        }
        if(kind==1u||kind==3u||kind==7u) {
            uint32_t sum=0;
            for(uint32_t i=0;i<3;i++) {
                if(flat&&i==1u)continue;
                uint32_t d=kind==7u?shift16(difference[i]):shift8(difference[i]);
                sum+=d*d;
            }
            value=square_root(sum)<<(kind==7u?16u:8u);
        } else {
            for(uint32_t i=0;i<3;i++)if(difference[i]&0x80000000u)difference[i]=0u-difference[i];
            uint32_t largest;
            if(flat) {
                largest=signed_word(difference[0])<signed_word(difference[2])?2u:0u;
                value=difference[largest]+quarter(difference[largest==0u?2u:0u]);
            } else {
                if(signed_word(difference[0])>=signed_word(difference[1])&&
                   signed_word(difference[0])>=signed_word(difference[2]))largest=0;
                else if(signed_word(difference[1])>=signed_word(difference[0])&&
                        signed_word(difference[1])>=signed_word(difference[2]))largest=1;
                else largest=2;
                uint32_t other=0;
                for(uint32_t i=0;i<3;i++)if(i!=largest)other+=difference[i];
                value=difference[largest]+quarter(other);
            }
        }
        return push(r,node,value);
    }
    case 2: {
        /* Native36188 chooses X/Y bearing for formats6/7, X/Z otherwise.
         * The camera/global origin option applies only to the X/Z path. */
        uint8_t format;uint32_t origin,ax,az,bx,bz;int32_t angle;
        error=gool_domain_read8(r->domain,node+0x120u,&format);if(error)return error;
        uint32_t second=(uint32_t)(format-6u)<2u?4u:8u;
        if(second==8u&&(sub&1u))origin=0x8005c1a4u;
        else {
            error=link(r,node,instruction,&origin);if(error)return error;
            origin+=0x60u;
        }
        error=read_word(r,operand,&bx);if(error)return error;
        error=read_word(r,origin,&ax);if(error)return error;
        error=read_word(r,operand+second,&bz);if(error)return error;
        error=read_word(r,origin+second,&az);if(error)return error;
        if(gool_atan2(signed_word(bx-ax),signed_word(bz-az),&angle))return GOOL_VM_ARITHMETIC;
        return push(r,node,(uint32_t)angle&0xfffu);
    }
    case 3:
        error=link(r,node,instruction,&target);if(error)return error;
        error=read_word(r,operand,&value);if(error)return error;
        error=read_word(r,target+0x40u+(value>>8)*4u,&value);if(error)return error;
        return push(r,node,value);
    case 4:
        error=link(r,node,instruction,&target);if(error)return error;
        error=read_word(r,operand,&value);if(error)return error;
        address=target+0x40u+(value>>8)*4u;
        error=pop_address(r,node,&sp);if(error)return error;
        error=read_word(r,sp,&value);if(error)return error;
        return write_word(r,address,value);
    case 5: {
        /* Native35E94 consumes the requested magnitude before reading XYZ.
         * All products are MULT/MFLO, including the small-magnitude raw path. */
        uint32_t argument,x,z,y=0,rate,length,normalized,product;
        error=pop_address(r,node,&argument);if(error)return error;
        error=read_word(r,argument,&rate);if(error)return error;rate=shift8(rate);
        error=read_word(r,operand+8u,&z);if(error)return error;z=shift8(z);
        error=read_word(r,operand,&x);if(error)return error;x=shift8(x);
        uint32_t sum=x*x+z*z;
        if(!(sub&1u)) {
            error=read_word(r,operand+4u,&y);if(error)return error;y=shift8(y);
            sum+=y*y;
        }
        length=square_root(sum);
        if(!(sub&1u)) {
            normalized=0;
            if(length) {
                error=divide(y*rate,length,&normalized);if(error)return error;
                normalized<<=8;
            }
            error=write_word(r,operand+4u,normalized);if(error)return error;
        }
        if(signed_word(rate)>16) {
            length=sixteenth(length);
            if(!length) {
                error=write_word(r,operand+8u,0);if(error)return error;
                return write_word(r,operand,0);
            }
            error=divide(sixteenth(x*rate),length,&normalized);if(error)return error;
            error=write_word(r,operand,normalized<<8);if(error)return error;
            error=divide(sixteenth(z*rate),length,&normalized);if(error)return error;
            return write_word(r,operand+8u,normalized<<8);
        }
        if(!length) {
            error=write_word(r,operand+8u,0);if(error)return error;
            return write_word(r,operand,0);
        }
        /* This signed rate<=16 path rereads both raw components and performs
         * both divisions before either store (native360F4..36184). */
        error=read_word(r,operand,&product);if(error)return error;
        error=divide(product*rate,length,&x);if(error)return error;
        error=read_word(r,operand+8u,&product);if(error)return error;
        error=divide(product*rate,length,&z);if(error)return error;
        error=write_word(r,operand,x);if(error)return error;
        return write_word(r,operand+8u,z);
    }
    case 7:
        if(!operand)return 0;
        error=read_word(r,operand,&value);if(error)return error;
        if(!value)return push(r,node,0);
        error=occupancy(r,operand,&address,&bits);if(error)return error;
        if(!(bits&1u))return push(r,node,0);
        if(!s||!s->hierarchy_scan_limit)return GOOL_NATIVE_GEOMETRY_UNSUPPORTED;
        for(uint32_t root=0x80068eb8u;root<=0x80068ec0u;root+=8u) {
            /* Native362CC rereads the serial before each root traversal. */
            error=read_word(r,operand,&value);if(error)return error;
            error=gool_search_serial(root,value,s->hierarchy_scan_limit,
                                     gool_native_registry_read,r,&target);
            if(error)return error;
            if(target)return push(r,node,target);
        }
        return push(r,node,0);
    case 8:
        if(!operand)return 0;
        error=occupancy(r,operand,&address,&bits);if(error)return error;
        return write_word(r,address,sub?bits&~2u:bits|2u);
    case 9: {
        uint32_t current;error=read_word(r,0x80065d5cu,&current);if(error)return error;
        int32_t result=signed_word(current);zdat_resource_lease found={0};
        if(operand) {
            if(!r->zones||!s||!s->zone_neighbor_limit)return GOOL_NATIVE_GEOMETRY_UNSUPPORTED;
            error=zdat_lookup_find(r->zones,current,operand,s->zone_neighbor_limit,&result,&found);
            if(error)return error;
        }
        if(result<-254) {
            error=link(r,node,instruction,&target);
            if(!error)error=write_word(r,target+0x18u,(uint32_t)result);
        }
        if(found.generation) {
            int cleanup=zdat_resources_release(r->zones,found);if(cleanup)return cleanup;
        }
        return error;
    }
    case 10:
        if(!operand||sub>=10u||sub==6u||sub==7u)return 0;
        if(sub==4u||sub==5u)return remaining(r,node,instruction,operand,s);
        error=occupancy(r,operand,&address,&bits);if(error)return error;
        if(sub==0)bits&=~4u;
        else if(sub==1)bits|=4u;
        else if(sub==2)bits&=~8u;
        else if(sub==3)bits|=8u;
        else if(sub==8)bits&=~1u;
        else bits|=1u;
        return write_word(r,address,bits);
    case 11:
        if(sub<1u||sub>3u)return 0;
        /* Native364D8/3651C/36554 reserves SP before any table read. */
        error=stack(r,node,&sp);if(error)return error;
        if(sp>UINT32_MAX-4u)return GOOL_VM_STACK;
        error=set_stack(r,node,sp+4u);if(error)return error;
        value=0;
        if(operand) {
            error=occupancy(r,operand,&address,&bits);if(error)return error;
            value=sub==1u?((bits>>1)^1u)&1u:bits&(sub==2u?4u:8u);
        }
        return gool_domain_write32(r->domain,sp,value);
    case 12:
        if(sub>=24u||sub==19u)return 0;
        switch(sub) {
        case 2: {
            uint32_t kind=(instruction>>12)&7u;
            if(kind>1u)return 0;
            if(!s||!s->hierarchy_scan_limit)return GOOL_NATIVE_GEOMETRY_UNSUPPORTED;
            error=read_word(r,operand,&target);if(error)return error;
            if(!kind)target=0x80068ea0u+(target>>8)*8u;
            gool_tree_services tree={.context=r,.read=tree_read,.write=tree_write};
            error=gool_tree_reparent(node,target,&tree,s->hierarchy_scan_limit);
            /* Original helper ignores the completed native return word. */
            return error==2||error==-24?0:error;
        }
        case 4:
            error=read_word(r,operand,&target);if(error)return error;
            error=read_word(r,0x80065d60u,&value);if(error)return error;
            return write_word(r,target+0x18u,value);
        case 8: {
            /* Native36748: pitch uses wrapped XZ deltas shifted before the
             * low32 square products; vertical subtraction stays unscaled. */
            uint32_t x,z,other_x,other_z,y,other_y;int32_t angle;
            error=link(r,node,instruction,&target);if(error)return error;
            target+=0x60u;
            error=read_word(r,target,&x);if(error)return error;
            error=read_word(r,operand,&other_x);if(error)return error;
            error=read_word(r,target+8u,&z);if(error)return error;
            error=read_word(r,operand+8u,&other_z);if(error)return error;
            x=shift8(x-other_x);z=shift8(z-other_z);
            uint32_t horizontal=square_root(x*x+z*z)<<8;
            error=read_word(r,target+4u,&y);if(error)return error;
            error=read_word(r,operand+4u,&other_y);if(error)return error;
            if(gool_atan2(signed_word(y-other_y),signed_word(horizontal),&angle))return GOOL_VM_ARITHMETIC;
            return push(r,node,(uint32_t)angle);
        }
        case 9:
            error=read_word(r,operand,&value);if(error)return error;
            return write_word(r,0x80060ac0u,value>>8);
        case 11: {
            uint32_t kind=(instruction>>12)&7u;
            if(kind>1u)return 0;
            return gool_native_engine_new_game(r->domain,kind?0u:1u);
        }
        case 12:
            error=read_word(r,0x8003ae8cu,&value);if(error)return error;
            return write_word(r,operand,value);
        case 13:
            error=read_word(r,operand,&value);if(error)return error;
            return write_word(r,0x8003ae8cu,value);
        case 14:case 16:
            error=link(r,node,instruction,&target);if(error)return error;
            error=read_word(r,operand,&value);if(error)return error;
            return write_word(r,target+(sub==14u?0x14u:0x10u),value);
        case 15:case 17:
            error=read_word(r,operand,&target);if(error)return error;
            error=read_word(r,target+(sub==15u?0x14u:0x10u),&value);if(error)return error;
            return push(r,node,value);
        case 20: {
            uint8_t first,character;
            error=gool_domain_read8(r->domain,0x80065bccu,&first);if(error)return error;
            value=0;
            if(!first) {
                error=gool_domain_read8(r->domain,0x80065bcdu,&character);if(error)return error;
                value=character<128u?character:(uint32_t)((int32_t)character-256);
                if(operand&&(character=='S'||character=='s')) {
                    uint8_t byte;
                    error=gool_domain_read8(r->domain,0x80065bd2u,&byte);if(error)return error;
                    error=write_word(r,operand,byte);if(error)return error;
                    error=gool_domain_read8(r->domain,0x80065bd3u,&byte);if(error)return error;
                    error=write_word(r,operand+4u,byte);if(error)return error;
                }
            }
            return push(r,node,value);
        }
        default:return remaining(r,node,instruction,operand,s);
        }
    case 13: {
        uint32_t signal,excluded,ignored;
        error=read_word(r,operand,&signal);if(error)return error;
        /* SignalF00 executes native1E42C inside the predicate. Its actual
         * resumable engine work cannot be replaced by a compatibility guess. */
        if(signal==0xf00u)return remaining(r,node,instruction,operand,s);
        if(!s||!s->hierarchy_scan_limit)return GOOL_NATIVE_GEOMETRY_UNSUPPORTED;
        error=link(r,node,instruction,&excluded);if(error)return error;
        nearest_context search={.registry=r,.excluded=excluded,.mask=sub,.signal=signal,.distance=INT32_MAX};
        error=gool_search_visit(0x80068ec0u,s->hierarchy_scan_limit,gool_native_registry_read,r,
                                nearest,&search,&ignored);if(error)return error;
        if(sub&0x17u) {
            error=gool_search_visit(0x80068eb8u,s->hierarchy_scan_limit,gool_native_registry_read,r,
                                    nearest,&search,&ignored);if(error)return error;
        }
        return push(r,node,search.best);
    }
    case 14: {
        error=link(r,node,instruction,&target);if(error)return error;
        int port_slot=target==GOOL_NATIVE_PLAYER_BASE||
            (target>=GOOL_NATIVE_POOL_BASE&&target-GOOL_NATIVE_POOL_BASE<GOOL_NATIVE_POOL_COUNT*GOOL_NATIVE_POOL_STRIDE&&
             (target-GOOL_NATIVE_POOL_BASE)%GOOL_NATIVE_POOL_STRIDE==0);
        if(!port_slot&&(target&0xff800003u)!=0x80000000u)return push(r,node,0);
        uint32_t model,record;uint8_t type;
        error=read_word(r,target+0xe8u,&model);if(error)return error;
        if(!model)return GOOL_NATIVE_GEOMETRY_INVALID;
        error=gool_domain_read8(r->domain,model,&type);if(error)return error;
        if(type==1) {
            error=gool_native_bounds_svtx(r->domain,target,0,&record);
            if(error==GOOL_DOMAIN_UNBOUND)return remaining(r,node,instruction,operand,s);
        } else error=gool_native_bounds_fallback(r->domain,target,&record);
        if(error==GOOL_DOMAIN_FULL)return push(r,node,0);
        if(error)return error;
        if(!record)return push(r,node,0);
        int contains;error=gool_native_bounds_contains(r->domain,operand,record+8u,&contains);
        if(!error)error=push(r,node,(uint32_t)contains);
        int cleanup=gool_native_bounds_pop(r->domain);
        return cleanup?cleanup:error;
    }
    case 15: {
        /* Native36B4C/17CA8. Peripheral commands keep their real service
         * boundary; the packed record commands below operate on mapped RAM. */
        int peripheral=sub==2u||sub==3u||sub==5u||sub==10u||
                       (sub>=12u&&sub<=18u);
        if(peripheral&&(!s||!s->peripheral))return remaining(r,node,instruction,operand,s);
        error=read_word(r,operand,&value);if(error)return error;
        if(peripheral) {
            uint32_t entry=0,arguments[2]={value,0},count=0,result=0;
            switch(sub) {
            case 2:entry=0x80071590u;arguments[0]=0x80u;count=1;break;
            case 3:entry=0x800717d8u;count=1;break;
            case 5:entry=0x800718d0u;break;
            case 10:entry=0x80071930u;break;
            case 12:entry=0x80071974u;break;
            case 13:entry=0x80071c8cu;count=1;break;
            case 14:entry=0x80072094u;count=1;break;
            case 15:
                if(!s->peripheral_owner_address)return GOOL_NATIVE_GEOMETRY_UNSUPPORTED;
                error=read_word(r,s->peripheral_owner_address,&address);if(error)return error;
                if(!address)return write_word(r,node+0xd4u,1u);
                /* Native shift/add sequence is wrapping value*1420+6F4. */
                arguments[0]=address+value*1420u+0x6f4u;count=2;
                error=s->peripheral(s->context,0x80071abcu,arguments,count,&result);
                if(error)return error>0?GOOL_NATIVE_GEOMETRY_INVALID:error;
                entry=0x800719d8u;count=0;break;
            case 16:entry=0x80071d38u;count=1;break;
            case 17:entry=0x80071da8u;count=1;break;
            case 18:entry=0x80071880u;count=1;break;
            }
            error=s->peripheral(s->context,entry,arguments,count,&result);
            if(error)return error>0?GOOL_NATIVE_GEOMETRY_INVALID:error;
            if(sub==2u)result=1;
            else if(sub==15u)result=0;
            return write_word(r,node+0xd4u,result);
        }
        if(sub!=19u&&sub!=20u)return write_word(r,node+0xd4u,1u);
        uint32_t index=shift8(value);
        address=0x8006b0e8u+index*36u+(value&255u)*9u;
        if(sub==19u) {
            /* A2 retains the second slot's address while native BC advances;
             * its +4 store is the third slot. Return rereads that last word. */
            uint8_t bytes[4];uint32_t packed=0;
            if(index<40u) {
                error=gool_domain_read8(r->domain,address+8u,&bytes[1]);if(error)return error;
                error=gool_domain_read8(r->domain,address+7u,&bytes[0]);if(error)return error;
                packed=(uint32_t)bytes[0]|((uint32_t)bytes[1]<<8);
            }
            error=push(r,node,packed);if(error)return error;
            packed=0;
            if(index<40u) {
                const uint8_t order[4]={1,0,2,3};
                for(unsigned i=0;i<4;i++) {
                    unsigned b=order[i];
                    error=gool_domain_read8(r->domain,address+3u+b,&bytes[b]);if(error)return error;
                }
                for(unsigned i=0;i<4;i++)packed|=(uint32_t)bytes[i]<<(8u*i);
            }
            error=push(r,node,packed);if(error)return error;
            packed=0;
            if(index<40u) {
                error=gool_domain_read8(r->domain,address+1u,&bytes[1]);if(error)return error;
                error=gool_domain_read8(r->domain,address,&bytes[0]);if(error)return error;
                error=gool_domain_read8(r->domain,address+2u,&bytes[2]);if(error)return error;
                packed=(uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16);
            }
            error=push(r,node,packed);if(error)return error;
            error=stack(r,node,&sp);if(error)return error;
            error=gool_domain_read32(r->domain,sp-4u,&value);if(error)return error;
        } else {
            if(index>=40u) {
                error=stack(r,node,&sp);if(error)return error;
                if(sp<12u)return GOOL_VM_STACK;
                error=set_stack(r,node,sp-12u);if(error)return error;
            } else {
                /* Native stores bytes in this order; aliasing with later
                 * stack reads must remain observable. */
                const uint8_t offsets[3][4]={{1,0,2,0},{4,5,3,6},{7,8,0,0}};
                const uint8_t shifts[3][4]={{8,0,16,0},{8,16,0,24},{0,8,0,0}};
                const uint8_t counts[3]={3,4,2};
                for(unsigned i=0;i<3;i++) {
                    error=pop_address(r,node,&sp);if(error)return error;
                    error=read_word(r,sp,&value);if(error)return error;
                    for(unsigned j=0;j<counts[i];j++) {
                        error=gool_domain_write8(r->domain,address+offsets[i][j],(uint8_t)(value>>shifts[i][j]));
                        if(error)return error;
                    }
                }
            }
            value=0;
        }
        return write_word(r,node+0xd4u,value);
    }
    default:return remaining(r,node,instruction,operand,s);
    }
}
