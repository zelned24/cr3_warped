#include "nsf_page.h"
#include <string.h>

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|
           (uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int overlaps(const void *left,size_t left_size,const void *right,size_t right_size) {
    uintptr_t a=(uintptr_t)left,b=(uintptr_t)right;
    return a<=b?b-a<left_size:a-b<right_size;
}

int nsf_page_decode(const void *source,size_t source_size,
                    void *destination,size_t destination_size,size_t *consumed) {
    if(!source||!destination||!consumed||source_size<2||destination_size<NSF_PAGE_BYTES||
       source_size>UINTPTR_MAX-(uintptr_t)source||
       NSF_PAGE_BYTES>UINTPTR_MAX-(uintptr_t)destination||
       sizeof(*consumed)>UINTPTR_MAX-(uintptr_t)consumed||
       overlaps(source,source_size,destination,NSF_PAGE_BYTES)||
       overlaps(source,source_size,consumed,sizeof(*consumed))||
       overlaps(destination,NSF_PAGE_BYTES,consumed,sizeof(*consumed)))return NSF_PAGE_INVALID;
    const uint8_t *src=source;
    uint8_t *out=destination;
    uint32_t magic=(uint32_t)src[0]|(uint32_t)src[1]<<8;
    if(magic==0x1234) {
        if(source_size<NSF_PAGE_BYTES)return NSF_PAGE_INVALID;
        memcpy(out,src,NSF_PAGE_BYTES);
        *consumed=NSF_PAGE_BYTES;
        return NSF_PAGE_OK;
    }
    if(magic!=0x1235||source_size<12)return NSF_PAGE_INVALID;
    uint32_t length=read32(src+4),skip=read32(src+8);
    if(length>NSF_PAGE_BYTES)return NSF_PAGE_INVALID;
    size_t position=12,produced=0;
    /* Validate every token and tail extent before touching caller output.
     * Zero literal tokens consume input; source_size bounds that scan too. */
    while(produced<length) {
        if(position>=source_size)return NSF_PAGE_INVALID;
        uint32_t format=src[position++];
        if(format&0x80) {
            if(position>=source_size)return NSF_PAGE_INVALID;
            uint32_t token=src[position++];
            uint32_t distance=((format&0x7f)<<5)|(token>>3);
            uint32_t span=token&7;
            span=span==7?64:span+3;
            if(!distance||distance>produced||span>length-produced)return NSF_PAGE_INVALID;
            produced+=span;
        } else {
            if(format>source_size-position||format>length-produced)return NSF_PAGE_INVALID;
            position+=format;produced+=format;
        }
    }
    if(skip>source_size-position)return NSF_PAGE_INVALID;
    size_t tail=position+skip,remainder=NSF_PAGE_BYTES-length;
    if(remainder>source_size-tail)return NSF_PAGE_INVALID;
    size_t used=tail+remainder;
    position=12;produced=0;
    while(produced<length) {
        uint32_t format=src[position++];
        if(format&0x80) {
            uint32_t token=src[position++];
            uint32_t distance=((format&0x7f)<<5)|(token>>3);
            uint32_t span=token&7;
            span=span==7?64:span+3;
            /* Forward copy deliberately supports overlapping LZ references. */
            for(uint32_t i=0;i<span;i++) {
                out[produced]=out[produced-distance];produced++;
            }
        } else {
            memcpy(out+produced,src+position,format);
            position+=format;produced+=format;
        }
    }
    memcpy(out+length,src+tail,remainder);
    *consumed=used;
    return NSF_PAGE_OK;
}
