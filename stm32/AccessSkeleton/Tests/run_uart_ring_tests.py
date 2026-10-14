"""Execute final AXF UART ring code with emulated registers, not real timing."""
from pathlib import Path
import sys, struct, json, hashlib, argparse
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--axf',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--deps',type=Path,default=Path(__file__).resolve().parent/'.deps')
a=p.parse_args();sys.path.insert(0,str(a.deps))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_XPSR
from elftools.elf.elffile import ELFFile
u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
for addr,size in [(0x08000000,0x40000),(0x20000000,0x10000),(0x40000000,0x30000),(0xe000e000,0x2000)]:u.mem_map(addr,size)
with a.axf.open('rb') as f:
    elf=ELFFile(f)
    for s in elf.iter_sections():
        if s['sh_flags']&2 and s['sh_type']!='SHT_NOBITS' and s['sh_size']:u.mem_write(s['sh_addr'],s.data())
    syms={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
STOP=0x0803fff0;SCRATCH=0x2000c000;STACK=0x2000f000;SR=0x40004400
returned=False
def stop(machine,addr,size,ctx):
    global returned
    returned=True;machine.emu_stop()
u.hook_add(UC_HOOK_CODE,stop,begin=STOP,end=STOP)
def call(name,*args):
    global returned
    returned=False
    for reg,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(reg,v)
    u.reg_write(UC_ARM_REG_SP,STACK);u.reg_write(UC_ARM_REG_LR,STOP|1);u.reg_write(UC_ARM_REG_XPSR,0x01000000)
    u.emu_start(syms[name]|1,STOP+2,count=100000)
    assert returned,name+' instruction budget exceeded'
    return u.reg_read(UC_ARM_REG_R0)
def irq(byte,status=0x20):
    u.mem_write(SR,struct.pack('<II',status,byte));call('BSP_UART2_IRQHandler')
    # Clear SR explicitly: this model does not simulate silicon SR/DR side effects.
    u.mem_write(SR,b'\0'*4)
def read(n):
    count=call('BSP_UART2_ReadBuffered',SCRATCH,n)
    assert count<=n
    return bytes(u.mem_read(SCRATCH,count))
tests=[]
def check(name,fn):
    try:fn();tests.append({'name':name,'passed':True})
    except Exception as exc:tests.append({'name':name,'passed':False,'error':repr(exc)})
def init():call('BSP_UART2_StartBufferedRx')
def empty():
    init();assert read(20)==b'';assert call('BSP_UART2_RxErrors')==0
def fifo():
    init()
    for b in range(200):irq(b)
    assert read(64)+read(64)+read(100)==bytes(range(200));assert read(1)==b''
def overflow():
    init()
    for b in range(256):irq(b)
    assert call('BSP_UART2_RxErrors')==1
    assert read(300)==bytes(range(255))
def wrap():
    init()
    for b in range(200):irq(b)
    assert read(180)==bytes(range(180))
    for b in range(180):irq(b)
    assert read(255)==bytes(range(180,200))+bytes(range(180))
def flags():
    init()
    for status in (0x08,0x02,0x04,0x01):irq(0xaa,status|0x20)
    assert read(10)==b'';assert call('BSP_UART2_RxErrors')==4
    irq(0xbb,0);assert read(10)==b''
    irq(0xcc);assert read(10)==b'\xcc'
def discard():
    init();irq(1);irq(2);call('BSP_UART2_DiscardBuffered');irq(3);assert read(10)==b'\x03'
def capacity_zero():
    init();irq(9);assert read(0)==b'';assert read(1)==b'\x09'
for name,fn in [('empty',empty),('FIFO/chunking',fifo),('bounded255/overflow',overflow),('index wrap',wrap),('ORE/FE/NE/PE drop',flags),('discard recovery',discard),('zero-capacity preserves',capacity_zero)]:check(name,fn)
result={'kind':'software register emulation of actual final ARM AXF; no hardware timing',
        'axf_sha256':hashlib.sha256(a.axf.read_bytes()).hexdigest(),'tests':tests,'passed':sum(t['passed'] for t in tests),'failed':sum(not t['passed'] for t in tests)}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n','utf-8')
print(json.dumps(result,indent=2));sys.exit(bool(result['failed']))
