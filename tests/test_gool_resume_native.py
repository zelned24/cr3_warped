"""Original RETURN and state-entry RESUME control on fixture RAM.

RETURN tests have no hooks. Full entry hooks loader/dispatcher explicitly;
dispatcher hook replays actual RETURN, not the whole GOOL script body.
"""
from pathlib import Path
import struct
import unittest
import test_gool_state as fixtures
from test_gool_stack_native import replay,MASK
from test_gool_jalr import call
from psx_native import Image,ORIGINAL_SHA256
from gool_vm import VM
import cb3


class NativeResume(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=Image.parse((Path(cb3.ISO)/'SCUS_942.44').read_bytes())
        if cls.image.sha256!=ORIGINAL_SHA256:raise AssertionError('wrong original executable')

    def compare_return(self,mode,argc,locals_,origin,nested=False,flags=None):
        vm=VM([0x2f000000,call(0xe25,0x800),0x31000000,0x31000000],
              args=tuple(range(argc)),members={0x25:0x5000000c},stack_start=origin)
        vm.invoke(0,1);vm.mode=mode;vm.respect_delay=False
        if nested:self.assertEqual(vm.step(),'running')
        else:vm.pc=2
        if flags is not None:vm.stack.words[vm.stack.frames[-1]]=flags
        for n in range(locals_):vm.stack.push(n+100)
        obj=0x80070000;start=obj+0x40+vm.stack.origin
        ram={start+4*n:value&MASK for n,value in enumerate(vm.stack.words[:vm.stack.top])}
        regs=replay(self.image,0x800399fc,{16:obj,17:vm.mode,
                    21:vm.pointer(0,vm.pc+1),22:start+4*vm.stack.top,
                    23:start+4*vm.stack.frames[-1]},ram,stops=(0x80038eac,0x80038e80))
        self.assertEqual(vm.step(),'running' if nested else 'resume_return')
        self.assertEqual(regs[22],start+4*vm.stack.top)
        self.assertEqual(regs[23],start+4*vm.stack.frames[-1])
        self.assertEqual(regs[21],vm.pointer(vm.owner,vm.pc));self.assertEqual(regs[17],vm.mode)
        self.assertEqual(vm.frame_yield,not nested)
        if not nested:self.assertEqual(ram[regs[22]-4],0);self.assertEqual(vm.stack.words[vm.frame_token_index],0)

    def test_72_resume_returns_match_original_zero_token_and_root_frame(self):
        for mode in (0x11,0x13,0x10013):
            for argc in (0,1,7,31):
                for local in (0,1,7):
                    for origin in (64,128):self.compare_return(mode,argc,local,origin)

    def test_12_nested_call_returns_continue_resume_without_early_token(self):
        for mode in (0x11,0x13,0x10013):
            for argc in (0,3):
                for local in (0,2):self.compare_return(mode,argc,local,64,True)

    def test_48_engine_returns_ignore_saved_flags_in_frame(self):
        for mode in (0x11,0x13,0x10013):
            for flags in (0,3,0x22,0x10000):
                for argc in (0,3):
                    for local in (0,1):self.compare_return(mode,argc,local,64,flags=flags)

    def test_16_native_state_entries_select_old_resume_before_current_pre(self):
        for old_owner in (0,1):
            for current in (False,True):
                for argc in (0,3):
                    for pre in (False,True):self.compare_entry(old_owner,current,argc,pre)

    def compare_entry(self,old_owner,current,argc,pre):
        i=fixtures.GoolState().make(pre=12 if pre else 0x3fff)
        i.vm.codes[0][4]=0x31000000;i.vm.codes[0][12]=0x31000000
        i.vm.codes[1][4]=0x31000000
        args=tuple(n*13-7 for n in range(argc));i.vm.set_tick(7)
        i.object.members[0x24]=i.vm.pointer(old_owner,4)
        self.assertEqual(i.enter_state(1,args),'new_state')
        self.assertFalse(i.vm.frame_yield);self.assertEqual(i.object.members[0x24],0)
        i.start_resume();self.assertEqual(i.vm.step(),'resume_return');i.resume_active=False
        if current and pre:
            i.vm.invoke(*i.vm.target(i.object.members[0x22]));i.vm.mode=3
            self.assertEqual(i.vm.step(),'running')
        obj=0x80070000;base=0x80090000;old_alt=0x80091000;new_alt=0x80092000
        base_code=0x80094000;old_code=0x80095000;new_code=0x80096000
        header=0x80097000;constants=0x80098000;states=0x80099000;arguments=0x8009a000
        globals_=0x80068e98;sp=0x801ff000
        resume=(old_code if old_owner else base_code)+16
        ram={obj+0xc:base,obj+0x10:base,obj+0x14:old_alt,obj+0xd0:resume,obj+0xa8:0x20020,
             base+0x10:header,base+0x18:constants,base+0x14:base_code,base+0x20:states,
             old_alt+0x14:old_code,new_alt+0x14:new_code,header+0xc:512,
             globals_+0x50:obj if current else 0,globals_+0x54:7}
        for n in range(4):ram[states+16+n*4]=struct.unpack_from('<I',i.base.items[4],16+n*4)[0]
        for n,value in enumerate(args):ram[arguments+4*n]=value&MASK
        calls=[]
        def resource(regs,memory):
            self.assertEqual(regs[4],constants+4);regs[2]=new_alt
        def dispatcher(regs,memory):
            mode=regs[5];calls.append(mode)
            self.assertEqual(memory[obj+0xc0],resume if mode==0x13 else base_code+48)
            out=replay(self.image,0x800399fc,{16:obj,17:mode,
                       22:memory[obj+0xbc],23:memory[obj+0xc4]},memory,stops=(0x80038eac,))
            memory[obj+0xbc]=out[22];memory[obj+0xc4]=out[23];memory[obj+0xc0]=out[21]
            regs[2]=out[2]
        result=replay(self.image,0x8001d2d4,{4:obj,5:1,6:argc,7:arguments,29:sp},ram,
                      hooks={0x800142b4:resource,0x80038e28:dispatcher})
        self.assertEqual(result[2],0xffffff01);self.assertEqual(calls,[0x13,3] if current and pre else [0x13])
        self.assertEqual(ram[obj+0xd0],0);self.assertEqual(ram[obj+0xc0],base_code+32)
        origin=obj+0x40+i.vm.stack.origin
        self.assertEqual(ram[obj+0xbc],origin+4*i.vm.stack.top)
        self.assertEqual(ram[obj+0xc4],origin+4*i.vm.stack.frames[-1])
        self.assertEqual(ram[ram[obj+0xbc]-4],0)
        for n,value in enumerate(args):self.assertEqual(ram[origin+4*n],value&MASK)


if __name__=='__main__':unittest.main()
