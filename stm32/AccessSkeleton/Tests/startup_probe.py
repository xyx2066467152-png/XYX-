"""执行实际 AXF 的复位路径；到达 main 为通过，提前触发半主机断点则失败。
此探针只验证 main 之前的启动，不模拟 STM32 外设或 FreeRTOS 调度。
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path.home() / 'Documents/Codex/2026-10-02/referenced-chatgpt-conversation-this-is-an/work/debugdeps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UcError
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_XPSR
from elftools.elf.elffile import ELFFile

def run(path):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in [(0,0x40000),(0x08000000,0x40000),(0x20000000,0x10000),(0x40000000,0x30000),(0xe000e000,0x2000)]:
        uc.mem_map(address,size)
    with open(path,'rb') as f:
        elf=ELFFile(f)
        for seg in elf.iter_segments():
            if seg['p_type']=='PT_LOAD' and seg['p_filesz']:
                address=seg['p_paddr']
                data=seg.data()
                uc.mem_write(address,data)
                if 0x08000000<=address<0x08040000:
                    uc.mem_write(address-0x08000000,data)
        syms={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
    sp=int.from_bytes(uc.mem_read(0x08000000,4),'little')
    reset=int.from_bytes(uc.mem_read(0x08000004,4),'little')
    uc.reg_write(UC_ARM_REG_SP,sp)
    uc.reg_write(UC_ARM_REG_XPSR,0x01000000)
    result={'status':'LIMIT','trace':[]}
    labels={v&~1:k for k,v in syms.items() if k in ('Reset_Handler','SystemInit','__main','__rt_entry','__rt_lib_init','_initio','_sys_open','main')}
    def step(u,address,size,user):
        if address in labels: result['trace'].append(labels[address])
        if address==(syms['main']&~1):
            result['status']='PASS: reached main'
            u.emu_stop()
        elif bytes(u.mem_read(address,2))==b'\xab\xbe':
            result['status']=f'FAIL: semihosting BKPT 0xAB before main at 0x{address:08X}'
            u.emu_stop()
    uc.hook_add(UC_HOOK_CODE,step)
    try: uc.emu_start(reset,0,count=200000)
    except UcError as error: result['status']=f'ERROR: {error} PC=0x{uc.reg_read(UC_ARM_REG_PC):08X}'
    print(Path(path).name, result['status'])
    print(' -> '.join(result['trace']))
    return result['status'].startswith('PASS')

okay=True
for path in sys.argv[1:]: okay=run(path) and okay
sys.exit(0 if okay else 1)
