"""Pure Python verification of the data contract prepared for gool_data.c.

Does not compile/execute C. Compares preserved raw records with the established
Python parser and checks structural assumptions against every original entry.
"""
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cb3
import nsf
import zdat
import runtime_archive


class GoolDataContract(unittest.TestCase):
    def test_all_original_tables(self):
        occurrences=states=libraries=0
        missing=set()
        for lid in cb3.all_level_ids():
            arc=cb3.load_level(lid,cache=False)
            packed=runtime_archive.decode((ROOT/f'game_data/runtime/lv_{lid:02X}.cga').read_bytes())
            entries={e['eid']:e for e in packed['entries'] if e['type']==11}
            for entry in arc.of_type(11):
                occurrences+=1
                items=entries[entry.eid]['items']
                self.assertEqual(items,list(entry.items))
                self.assertIn(len(items),(3,6))
                self.assertGreaterEqual(len(items[0]),20)
                self.assertLess(items[0][5],32)
                self.assertEqual(len(items[1])%4,0)
                self.assertEqual(len(items[2])%4,0)
                if len(items)==3:
                    libraries+=1
                    continue
                table=zdat.parse_gool_table(entry)
                declared=struct.unpack_from('<I',items[0],16)[0]
                self.assertLessEqual(declared*2,len(items[3]))
                self.assertEqual(len(items[3])%2,0)
                self.assertEqual(len(items[4])%16,0)
                self.assertEqual(list(struct.unpack('<%dh'%((len(items[3])-2*declared)//2),items[3][2*declared:])),table.object_indices)
                self.assertEqual(len(table.states),len(items[4])//16)
                for i,state in enumerate(table.states):
                    states+=1
                    motion,interact,alt,on_signal,pre_run,script=struct.unpack_from('<II4H',items[4],i*16)
                    self.assertEqual((motion,interact),(state.motion_flags,state.interact_flags))
                    self.assertLess(alt,len(items[2])//4)
                    alternate=struct.unpack_from('<I',items[2],alt*4)[0]
                    self.assertEqual(nsf.eid_str(alternate),state.alt_name)
                    for raw,decoded in ((on_signal,state.on_signal),(pre_run,state.pre_run)):
                        self.assertEqual(None if raw==0x3fff else raw&0x7fff,decoded)
                    self.assertEqual(script&0x7fff,state.script)
                    for kind,raw in (('main',script),('pre',pre_run),('signal',on_signal)):
                        if kind!='main' and raw==0x3fff:continue
                        address=raw&0x7fff
                        owner=entries.get(alternate) if address&0x4000 else entries[entry.eid]
                        if owner is None:
                            missing.add((lid,entry.name,state.alt_name))
                            continue
                        self.assertLess(address&0x3fff,len(owner['items'][1])//4,
                                        (lid,entry.name,i,kind,hex(raw)))
        self.assertEqual(occurrences,557)
        self.assertEqual(libraries,39)
        self.assertEqual(states,15933)
        print(f'Contract checked: {occurrences} GOOL entries, {libraries} libraries, '
              f'{states} states; {len(missing)} missing alternate-owner links.')


if __name__=='__main__':unittest.main()
