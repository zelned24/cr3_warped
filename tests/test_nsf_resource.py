"""Verify original NSD/NSF resource identity and real threaded page leases."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cb3
from audio_assets import parse_audio_chunk
from test_nsf_stream import SDK

HARNESS=r'''
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "nsf_resource.h"
int test_allocations,test_cache_failure,test_thread_failure,test_alloc_failure;
DWORD test_worker_id;
static uint8_t page[65536],nsd[65536];
static uint32_t number(FILE *f){uint32_t v;assert(fread(&v,4,1,f)==1);return v;}
static uint32_t crc(const uint8_t *data,size_t size){
 uint32_t c=0xffffffffu;
 for(size_t i=0;i<size;i++){c^=data[i];for(unsigned n=0;n<8;n++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}
 return ~c;
}
static void wait_for(nsf_resource_ticket ticket){
 for(unsigned i=0;i<10000;i++){
  nsf_stream_state state;int error;
  assert(nsf_resource_poll(ticket,&state,&error)==0);
  if(state==NSF_STREAM_READY){assert(!error);return;}
  assert(state==NSF_STREAM_QUEUED||state==NSF_STREAM_READING);Sleep(1);
 }
 assert(!"resource worker timeout");
}
int main(int argc,char **argv){
 assert(argc>1&&(argc-1)%3==0);unsigned entries=0,items=0,textures=0;
 for(int arg=1;arg<argc;arg+=3){
  FILE *file=fopen(argv[arg],"rb"),*index_file=fopen(argv[arg+1],"rb"),*manifest=fopen(argv[arg+2],"rb");
  assert(file&&index_file&&manifest);
  size_t nsd_size=fread(nsd,1,sizeof(nsd),index_file);assert(fclose(index_file)==0);
  nsd_resource_index index={0};assert(nsd_resource_bind(&index,nsd,nsd_size)==0);
  nsd_resource_index previous=index;
  uint32_t save;memcpy(&save,nsd+0x404,4);memset(nsd+0x404,0xff,4);
  assert(nsd_resource_bind(&index,nsd,nsd_size)==NSF_RESOURCE_FORMAT);
  assert(memcmp(&index,&previous,sizeof(index))==0);memcpy(nsd+0x404,&save,4);
  if(index.count>1){
   uint32_t duplicate;memcpy(&duplicate,nsd+0x52c,4);
   memcpy(nsd+0x52c,nsd+0x524,4);
   assert(nsd_resource_bind(&index,nsd,nsd_size)==NSF_RESOURCE_FORMAT);
   assert(memcmp(&index,&previous,sizeof(index))==0);memcpy(nsd+0x52c,&duplicate,4);
  }
  uint32_t count=number(manifest);assert(count==index.count);
  uint32_t sample_eid=0,sample_size=0,sample_crc=0,texture_eid=0;
  for(unsigned entry=0;entry<count;entry++){
   uint32_t eid=number(manifest),cid=number(manifest),kind=number(manifest),n=number(manifest),resolved=0;
   assert(nsd_resource_find(&index,eid,&resolved)==0&&resolved==cid);
   assert(fseek(file,(long)(cid>>1)*65536,SEEK_SET)==0&&fread(page,1,sizeof(page),file)==sizeof(page));
   nsf_resource_view view={0};assert(nsf_resource_parse_page(page,sizeof(page),cid,eid,&view)==0);
   assert(view.eid==eid&&view.entry_type==kind&&view.item_count==n);
   if(view.page_type!=1&&entry==0){
    uint32_t saved_count;memcpy(&saved_count,page+8,4);memset(page+8,0xff,4);
    nsf_resource_view rejected={.eid=0xa55a},before=rejected;
    assert(nsf_resource_parse_page(page,sizeof(page),cid,eid,&rejected)==NSF_RESOURCE_FORMAT);
    assert(memcmp(&rejected,&before,sizeof(before))==0);memcpy(page+8,&saved_count,4);
   }
   if(view.page_type==1){assert(kind==0&&n==0);assert(crc(page,sizeof(page))==number(manifest));textures++;if(!texture_eid)texture_eid=eid;}
   for(unsigned item=0;item<n;item++){
    uint32_t expected_size=number(manifest),expected_crc=number(manifest),size;const uint8_t *data;
    assert(nsf_resource_item(&view,item,&data,&size)==0&&size==expected_size);
    assert(crc(data,size)==expected_crc);items++;
    if(kind==12&&n==1&&!sample_eid){sample_eid=eid;sample_size=size;sample_crc=expected_crc;}
   }
   entries++;
   /* A wrong physical CID or identity cannot publish an unrelated resource. */
   nsf_resource_view rejected={0};
   int result=nsf_resource_parse_page(page,sizeof(page),cid+2,eid,&rejected);
   assert(view.page_type==1?result==0:result==NSF_RESOURCE_FORMAT);
  }
  assert(fgetc(manifest)==EOF&&fclose(manifest)==0&&fclose(file)==0);
  /* End-to-end asynchronous EID lookup: no whole NSF allocation. */
  assert(nsf_resource_open_raw(argv[arg],nsd,nsd_size)==0);
  nsf_resource_ticket ticket;
  uint32_t eid;memcpy(&eid,nsd+0x524,4);
  assert(nsf_resource_request(eid,&ticket)==0);wait_for(ticket);
  nsf_resource_view view={0};assert(nsf_resource_acquire(ticket,&view)==0&&view.leased);
  assert(nsf_resource_acquire(ticket,&view)==NSF_RESOURCE_INVALID);
  assert(nsf_resource_release_ticket(ticket)==0);
  nsf_resource_view audio={0},texture={0};
  if(sample_eid){
   nsf_resource_ticket sound;assert(nsf_resource_request(sample_eid,&sound)==0);wait_for(sound);
   assert(nsf_resource_acquire(sound,&audio)==0&&audio.entry_type==12);
   assert(nsf_resource_release_ticket(sound)==0);
  }
  if(texture_eid){
   nsf_resource_ticket tex;assert(nsf_resource_request(texture_eid,&tex)==0);wait_for(tex);
   assert(nsf_resource_acquire(tex,&texture)==0&&texture.page_type==1);
   assert(nsf_resource_release_ticket(tex)==0);
  }
  const uint8_t *borrowed=view.page;uint32_t signature=crc(borrowed,65536);
  assert(nsf_resource_close()==NSF_STREAM_BUSY);
  assert(crc(borrowed,65536)==signature);
  if(audio.leased){
   const uint8_t *data;uint32_t size;
   assert(nsf_resource_item(&audio,0,&data,&size)==0&&size==sample_size&&crc(data,size)==sample_crc);
   assert(nsf_resource_release_view(&audio)==0);
  }
  if(texture.leased)assert(nsf_resource_release_view(&texture)==0);
  assert(nsf_resource_release_view(&view)==0&&!view.page&&!view.leased);
  assert(nsf_resource_release_view(&view)==NSF_RESOURCE_INVALID);
  assert(nsf_resource_close()==0&&test_allocations==0);
  assert(nsf_resource_poll(ticket,NULL,NULL)==NSF_RESOURCE_INVALID);
 }
 printf("PASS resources: %u original NSD entries, %u exact items, %u texture pages;42 raw sessions and retained asynchronous leases\n",entries,items,textures);
 return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='crash-resources-') as directory:
        folder=Path(directory);(folder/'3ds.h').write_text(SDK);(folder/'test.c').write_text(HARNESS)
        arguments=[]
        for lid in cb3.all_level_ids():
            archive=cb3.load_level(lid,cache=False)
            records=bytearray(struct.pack('<I',len(archive.nsd.list_entries)))
            for cid,eid in archive.nsd.list_entries:
                page=archive.chunks[cid>>1]
                if page.ctype==1:
                    assert struct.unpack_from('<I',page.data,4)[0]==eid
                    records+=struct.pack('<5I',eid,cid,0,0,zlib.crc32(page.data))
                else:
                    candidates=page.entries if page.ctype==0 else parse_audio_chunk(page.data)
                    matches=[e for e in candidates if e.eid==eid]
                    assert len(matches)==1,(lid,cid,eid)
                    entry=matches[0]
                    records+=struct.pack('<4I',eid,cid,entry.type,len(entry.items))
                    for item in entry.items:records+=struct.pack('<2I',len(item),zlib.crc32(item))
            manifest=folder/f'level_{lid:02x}.bin';manifest.write_bytes(records)
            arguments.extend([str(archive.nsf_path),str(archive.nsd_path),str(manifest)])
        output=folder/'resources.exe'
        subprocess.run([sys.executable,'-m','ziglang','cc','-std=c11','-O3','-UNDEBUG','-Wall','-Wextra','-Werror',
            '-I'+str(folder),'-I'+str(ROOT/'source'),str(folder/'test.c'),
            *[str(ROOT/'source'/f) for f in ('nsf_resource.c','nsf_stream.c','nsf_page.c')],
            '-o',str(output)],check=True)
        subprocess.run([str(output)]+arguments,check=True,timeout=90)


if __name__=='__main__':main()
