#include "nsf_resource.h"
#include <string.h>

static nsd_resource_index active_index;
static int opened;
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t rd16(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8;}
int nsd_resource_bind(nsd_resource_index *out,const void *data,size_t size) {
    if(!out||!data||size<0x520u||size>NSD_RESOURCE_MAX_BYTES)return NSF_RESOURCE_INVALID;
    const uint8_t *bytes=data;uint32_t count=rd32(bytes+0x404);
    if((uint64_t)0x520+(uint64_t)count*8+0x114>size)return NSF_RESOURCE_FORMAT;
    uint32_t previous=0;
    for(unsigned i=0;i<256;i++) {
        uint32_t index=rd32(bytes+i*4);
        if(index<previous||index>count)return NSF_RESOURCE_FORMAT;
        previous=index;
    }
    for(uint32_t i=0;i<count;i++) {
        uint32_t cid=rd32(bytes+0x520+i*8),eid=rd32(bytes+0x524+i*8);
        if(!(cid&1)||!(eid&1))return NSF_RESOURCE_FORMAT;
        for(uint32_t j=0;j<i;j++)if(rd32(bytes+0x524+j*8)==eid)return NSF_RESOURCE_FORMAT;
    }
    *out=(nsd_resource_index){.data=bytes,.size=(uint32_t)size,.count=count};return 0;
}
int nsd_resource_find(const nsd_resource_index *index,uint32_t eid,uint32_t *cid) {
    if(!index||!index->data||!cid)return NSF_RESOURCE_INVALID;
    for(uint32_t i=0;i<index->count;i++)if(rd32(index->data+0x524+i*8)==eid) {
        *cid=rd32(index->data+0x520+i*8);return 0;
    }
    return NSF_RESOURCE_MISSING;
}
static int entry_valid(const uint8_t *entry,uint32_t size) {
    if(size<20||rd32(entry)!=0x0100ffffu)return 0;
    uint32_t count=rd32(entry+12);
    if((uint64_t)20+(uint64_t)count*4>size)return 0;
    uint32_t previous=rd32(entry+16);
    if(previous<20+count*4||previous>size)return 0;
    for(uint32_t i=1;i<=count;i++) {
        uint32_t offset=rd32(entry+16+i*4);
        if(offset<previous||offset>size)return 0;
        previous=offset;
    }
    return 1;
}
int nsf_resource_parse_page(const void *data,size_t size,uint32_t cid,uint32_t eid,
                            nsf_resource_view *out) {
    if(!data||!out||out->leased||size!=NSF_PAGE_BYTES||!(cid&1))return NSF_RESOURCE_INVALID;
    const uint8_t *page=data;
    if(rd16(page)!=0x1234)return NSF_RESOURCE_FORMAT;
    uint32_t type=rd16(page+2);
    nsf_resource_view next={.page=page,.eid=eid,.page_type=type};
    if(type==1) {
        if(rd32(page+4)!=eid)return NSF_RESOURCE_MISSING;
        *out=next;return 0;
    }
    if((type!=0&&type!=3&&type!=4&&type!=5)||rd32(page+4)!=cid)return NSF_RESOURCE_FORMAT;
    uint32_t count=rd32(page+8);
    if(count>(NSF_PAGE_BYTES-20)/4)return NSF_RESOURCE_FORMAT;
    uint32_t start=rd32(page+16),found=0;
    if(start<20+count*4||start>NSF_PAGE_BYTES)return NSF_RESOURCE_FORMAT;
    for(uint32_t i=0;i<count;i++) {
        uint32_t end=rd32(page+20+i*4);
        if(end<start||end>NSF_PAGE_BYTES||!entry_valid(page+start,end-start))return NSF_RESOURCE_FORMAT;
        const uint8_t *entry=page+start;uint32_t entry_type=rd32(entry+8);
        if((type==3&&entry_type!=12)||(type==4&&entry_type!=14)||(type==5&&entry_type!=20))
            return NSF_RESOURCE_FORMAT;
        if(rd32(entry+4)==eid) {
            if(found)return NSF_RESOURCE_FORMAT;
            found=1;next.entry=entry;next.entry_size=end-start;
            next.entry_type=entry_type;next.item_count=rd32(entry+12);
        }
        start=end;
    }
    if(!found)return NSF_RESOURCE_MISSING;
    *out=next;return 0;
}
int nsf_resource_item(const nsf_resource_view *view,uint32_t item,const uint8_t **data,uint32_t *size) {
    if(!view||!view->page||!data||!size)return NSF_RESOURCE_INVALID;
    if(view->page_type==1)return NSF_RESOURCE_TEXTURE;
    if(!view->entry||item>=view->item_count)return NSF_RESOURCE_INVALID;
    uint32_t start=rd32(view->entry+16+item*4),end=rd32(view->entry+20+item*4);
    if(start>end||end>view->entry_size)return NSF_RESOURCE_FORMAT;
    *data=view->entry+start;*size=end-start;return 0;
}
int nsf_resource_open_raw(const char *path,const void *nsd,size_t size) {
    if(opened)return NSF_RESOURCE_INVALID;
    nsd_resource_index next;
    int error=nsd_resource_bind(&next,nsd,size);if(error)return error;
    error=nsf_stream_open(path);if(error)return error;
    active_index=next;opened=1;return 0;
}
int nsf_resource_request(uint32_t eid,nsf_resource_ticket *out) {
    if(!opened||!out)return NSF_RESOURCE_INVALID;
    uint32_t cid;int error=nsd_resource_find(&active_index,eid,&cid);if(error)return error;
    nsf_resource_ticket next={.eid=eid,.cid=cid};
    error=nsf_stream_request((uint64_t)(cid>>1)*NSF_PAGE_BYTES,NSF_PAGE_BYTES,&next.page);
    if(error)return error;
    *out=next;return 0;
}
int nsf_resource_poll(nsf_resource_ticket ticket,nsf_stream_state *state,int *error) {
    if(!opened)return NSF_RESOURCE_INVALID;
    return nsf_stream_poll(ticket.page,state,error);
}
int nsf_resource_acquire(nsf_resource_ticket ticket,nsf_resource_view *out) {
    if(!opened||!out||out->leased)return NSF_RESOURCE_INVALID;
    const uint8_t *page;int error=nsf_stream_acquire(ticket.page,&page);if(error)return error;
    nsf_resource_view next={0};
    error=nsf_resource_parse_page(page,NSF_PAGE_BYTES,ticket.cid,ticket.eid,&next);
    if(error){nsf_stream_release(ticket.page);return error;}
    next.ticket=ticket.page;next.leased=1;*out=next;return 0;
}
int nsf_resource_release_view(nsf_resource_view *view) {
    if(!opened||!view||!view->leased)return NSF_RESOURCE_INVALID;
    int error=nsf_stream_release(view->ticket);
    if(!error)memset(view,0,sizeof(*view));
    return error;
}
int nsf_resource_release_ticket(nsf_resource_ticket ticket){return opened?nsf_stream_release(ticket.page):NSF_RESOURCE_INVALID;}
int nsf_resource_cancel(nsf_resource_ticket ticket){return opened?nsf_stream_cancel(ticket.page):NSF_RESOURCE_INVALID;}
int nsf_resource_close(void) {
    if(!opened)return 0;
    int error=nsf_stream_close();if(error)return error;
    memset(&active_index,0,sizeof(active_index));opened=0;return 0;
}
