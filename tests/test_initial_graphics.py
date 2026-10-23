"""Pure Python GOOL analysis regressions; no target or host C compilation."""
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import initial_graphics as graphics


def store(op,source=0xb80,destination=0xe2a):
    return op<<24 | source<<12 | destination


class InitialGraphics(unittest.TestCase):
    def run_script(self,instructions,pre_run=None,state_index=0,budget=4096):
        entry=SimpleNamespace(name='BaseC',type=11,items=[b'',struct.pack('<%dI'%len(instructions),*instructions),bytes(8)])
        archive=SimpleNamespace(entry=lambda name:entry if name=='BaseC' else None)
        state=SimpleNamespace(script=0,alt_name='BaseC',pre_run=pre_run)
        models=[SimpleNamespace(offset=offset,as_dict=lambda offset=offset:{'kind':'MESH','geo':str(offset)}) for offset in (0,20)]
        table=SimpleNamespace(states=[state],models=models)
        entity=SimpleNamespace(name='fixture',file_name='BaseC',object_id=0,state_index=state_index,state=state)
        with patch.object(graphics.zdat,'parse_gool_table',return_value=table):
            return graphics.audit(archive,entity,budget)

    def test_explicit_model(self):
        result=self.run_script([0x38000000])
        self.assertEqual(result['resolution'],'unique_first_assignment')
        self.assertEqual(result['assignments'][0]['offset'],0)

    def test_null_geometry_then_explicit_model(self):
        for destination in (0xe2a,0xc2a):
            with self.subTest(destination=hex(destination)):
                result=self.run_script([store(0x11,destination=destination),0x38000000])
                self.assertEqual(result['resolution'],'unique_first_assignment')

    def test_constant_geometry_pointer_and_literal_scaling(self):
        # Literal8 0x50 represents 80*16; STORE_GEO converts this to offset 20.
        result=self.run_script([store(0x27,source=0xa50)])
        self.assertEqual(result['resolution'],'unique_first_assignment')
        self.assertEqual(result['assignments'][0]['offset'],20)
        result=self.run_script([store(0x27,source=0xe25),0x38000000])
        self.assertIn('dynamic_geometry_operand',result['stops'])

    def test_all_geometry_store_variants_are_seen(self):
        for opcode in (0x12,0x14,0x17,0x19,0x29,0x2b,0x2c):
            for destination in (0xe2a,0xc2a):
                with self.subTest(opcode=hex(opcode),destination=hex(destination)):
                    result=self.run_script([store(opcode,destination=destination),0x38000000])
                    self.assertEqual(result['resolution'],'unresolved')
                    self.assertIn('unresolved_geometry_store',result['stops'])

    def test_indirect_other_can_alias_self(self):
        result=self.run_script([store(0x11,destination=0xc6a),0x38000000])
        self.assertIn('possibly_aliased_geometry_store',result['stops'])
        self.assertEqual(result['resolution'],'unresolved')

    def test_script_store_to_geometry(self):
        result=self.run_script([0x18000000 | 0x2a<<18,0x38000000])
        self.assertIn('unresolved_geometry_store',result['stops'])

    def test_script_pointer_writes_stop_control_flow(self):
        for source in graphics.CONTROL_SOURCES:
            with self.subTest(source=source):
                result=self.run_script([store(0x11,destination=0xe00|source),0x38000000])
                self.assertIn('unresolved_control_store',result['stops'])

    def test_other_member_writes_can_continue(self):
        result=self.run_script([store(0x12,destination=0xe25),0x38000000])
        self.assertEqual(result['resolution'],'unique_first_assignment')

    def test_unaudited_pre_run_prevents_unique_claim(self):
        result=self.run_script([0x38000000],pre_run=3)
        self.assertEqual(result['resolution'],'candidates_only')
        self.assertIn('pre_run_not_audited',result['stops'])

    def test_native_children_and_unknown_side_effects_stop(self):
        for opcode in (0x1c,0x2e,0x3a,0x3f,0x40,0x46,0x47,0x49,0x50,0x51):
            with self.subTest(opcode=hex(opcode)):
                result=self.run_script([opcode<<24,0x38000000])
                self.assertEqual(result['resolution'],'unresolved')
                self.assertFalse(result['assignments'])

    def test_branch_with_native_path_is_not_unique(self):
        result=self.run_script([0x33000001,0x38000000,0x49000000])
        self.assertEqual(result['resolution'],'candidates_only')
        self.assertIn('native_mips',result['stops'])

    def test_loop_bounds_and_budget(self):
        for instructions in ([0x320003ff],[0x32000040]):
            self.assertEqual(self.run_script(instructions)['resolution'],'unresolved')
        self.assertIn('instruction_budget',self.run_script([0x2f000000,0x38000000],budget=1)['stops'])
        self.assertEqual(self.run_script([0x38000000],state_index=-1)['resolution'],'unresolved')


if __name__=='__main__':unittest.main()
