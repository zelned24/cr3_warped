"""Compare native bounded ZDAT views with every original placement/database."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import cb3
import zdat

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "zdat_runtime.h"
static uint8_t storage[65536],nsd[65536],indices[65536];
static zdat_span items[16384];
static uint32_t number(FILE *f){uint32_t n;assert(fread(&n,4,1,f)==1);return n;}
static int64_t wide(FILE *f){int64_t n;assert(fread(&n,8,1,f)==1);return n;}
static int32_t signed_number(FILE *f){uint32_t n=number(f);return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);}
static void span(FILE *f,uint8_t *data,uint32_t *size){*size=number(f);assert(*size<=65536);assert(fread(data,1,*size,f)==*size);}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static void overflow_cases(void){
 uint8_t vector[40]={0};put32(vector,40);vector[12]=1;
 vector[16]=0x4b;vector[18]=12;vector[20]=7;vector[21]=12;vector[22]=1;vector[24]=1;
 zdat_database db;assert(zdat_database_bind(&db,(zdat_span){vector,sizeof(vector)})==0);
 zdat_entity entity={.shift=30};assert(zdat_series_at(&db,0,&entity.path)==0);
 zdat_zone zone={0};int32_t raw[3]={11,22,33},raw_before[3]={11,22,33};
 int64_t reference[3]={44,55,66},reference_before[3]={44,55,66};
 put32(vector+32,INT32_MAX);
 assert(zdat_entity_position(&zone,&entity,0,raw,reference)==ZDAT_RANGE);
 assert(memcmp(raw,raw_before,sizeof(raw))==0&&memcmp(reference,reference_before,sizeof(reference))==0);
 put32(vector+32,0x80000000u);
 assert(zdat_entity_position(&zone,&entity,0,raw,reference)==ZDAT_RANGE);
 assert(memcmp(raw,raw_before,sizeof(raw))==0&&memcmp(reference,reference_before,sizeof(reference))==0);
}
int main(int argc,char **argv){
 overflow_cases();
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
 unsigned zones=0,entities=0,series_count=0,values=0,points=0;
 uint32_t levels=number(f);
 for(uint32_t level=0;level<levels;level++){
  uint32_t nsd_size;span(f,nsd,&nsd_size);uint32_t count=number(f);
  for(uint32_t zone=0;zone<count;zone++){
   uint32_t item_count=number(f),used=0;assert(item_count<=16384);
   for(uint32_t i=0;i<item_count;i++){
    uint32_t size=number(f);assert(size<=65536-used);
    assert(fread(storage+used,1,size,f)==size);items[i]=(zdat_span){storage+used,size};used+=size;
   }
   zdat_zone z={0};assert(zdat_zone_bind(&z,items,item_count)==0);
   assert(z.zone_index==number(f)&&z.sky_eid==number(f));
   assert(z.info_count==number(f)&&z.database_count==number(f)&&z.entity_count==number(f));
   assert(z.other_count==number(f)&&z.has_offset==(int)number(f));
   for(unsigned i=0;i<6;i++)assert(z.world_eids[i]==number(f));
   for(unsigned i=0;i<3;i++)assert(z.offset[i]==signed_number(f));
   for(uint32_t i=0;i<z.other_count;i++){uint32_t eid;assert(zdat_zone_other_eid(&z,i,&eid)==0&&eid==number(f));}
   zdat_zone before=z;
   assert(zdat_zone_bind(&z,items,item_count-1)!=0&&memcmp(&z,&before,sizeof(z))==0);
   uint8_t info_saved[4];memcpy(info_saved,storage+0x184,4);put32(storage+0x184,3);
   assert(zdat_zone_bind(&z,items,item_count)!=0&&memcmp(&z,&before,sizeof(z))==0);memcpy(storage+0x184,info_saved,4);
   uint8_t other_saved[4];memcpy(other_saved,storage+0x190,4);memset(storage+0x190,0xff,4);
   assert(zdat_zone_bind(&z,items,item_count)!=0&&memcmp(&z,&before,sizeof(z))==0);memcpy(storage+0x190,other_saved,4);
   for(uint32_t d=0;d<z.database_count+z.entity_count;d++){
    zdat_database db;assert(zdat_database_bind(&db,items[2+d])==0&&db.count==number(f));
    for(uint32_t i=0;i<db.count;i++){
     zdat_series s,found;assert(zdat_series_at(&db,i,&s)==0);
     assert(s.key==number(f)&&s.format==number(f)&&s.stride==number(f)&&s.count==number(f));
     assert(zdat_series_find(&db,s.key,&found)==0&&found.values_offset==s.values_offset);
     for(uint32_t value=0;value<s.count;value++){
      uint32_t length;uint16_t time;
      assert(zdat_series_length(&s,value,&length,&time)==0&&length==number(f)&&time==number(f));
      for(uint32_t element=0;element<length;element++){
       int32_t data[3];uint32_t n;assert(zdat_series_value(&s,value,element,data,&n)==0&&n==number(f));
       for(uint32_t j=0;j<n;j++)assert(data[j]==signed_number(f));values++;
      }
      int32_t rejected[3]={123,456,789},prior[3]={123,456,789};uint32_t n=99;
      assert(zdat_series_value(&s,value,length,rejected,&n)!=0&&n==99&&memcmp(prior,rejected,sizeof(prior))==0);
     }
     series_count++;
    }
    /* Malformed size, series directory, data offset and stride cannot publish. */
    zdat_database unchanged=db;uint8_t *b=(uint8_t *)items[2+d].data;uint8_t saved[4];
    memcpy(saved,b,4);memset(b,0xff,4);
    assert(zdat_database_bind(&db,items[2+d])!=0&&memcmp(&db,&unchanged,sizeof(db))==0);memcpy(b,saved,4);
    memcpy(saved,b+12,2);memset(b+12,0xff,2);
    assert(zdat_database_bind(&db,items[2+d])!=0&&memcmp(&db,&unchanged,sizeof(db))==0);memcpy(b+12,saved,2);
    if(db.count){
     memcpy(saved,b+18,2);memset(b+18,0xff,2);
     assert(zdat_database_bind(&db,items[2+d])!=0&&memcmp(&db,&unchanged,sizeof(db))==0);memcpy(b+18,saved,2);
     uint8_t stride=b[21];b[21]=0;
     assert(zdat_database_bind(&db,items[2+d])!=0&&memcmp(&db,&unchanged,sizeof(db))==0);b[21]=stride;
    }
   }
   for(uint32_t i=0;i<z.entity_count;i++){
    zdat_entity e;assert(zdat_zone_entity(&z,i,&e)==0&&e.present==number(f));
    assert(e.file_index==signed_number(f)&&e.object_id==signed_number(f)&&e.lookup_index==signed_number(f));
    assert(e.geometry_format==signed_number(f)&&e.shift==signed_number(f));
    uint32_t eid;assert(zdat_class_eid((zdat_span){nsd,nsd_size},e.file_index,&eid)==0&&eid==number(f));
    uint32_t paths=number(f);
    for(uint32_t p=0;p<paths;p++){
     int32_t raw[3];int64_t reference[3];assert(zdat_entity_position(&z,&e,p,raw,reference)==0);
     for(unsigned j=0;j<3;j++)assert(raw[j]==signed_number(f));
     for(unsigned j=0;j<3;j++)assert(reference[j]==wide(f));points++;
    }
    uint32_t index_size;span(f,indices,&index_size);
    uint32_t signals=number(f),states=number(f);int expected=signed_number(f);uint32_t wanted=number(f),state=0xa55a;
    int result=zdat_entity_state(&e,(zdat_span){indices,index_size},signals,states,&state);
    assert(result==expected);if(!result)assert(state==wanted);else assert(state==0xa55a);
    entities++;
   }
   zones++;
  }
 }
 assert(fgetc(f)==EOF&&fclose(f)==0);
 printf("PASS ZDAT: %u original zones, %u entities, %u series, %u elements, %u path points; malformed outputs preserved\n",zones,entities,series_count,values,points);
 return 0;
}
'''


def number(out, *values):
    out.write(struct.pack('<' + 'I' * len(values), *(value & 0xffffffff for value in values)))


def span(out, data):
    number(out, len(data))
    out.write(data)


def fixture(out):
    levels = cb3.all_level_ids()
    number(out, len(levels))
    for level in levels:
        archive = cb3.load_level(level, cache=False)
        nsd = bytes(archive.nsd.buf)
        span(out, nsd)
        entries = archive.of_type(7)
        number(out, len(entries))
        classes = zdat.class_names_from_nsd(archive.nsd)
        for entry in entries:
            zone = zdat.parse_zdat(entry, classes, archive)
            number(out, len(entry.items))
            for item in entry.items:
                span(out, item)
            number(out, zone.zone_index, zone.sky_wgeo_eid, zone.info_count,
                   zone.db_count, zone.object_count, zone.other_name_count,
                   int(zone.offset_vector is not None))
            number(out, *(struct.unpack_from('<I', entry.items[0], 0x34 + i * 0x30)[0] for i in range(6)))
            number(out, *(zone.offset_vector or (0, 0, 0)))
            number(out, *(struct.unpack_from('<I', entry.items[0], 0x194 + i * 4)[0] for i in range(zone.other_name_count)))
            for database in zone.databases + [entity.db for entity in zone.objects]:
                number(out, len(database.series))
                for series in database.series:
                    number(out, series.key, series.format, series.stride, series.count)
                    for index, value in enumerate(series.values):
                        number(out, len(value), series.times[index] if series.times else 0)
                        for element in value:
                            components = element if isinstance(element, tuple) else (element,)
                            number(out, len(components), *components)
            for entity in zone.objects:
                present = sum(flag for flag, value in [(1, entity.file_index), (2, entity.object_id),
                              (4, entity.lookup_index), (8, entity.geometry_format)] if value is not None)
                number(out, present, entity.file_index or 0, entity.object_id or 0,
                       entity.lookup_index or 0, entity.geometry_format or 0, entity.path_scale_shift)
                class_base = 0x520 + 8 * archive.nsd.num_list_entries + 0x10
                number(out, struct.unpack_from('<I', nsd, class_base + entity.file_index * 4)[0], len(entity.path))
                for point, position in zip(entity.path, entity.positions):
                    number(out, *point)
                    out.write(struct.pack('<3q', *position))
                gool = archive.entry(entity.file_name)
                assert gool is not None and gool.type == 11
                table = zdat.parse_gool_table(gool)
                span(out, gool.items[3])
                signals = struct.unpack_from('<I', gool.items[0], 16)[0]
                mapped = table.object_indices[entity.object_id]
                expected = 1 if mapped < 0 or mapped == 255 else (0 if mapped < len(table.states) else -63)
                number(out, signals, len(table.states), expected, mapped if expected == 0 else 0)


def main():
    with tempfile.TemporaryDirectory(prefix='crash-zdat-') as directory:
        folder = Path(directory)
        source = folder / 'test.c'
        source.write_text(HARNESS)
        data = folder / 'original.bin'
        with data.open('wb') as out:
            fixture(out)
        output = folder / 'zdat.exe'
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-O3', '-UNDEBUG',
                        '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'source'), str(source),
                        str(ROOT / 'source/zdat_runtime.c'), '-o', str(output)], check=True)
        subprocess.run([str(output), str(data)], check=True, timeout=90)


if __name__ == '__main__':
    main()
