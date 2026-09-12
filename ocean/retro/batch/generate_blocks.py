#!/usr/bin/env python3
"""Specialize QuickNES' 6502 operations to immutable mapper-0 PRG bytes.

Generated artifacts stay in build/. No ROM bytes or disassembly are checked in.
Unknown PCs, RAM code, interrupt-control and unofficial opcodes use the existing
interpreter. This is an execution optimization, not a source port of SMB logic.
Operation/flag/bus ordering below follows ../nes_emu/Nes_Cpu.cpp (LGPL-2.1+).
"""
import argparse
from pathlib import Path


OPS = {}


def group(name, entries):
    for mode, opcode in entries.items():
        OPS[opcode] = (name, mode)


for name, base in [('ORA', 0x00), ('AND', 0x20), ('EOR', 0x40),
                   ('ADC', 0x60), ('STA', 0x80), ('LDA', 0xA0),
                   ('CMP', 0xC0), ('SBC', 0xE0)]:
    modes = {'ix': 1, 'zp': 5, 'imm': 9, 'abs': 13, 'iy': 17,
             'zx': 21, 'ay': 25, 'ax': 29}
    if name == 'STA':
        del modes['imm']
    group(name, {mode: base + offset for mode, offset in modes.items()})
group('LDX', dict(imm=0xA2, zp=0xA6, zy=0xB6, abs=0xAE, ay=0xBE))
group('LDY', dict(imm=0xA0, zp=0xA4, zx=0xB4, abs=0xAC, ax=0xBC))
group('STX', dict(zp=0x86, zy=0x96, abs=0x8E))
group('STY', dict(zp=0x84, zx=0x94, abs=0x8C))
group('CPX', dict(imm=0xE0, zp=0xE4, abs=0xEC))
group('CPY', dict(imm=0xC0, zp=0xC4, abs=0xCC))
group('BIT', dict(zp=0x24, abs=0x2C))
for name, base in [('ASL', 0x00), ('ROL', 0x20), ('LSR', 0x40), ('ROR', 0x60)]:
    group(name, {mode: base + offset for mode, offset in
                 dict(zp=6, acc=10, abs=14, zx=22, ax=30).items()})
group('DEC', dict(zp=0xC6, zx=0xD6, abs=0xCE, ax=0xDE))
group('INC', dict(zp=0xE6, zx=0xF6, abs=0xEE, ax=0xFE))
for name, opcode in dict(INX=0xE8, INY=0xC8, DEX=0xCA, DEY=0x88,
                         TAX=0xAA, TXA=0x8A, TAY=0xA8, TYA=0x98,
                         TSX=0xBA, TXS=0x9A, CLC=0x18, SEC=0x38,
                         CLV=0xB8, CLD=0xD8, SED=0xF8, NOP=0xEA,
                         PHA=0x48, PLA=0x68, PHP=0x08, RTS=0x60).items():
    group(name, {'imp': opcode})
group('JMP', dict(abs=0x4C))
group('JSR', dict(abs=0x20))
BRANCHES = {'BPL': '!IS_NEG', 'BMI': 'IS_NEG', 'BVC': '!(status & st_v)',
            'BVS': 'status & st_v', 'BCC': '!(c & 0x100)', 'BCS': 'c & 0x100',
            'BNE': '(uint8_t)nz', 'BEQ': '!(uint8_t)nz'}
for i, name in enumerate(BRANCHES):
    group(name, {'rel': 0x10 + 0x20 * i})
LENGTH = dict(imp=1, acc=1, imm=2, zp=2, zx=2, zy=2, ix=2, iy=2, rel=2,
              abs=3, ax=3, ay=3)


def fnv(data):
    h = 14695981039346656037
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xffffffffffffffff
    return h


def generate(prg, page_bits=10):
    def byte(pc):
        return prg[pc - 0x8000] if 0x8000 <= pc <= 0xffff else 0

    def word(pc):
        return byte(pc) | byte(pc + 1) << 8

    def decode(pc):
        op = OPS.get(byte(pc))
        return op if op and pc + LENGTH[op[1]] <= 0x10000 else None

    # Conservative discovery: vectors and all potential direct call/jump
    # targets, including overlapping/data interpretations. Dynamic destinations
    # not discovered here remain fully executable by the reference fallback.
    todo = [word(v) for v in (0xfffa, 0xfffc, 0xfffe)]
    for pc in range(0x8000, 0xfffe):
        if byte(pc) in (0x20, 0x4c):
            todo += [word(pc + 1), pc + 3]
    seen = set()
    while todo:
        pc = todo.pop()
        if pc in seen or not 0x8000 <= pc < 0x10000:
            continue
        op = decode(pc)
        if not op:
            continue
        seen.add(pc)
        name, mode = op
        nxt = pc + LENGTH[mode]
        if name in ('JMP', 'JSR'):
            todo.append(word(pc + 1))
        if name in BRANCHES:
            b = byte(pc + 1)
            todo.append((nxt + (b if b < 128 else b - 256)) & 65535)
        if name not in ('JMP', 'RTS'):
            todo.append(nxt)

    bank = 0
    def jump(pc):
        if pc in seen and pc >> page_bits == bank:
            return f'goto rom_{pc:04x};'
        return f'pc={pc}; goto page_exit;'

    bodies = {}

    for pc in sorted(seen):
        bank = pc >> page_bits
        lines = []
        opcode = byte(pc)
        name, mode = decode(pc)
        b, addr = byte(pc + 1), word(pc + 1)
        nxt = pc + LENGTH[mode]
        lines += [f'rom_{pc:04x}: {{ // {name} {mode}',
                  f'if(clock_count>=clock_limit) {{ pc={pc}; goto page_exit; }}',
                  f'clock_count+=clock_table[0x{opcode:02x}];']
        s = []
        if name in BRANCHES:
            target = (nxt + (b if b < 128 else b - 256)) & 65535
            s += [f'if({BRANCHES[name]}) {{',
                  f'clock_count+={int((nxt & 0xff00) != (target & 0xff00))};']
            # Fold only complete iterations of a literal status-poll loop,
            # and only inside QuickNES' already-cached $2002 interval. Those
            # reads just clear an already-clear write latch and return r2002;
            # they do not run a peripheral or refresh open bus. Stop before
            # the next status event AND the CPU scheduler's next boundary.
            # A stable-register guard also handles the first read clearing
            # vblank / returning different open-bus low bits correctly.
            poll_setup = []
            poll_guard = None
            period = 0
            if target == pc-5 and byte(target) == 0xad and word(target+1) == 0x2002 and byte(target+3) == 0x29:
                poll_guard = f'a==(poll_status&{byte(target+4)}) && nz==a'
                period = 9
            elif target == pc-3 and word(target+1) == 0x2002:
                if byte(target) == 0xad:
                    poll_guard = 'a==poll_status && nz==poll_status'
                    period = 7
                elif byte(target) == 0x2c:
                    poll_setup = ['int poll_nz=poll_status;',
                                  'if(!(a&poll_nz)) poll_nz=(poll_nz<<4)&0x800;']
                    poll_guard = 'nz==poll_nz && (status&st_v)==(poll_status&st_v)'
                    period = 7
            if poll_guard:
                period += int((nxt & 0xff00) != (target & 0xff00))
                s += ['if(idle_skip_enabled) {',
                      'auto& poll_core=STATIC_CAST(Nes_Core&,*this);',
                      'int poll_status=poll_core.ppu.r2002;'] + poll_setup
                s += [f'if({poll_guard}) {{',
                      'nes_time_t poll_end=poll_core.ppu_2002_time-1;',
                      'if(poll_end>clock_limit) poll_end=clock_limit;',
                      'if(poll_end>clock_count) {',
                      f'nes_time_t folded=((poll_end-clock_count)/{period})*{period};',
                      'if(folded) { clock_count+=folded; poll_core.ppu.second_write=false; }',
                      '}',
                      '}', '}']
            s += [jump(target), '} else { clock_count--; ' + jump(nxt) + ' }']
        elif name == 'JMP':
            if addr == pc:
                s += ['if(idle_skip_enabled && clock_count<clock_limit)',
                      'clock_count+=((clock_limit-clock_count+2)/3)*3;']
            s += [jump(addr)]
        elif name == 'JSR':
            s += [f'WRITE_LOW(0x100|(sp-1),{(nxt-1) >> 8});',
                  'sp=(sp-2)|0x100;', f'WRITE_LOW(sp,{(nxt-1) & 255});', jump(addr)]
        elif name == 'RTS':
            s += ['pc=1+READ_LOW(sp)+READ_LOW(0x100|(sp-0xff))*0x100;',
                  'sp=(sp-0xfe)|0x100;',
                  f'if((pc>>{page_bits})=={bank}) goto page_dispatch;',
                  'goto page_exit;']
        elif mode == 'imp':
            s += {
                'INX': ['x=uint8_t(nz=x+1);'], 'INY': ['y=uint8_t(nz=y+1);'],
                'DEX': ['x=uint8_t(nz=x-1);'], 'DEY': ['y=uint8_t(nz=y-1);'],
                'TAX': ['x=nz=a;'], 'TXA': ['a=nz=x;'],
                'TAY': ['y=nz=a;'], 'TYA': ['a=nz=y;'],
                'TSX': ['x=nz=GET_SP();'], 'TXS': ['SET_SP(x);'],
                'CLC': ['c=0;'], 'SEC': ['c=~0;'], 'CLV': ['status&=~st_v;'],
                'CLD': ['status&=~st_d;'], 'SED': ['status|=st_d;'], 'NOP': [],
                'PHA': ['PUSH(a);'], 'PLA': ['a=nz=READ_LOW(sp); sp=(sp-0xff)|0x100;'],
                'PHP': ['int temp; CALC_STATUS(temp); PUSH(temp|st_b|st_r);'],
            }[name]
            s += [jump(nxt)]
        else:
            store = name in ('STA', 'STX', 'STY')
            rmw = name in ('INC', 'DEC', 'ASL', 'LSR', 'ROL', 'ROR') and mode != 'acc'
            if mode == 'imm':
                s += [f'unsigned data={b};']
            elif mode == 'acc':
                s += ['unsigned data=a;']
            else:
                if mode in ('zp', 'zx', 'zy'):
                    expr = str(b) if mode == 'zp' else f'uint8_t({b}+{"x" if mode == "zx" else "y"})'
                    s += [f'unsigned addr={expr};']
                elif mode == 'abs':
                    s += [f'unsigned addr={addr};']
                elif mode in ('ax', 'ay'):
                    s += [f'unsigned low={b}+{"x" if mode == "ax" else "y"};',
                          f'unsigned addr={addr & 0xff00}+low;']
                elif mode == 'ix':
                    s += [f'unsigned zp=uint8_t({b}+x);',
                          'unsigned addr=READ_LOW(zp)+256*READ_LOW(uint8_t(zp+1));']
                elif mode == 'iy':
                    s += [f'unsigned low=READ_LOW({b})+y;',
                          f'unsigned addr=low+256*READ_LOW({(b+1)&255});']
                indexed = mode in ('ax', 'ay', 'iy')
                if indexed:
                    if not store and not rmw:
                        s += ['clock_count+=low>>8;']
                    if name != 'LDA':
                        if store or rmw:
                            s += ['READ(addr-(low&0x100));']
                        else:
                            s += ['if(low&0x100) READ(addr-0x100);']
                if not store:
                    if mode in ('zp', 'zx', 'zy'):
                        s += ['unsigned data=READ_LOW(addr);']
                    elif name == 'LDA' and indexed:
                        # Preserve QuickNES' specialized mapped RAM/ROM read
                        # and its exact I/O dummy-read/page-overflow behavior.
                        s += ['unsigned data=READ_PROG(uint16_t(addr));',
                              'if((unsigned)(addr-0x2000)<0x6000) {',
                              'if(low&0x100) READ(addr-0x100); data=READ(addr); }']
                    else:
                        reader = 'READ_LIKELY_PPU' if name in ('LDA', 'BIT') and mode == 'abs' else 'READ'
                        s += [f'unsigned data={reader}(addr);']
                if rmw and mode not in ('zp', 'zx', 'zy'):
                    s += ['WRITE(addr,data);']
            if store:
                writer = 'WRITE_LOW' if mode in ('zp', 'zx', 'zy') else 'WRITE'
                s += [f'{writer}(addr,{dict(STA="a", STX="x", STY="y")[name]});']
            elif name in ('LDA', 'LDX', 'LDY'):
                s += [f'{dict(LDA="a", LDX="x", LDY="y")[name]}=nz=data;']
            elif name in ('CMP', 'CPX', 'CPY'):
                s += [f'nz={dict(CMP="a", CPX="x", CPY="y")[name]}-data; c=~nz; nz&=0xff;']
            elif name in ('AND', 'ORA', 'EOR'):
                s += [f'nz=(a{dict(AND="&", ORA="|", EOR="^")[name]}=data);']
            elif name in ('ADC', 'SBC'):
                if name == 'SBC':
                    s += ['data^=0xff;']
                s += ['int carry=(c>>8)&1;', 'int ov=(a^0x80)+carry+(int8_t)data;',
                      'status=(status&~st_v)|((ov>>2)&0x40);',
                      'c=nz=a+data+carry; a=uint8_t(nz);']
            elif name == 'BIT':
                s += ['nz=data; status=(status&~st_v)|(nz&st_v);',
                      'if(!(a&nz)) nz=(nz<<4)&0x800;']
            else:
                s += {
                    'INC': ['nz=data+1;'], 'DEC': ['nz=data-1;'],
                    'ASL': ['c=nz=data<<1;'],
                    'ROL': ['int carry=(c>>8)&1; c=data<<1; nz=c|carry;'],
                    'LSR': ['c=data<<8; nz=data>>1;'],
                    'ROR': ['nz=((c>>1)&0x80)|(data>>1); c=data<<8;'],
                }[name]
                if mode == 'acc':
                    s += ['a=uint8_t(nz);']
                else:
                    writer = 'WRITE_LOW' if mode in ('zp', 'zx', 'zy') else 'WRITE'
                    s += [f'{writer}(addr,uint8_t(nz));']
            s += [jump(nxt)]
        lines += s + ['}']
        bodies[pc] = lines
    output = ['// Generated from the supplied, fingerprint-checked PRG. LGPL-2.1+.']
    for bank in sorted({pc >> page_bits for pc in seen}):
        entries = sorted(pc for pc in seen if pc >> page_bits == bank)
        output += [f'template<> void Nes_Cpu::run_rom_page<{bank}>(RomRegisters& regs) {{',
                   'unsigned pc=regs.pc;',
                   'nes_time_t clock_count=this->clock_count;',
                   'int a=regs.a,x=regs.x,y=regs.y,sp=regs.sp,status=regs.status,c=regs.c,nz=regs.nz;',
                   'int const st_n=0x80,st_v=0x40,st_r=0x20,st_b=0x10,st_d=8,st_i=4,st_z=2,st_c=1;',
                   'page_dispatch:', 'switch(pc) {']
        output += [f'case {pc}: goto rom_{pc:04x};' for pc in entries]
        output += ['default: goto page_exit;', '}']
        for pc in entries:
            output += bodies[pc]
        output += ['page_exit:', 'this->clock_count=clock_count;', 'regs={pc,a,x,y,sp,status,c,nz};', '}']
    output += ['void Nes_Cpu::run_rom_blocks(RomRegisters& regs) {',
               'while(clock_count<clock_limit && rom_blocks_enabled && rom_compiled_pc(regs.pc)) {',
               f'switch(regs.pc>>{page_bits}) {{']
    output += [f'case {bank}: run_rom_page<{bank}>(regs); break;' for bank in sorted({pc >> page_bits for pc in seen})]
    output += ['default: return;', '}', '}', '}']
    coverage = [0]*4096
    for pc in seen:
        coverage[(pc-0x8000)//8] |= 1 << (pc & 7)
    cases = [f'{{{pc},{list(LENGTH).index(decode(pc)[1])},{int(decode(pc)[0] in ("JMP", "JSR"))}}},' for pc in sorted(seen)]
    return '\n'.join(output) + '\n', len(seen), coverage, '\n'.join(cases)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('rom', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--page-bits', type=int, choices=range(8,16), default=10)
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if len(rom) != 40976 or fnv(rom) != 0x31d802e3779199da:
        raise SystemExit('requires the fingerprint-validated SMB1 iNES ROM')
    prg = rom[16:32784]
    code, count, coverage, cases = generate(prg, args.page_bits)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'rom_blocks.inc').write_text(code)
    (args.output / 'rom_blocks_cases.inc').write_text(cases)
    (args.output / 'rom_blocks_identity.h').write_text(
        f'#define RETRO_BLOCKS_PRG_FNV 0x{fnv(prg):016x}ull\n'
        f'#define RETRO_BLOCKS_INSTRUCTIONS {count}\n'
        'static const unsigned char rom_compiled_coverage[4096]={' + ','.join(map(str,coverage)) + '};\n'
        'static inline bool rom_compiled_pc(unsigned pc) { return pc>=0x8000 && pc<0x10000 && '
        '(rom_compiled_coverage[(pc-0x8000)>>3]&(1u<<(pc&7))); }\n')
    print(f'generated {count} instruction entries ({len(code):,} source bytes); fallback handles other PCs')


if __name__ == '__main__':
    main()
