"""Exercise real async NSF-to-NDSP ownership and exact original sample PCM."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import cb3
from audio_assets import audio_entries
from psx_adpcm import decode_stream
from test_nsf_stream import SDK

HARNESS = r'''
#include <stdio.h>
#include <string.h>
#include "audio_resource.h"
int test_allocations,test_cache_failure,test_thread_failure,test_alloc_failure;
DWORD test_worker_id;
static ndspWaveBuf *queues[24][3];
static unsigned queued[24];
static int live;
static int16_t pcm[65536];
static size_t pcm_count;
Result ndspInit(void){live=1;return 0;}
void ndspExit(void){for(unsigned c=0;c<24;c++)assert(!queued[c]);live=0;}
void ndspSetOutputMode(int mode){(void)mode;assert(live);}
void ndspChnWaveBufClear(int c){assert(live);for(unsigned i=0;i<queued[c];i++)queues[c][i]->status=NDSP_WBUF_FREE;queued[c]=0;}
void ndspChnReset(int c){assert(live&&!queued[c]);}
void ndspChnSetFormat(int c,int format){(void)c;assert(live&&format==NDSP_FORMAT_MONO_PCM16);}
void ndspChnSetRate(int c,float rate){(void)c;assert(live&&rate>0);}
void ndspChnSetMix(int c,float mix[12]){(void)c;assert(live&&mix[0]>=0&&mix[1]>=0);}
void ndspChnSetPaused(int c,bool pause){(void)c;(void)pause;assert(live);}
void DSP_FlushDataCache(const void *p,uint32_t n){assert(live&&p&&n);}
void ndspChnWaveBufAdd(int c,ndspWaveBuf *wave){
 assert(live&&queued[c]<3&&wave->status==NDSP_WBUF_FREE);
 for(unsigned i=0;i<queued[c];i++)assert(queues[c][i]!=wave);
 queues[c][queued[c]++]=wave;wave->status=NDSP_WBUF_QUEUED;
}
static void consume(int c){
 if(!queued[c])return;
 ndspWaveBuf *wave=queues[c][0];assert(pcm_count+wave->nsamples<=65536);
 memcpy(pcm+pcm_count,wave->data_vaddr,wave->nsamples*2);pcm_count+=wave->nsamples;
 for(unsigned i=1;i<queued[c];i++)queues[c][i-1]=queues[c][i];
 queued[c]--;wave->status=NDSP_WBUF_DONE;
}
static int ready(AudioResource *slot){
 for(unsigned i=0;i<10000;i++){
  int state=audio_resource_poll(slot);
  if(state!=AUDIO_RESOURCE_LOADING)return state;
  Sleep(1);
 }
 assert(!"audio resource timeout");return -99;
}
static uint32_t word(FILE *f){uint32_t n;assert(fread(&n,4,1,f)==1);return n;}
int main(int argc,char **argv){
 assert(argc==5);uint8_t nsd[65536];FILE *f=fopen(argv[2],"rb");assert(f);
 size_t bytes=fread(nsd,1,sizeof(nsd),f);assert(!fclose(f));
 f=fopen(argv[3],"rb");assert(f);uint32_t count=word(f),eid[3],sizes[3];
 int16_t expected[3][65536];assert(count==3);
 for(unsigned i=0;i<count;i++){eid[i]=word(f);sizes[i]=word(f);assert(sizes[i]<=sizeof(expected[i]));assert(fread(expected[i],1,sizes[i],f)==sizes[i]);}
 assert(!fclose(f));uint32_t other=(uint32_t)strtoul(argv[4],NULL,0);
 assert(ndspInit()==0);AudioResource slot={0};
 test_alloc_failure=1;assert(audio_resource_init(&slot,4)==AUDIO_STREAM_NO_MEMORY);
 test_alloc_failure=0;assert(!audio_resource_init(&slot,4)&&test_allocations==1);
 assert(audio_resource_init(&slot,5)==AUDIO_RESOURCE_BUSY);
 AudioResourceControls controls={.external_loop_byte=AUDIO_STREAM_NO_LOOP,.pitch=0x1000,.left_volume=0x3fff,.right_volume=0x2000};
 assert(!nsf_resource_open_raw(argv[1],nsd,bytes)&&test_allocations==2);
 for(unsigned i=0;i<count;i++){
  pcm_count=0;assert(!audio_resource_begin(&slot,eid[i],&controls));
  nsf_resource_ticket before=slot.ticket;
  assert(audio_resource_begin(&slot,eid[(i+1)%count],&controls)==AUDIO_RESOURCE_BUSY);
  assert(slot.ticket.page.generation==before.page.generation&&slot.eid==eid[i]);
  assert(audio_resource_release_after_ndsp_exit(&slot)==AUDIO_RESOURCE_BUSY);
  assert(ready(&slot)==AUDIO_RESOURCE_PLAYING&&slot.view.leased&&!slot.has_ticket);
  for(unsigned step=0;step<1000&&slot.state==AUDIO_RESOURCE_PLAYING;step++){
   consume(4);assert(audio_stream_poll_all()>=0);assert(audio_resource_poll(&slot)>=0);
  }
  assert(slot.state==AUDIO_RESOURCE_IDLE&&!slot.view.leased&&!queued[4]);
  assert(pcm_count*2==sizes[i]&&!memcmp(pcm,expected[i],sizes[i]));
 }
 assert(!audio_resource_begin(&slot,other,&controls));assert(ready(&slot)==AUDIO_RESOURCE_FORMAT);
 assert(!slot.has_ticket&&!slot.view.leased&&audio_resource_poll(&slot)==AUDIO_RESOURCE_FORMAT);
 assert(!audio_resource_stop(&slot));
 /* All four page slots are shared with other consumers. A full pool must not
  * allocate around the limit or leave a half-started audio owner. */
 nsf_resource_ticket tickets[4];
 for(unsigned i=0;i<4;i++)assert(!nsf_resource_request(eid[0],&tickets[i]));
 assert(audio_resource_begin(&slot,eid[0],&controls)==NSF_STREAM_FULL);
 assert(slot.state==AUDIO_RESOURCE_IDLE&&!slot.has_ticket);
 for(unsigned i=0;i<4;i++){assert(!nsf_resource_cancel(tickets[i]));assert(!nsf_resource_release_ticket(tickets[i]));}
 assert(!nsf_resource_close());assert(!nsf_resource_open_raw(argv[1],nsd,bytes));
 /* Cancel while the worker may still own the file read. */
 assert(!audio_resource_begin(&slot,eid[0],&controls));assert(!audio_resource_stop(&slot));
 assert(!nsf_resource_close());assert(!nsf_resource_open_raw(argv[1],nsd,bytes));
 assert(!audio_resource_begin(&slot,eid[0],&controls));assert(ready(&slot)==AUDIO_RESOURCE_PLAYING);
 const uint8_t *source=slot.stream.source;uint8_t first=source[0];
 assert(nsf_resource_close()==NSF_STREAM_BUSY);assert(source[0]==first&&slot.view.leased);
 assert(!audio_resource_stop(&slot));assert(!nsf_resource_close()&&test_allocations==1);
 /* Missing files fail through the real worker; cleanup permits another session. */
 assert(!nsf_resource_open_raw("missing-audio-resource.nsf",nsd,bytes));
 assert(!audio_resource_begin(&slot,eid[0],&controls));assert(ready(&slot)==NSF_STREAM_IO);
 assert(!slot.has_ticket&&!slot.view.leased);assert(!audio_resource_stop(&slot));
 assert(!nsf_resource_close());ndspExit();assert(!audio_resource_release_after_ndsp_exit(&slot));
 assert(!test_allocations&&!slot.initialized);
 puts("PASS original NSF -> ADIO lease -> NDSP exact PCM; busy, failure, cancellation, drain and shutdown ownership");
 return 0;
}
'''


def main():
    archive = cb3.load_level(0x0b, cache=False)
    samples = {e.name: e for e in audio_entries(archive) if e.type == 12}
    chosen = [samples[n] for n in ('jmp0A', 'spn0A', 'tnt0A')]
    nsf, nsd = cb3.level_paths(0x0b)
    directory = Path(nsd).read_bytes()
    adio = {e.eid for e in audio_entries(archive) if e.type == 12}
    other = next(struct.unpack_from('<I', directory, 0x524 + i * 8)[0]
                 for i in range(struct.unpack_from('<I', directory, 0x404)[0])
                 if struct.unpack_from('<I', directory, 0x524 + i * 8)[0] not in adio)
    with tempfile.TemporaryDirectory(prefix='crash-audio-resource-') as folder:
        tmp = Path(folder)
        audio_sdk = (ROOT / 'tools/tests_audio/3ds.h').read_text()
        audio_sdk = '\n'.join(line for line in audio_sdk.splitlines()
                              if not line.startswith(('typedef int32_t Result;', '#define R_FAILED',
                                                      'void *linearAlloc(', 'void linearFree(')))
        audio_sdk += '\n#define NDSP_WBUF_QUEUED 1\n#define NDSP_WBUF_DONE 3\nvoid ndspChnSetPaused(int c,bool p);\n'
        (tmp / '3ds.h').write_text(SDK + audio_sdk)
        manifest = bytearray(struct.pack('<I', len(chosen)))
        for entry in chosen:
            pcm = decode_stream(entry.items[0]).pcm
            manifest += struct.pack('<II', entry.eid, len(pcm)) + pcm
        (tmp / 'samples.bin').write_bytes(manifest)
        (tmp / 'test.c').write_text(HARNESS)
        exe = tmp / 'audio_resource.exe'
        sources = ['audio.c', 'audio_stream.c', 'audio_resource.c',
                   'nsf_resource.c', 'nsf_stream.c', 'nsf_page.c']
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-O3', '-UNDEBUG',
                        '-Wall', '-Wextra', '-Werror', '-I' + str(tmp), '-I' + str(ROOT / 'source'),
                        str(tmp / 'test.c')] + [str(ROOT / 'source' / s) for s in sources]
                       + ['-lm', '-o', str(exe)], check=True)
        subprocess.run([str(exe), str(nsf), str(nsd), str(tmp / 'samples.bin'), str(other)], check=True)


if __name__ == '__main__':
    main()
