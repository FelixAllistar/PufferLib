#!/usr/bin/env python3
"""Compile immutable local SMB1 PRG control flow into C/CUDA statements.

This is a feasibility oracle, not a replacement claim for hand-written native
controllers. No opcode fetch/decode runs at frame time. Generated ROM-derived
code and data stay in ignored build/. The supplied annotated disassembly is
used only to find symbols/instruction boundaries, checked against PRG opcodes.
"""
import argparse
import importlib.util
import json
import re
from pathlib import Path

spec=importlib.util.spec_from_file_location('blocks',Path(__file__).parents[1]/'retro/batch/generate_blocks.py')
blocks=importlib.util.module_from_spec(spec);spec.loader.exec_module(blocks)
OPS=dict(blocks.OPS)
OPS.update({0x6c:('JMP','ind'),0x40:('RTI','imp'),0x28:('PLP','imp'),
            0x78:('SEI','imp'),0x58:('CLI','imp')})
LENGTH=dict(blocks.LENGTH,ind=3)
MODES={}
for opcode,(name,mode) in OPS.items():MODES[(name,mode)]=opcode
# Opcode timing is checked against the independent local reference's table.
timing_source=(Path(__file__).parents[1]/'retro/nes_emu/Nes_Cpu.cpp').read_text()
timing_text=timing_source.split('clock_table [256] = {',1)[1].split('};',1)[0]
timing_text=re.sub(r'//[^\n]*','',timing_text)
CYCLES=[int(v) for v in re.findall(r'\d+',timing_text)]
assert len(CYCLES)==256

def expression(text,symbols):
    if text.startswith('<'):return expression(text[1:],symbols)&255
    if text.startswith('>'):return expression(text[1:],symbols)>>8
    text=re.sub(r'\$([0-9a-fA-F]+)',r'0x\1',text)
    text=re.sub(r'%([01]+)',r'0b\1',text)
    text=re.sub(r'\b[A-Za-z_]\w*\b',lambda m: str(symbols[m[0]]) if m[0] in symbols else m[0],text)
    if not re.fullmatch(r'[0-9a-fA-Fxob +()*/\-]+',text):raise ValueError(text)
    return int(eval(text,{'__builtins__':{}},{}))

def symbols_and_code(asm,prg):
    symbols={};pc=0;instructions=[];pending_symbols={}
    for number,line in enumerate(asm.splitlines(),1):
        line=line.split(';',1)[0].strip()
        if not line:continue
        if '=' in line:
            k,v=line.split('=',1)
            try:symbols[k.strip()]=expression(v.strip(),symbols)
            except ValueError:pending_symbols[k.strip()]=v.strip()
            continue
        if ':' in line:
            label,line=line.split(':',1);symbols[label.strip()]=pc;line=line.strip()
        if not line:continue
        parts=line.split(None,1);op=parts[0];arg=parts[1].strip() if len(parts)>1 else ''
        if op in ('.index','.mem'):continue
        if op=='.org':pc=expression(arg,symbols);continue
        if op in ('.db','.dw'):
            if op=='.db' and arg=='$2c':instructions.append(pc)
            pc+=len(arg.split(','))*(1 if op=='.db' else 2);continue
        name=op.upper()
        if name not in {v[0] for v in OPS.values()}:raise ValueError((number,line,'unknown operation'))
        if name in blocks.BRANCHES:mode='rel'
        elif not arg:mode='acc' if name in ('ASL','LSR','ROL','ROR') else 'imp'
        elif arg.startswith('#'):mode='imm'
        elif arg.startswith('('):mode='ix' if arg.endswith(',x)') else 'iy' if arg.endswith('),y') else 'ind'
        else:
            base=arg.split(',')[0]
            try:zero=expression(base,symbols)<256
            except ValueError:zero=False
            mode=('z' if zero else 'a')+arg[-1] if ',' in arg else ('zp' if zero else 'abs')
        if mode in ('zx','zy') and (name,mode) not in MODES:mode='a'+mode[1]
        expected=MODES[(name,mode)]
        if not 0x8000<=pc<0x10000 or prg[pc-0x8000]!=expected:
            raise ValueError((number,hex(pc),line,hex(expected),hex(prg[pc-0x8000])))
        instructions.append(pc);pc+=LENGTH[mode]
    if pc!=0x10000:raise ValueError(('wrong assembly endpoint',hex(pc)))
    for k,v in pending_symbols.items():symbols[k]=expression(v,symbols)
    # Include overlapping BIT skips and branch targets encoded as .db.
    pending=list(instructions);seen=set()
    while pending:
        p=pending.pop()
        if p in seen:continue
        if p<0x8000 or p>=0x10000:raise ValueError(('non-PRG code',p))
        if prg[p-0x8000] not in OPS:raise ValueError(('unsupported reachable opcode',hex(p),prg[p-0x8000], sorted([(hex(v),k) for k,v in symbols.items() if abs(v-p)<32])))
        name,mode=OPS[prg[p-0x8000]];seen.add(p);nxt=p+LENGTH[mode]
        if name in blocks.BRANCHES:
            offset=prg[p-0x7fff];pending.append((nxt+(offset if offset<128 else offset-256))&65535)
        if name in ('JSR','JMP') and mode=='abs':pending.append(prg[p-0x7fff]+256*prg[p-0x7ffe])
        jump_engine=name=='JSR' and prg[p-0x7fff]+256*prg[p-0x7ffe]==symbols['JumpEngine']
        if name not in ('JMP','RTI','RTS') and not jump_engine and nxt in instructions:pending.append(nxt)
    return symbols,sorted(seen)

def generate(prg,symbols,pcs,page_bits=8):
    # Bound function size so the CUDA assembler fits within ordinary workstation memory.
    chunks=[];pages=sorted({p>>page_bits for p in pcs})
    poll_clear=symbols['Sprite0Clr'];poll_hit=symbols['Sprite0Hit']
    for page in pages:
        local=[p for p in pcs if p>>page_bits==page]
        def jump(p):
            return f'goto p{p:04x};' if p in local else f's->pc={p};return;'
        lines=[f'SMB_PAGE void smb_logic_page_{page}(SmbLogic* s,const uint8_t* data) {{',
               'switch(s->pc){'+''.join(f'case {p}:goto p{p:04x};' for p in local)+'default:s->fault=s->pc|0x40000;return;}']
        for p in local:
            name,mode=OPS[prg[p-0x8000]];nxt=p+LENGTH[mode];b=prg[(p+1-0x8000)&32767]
            addr=b+256*prg[(p+2-0x8000)&32767]
            lines.append(f'p{p:04x}:{{ // {name} {mode}\nif(++s->instructions>200000){{s->fault=-1;return;}}')
            lines.append(f'if(s->timing.enabled){{if(s->timing.clock>=s->timing.length||s->timing.clock>=s->timing.nmi){{s->pc={p};return;}}s->timing.clock+={CYCLES[prg[p-0x8000]]};}}')
            if mode=='imm':value=str(b)
            elif mode=='acc':value='s->a'
            elif mode in ('zp','abs','zx','zy','ax','ay','ix','iy','ind'):
                ea={'zp':str(b),'abs':str(addr),'zx':f'(({b}+s->x)&255)','zy':f'(({b}+s->y)&255)',
                    'ax':f'(({addr}+s->x)&65535)','ay':f'(({addr}+s->y)&65535)',
                    'ix':f'(s->ram[({b}+s->x)&255]+256*s->ram[({b}+s->x+1)&255])',
                    'iy':f'((s->ram[{b}]+256*s->ram[{(b+1)&255}]+s->y)&65535)',
                    'ind':str(addr)}[mode]
                if name not in ('JMP','JSR') or mode=='ind':lines.append('int address='+ea+';')
                if mode in ('ax','ay','iy') and name not in ('STA','STX','STY','ASL','ROL','LSR','ROR','INC','DEC'):
                    low=f's->ram[{b}]' if mode=='iy' else str(addr&255)
                    index='s->x' if mode=='ax' else 's->y'
                    lines.append(f'if(s->timing.enabled)s->timing.clock+=(({low}+{index})>>8);')
                value='smb_read(s,data,address)'
                if mode=='abs' and addr==0x2002:
                    value='(s->timing.enabled?smb_read(s,data,address):'+('64' if p==poll_hit else '0')+')'
                if name not in ('STA','STX','STY','JMP','JSR'):
                    lines.append('int value='+value+';');value='value'
            if name in blocks.BRANCHES:
                cond={'BPL':'!(s->p&128)','BMI':'s->p&128','BVC':'!(s->p&64)','BVS':'s->p&64',
                      'BCC':'!(s->p&1)','BCS':'s->p&1','BNE':'!(s->p&2)','BEQ':'s->p&2'}[name]
                target=(nxt+(b if b<128 else b-256))&65535
                cross=int((nxt>>8)!=(target>>8))
                lines.append(f'if({cond}){{if(s->timing.enabled)s->timing.clock+={cross};{jump(target)}}}if(s->timing.enabled)--s->timing.clock;'+jump(nxt))
            elif name=='JMP':
                if mode=='ind':lines.append(f's->pc=smb_read(s,data,{addr})+256*smb_read(s,data,{(addr&65280)|((addr+1)&255)});return;')
                else:lines.append(jump(addr))
            elif name=='JSR':lines.append(f'smb_push(s,{(nxt-1)>>8});smb_push(s,{(nxt-1)&255});'+jump(addr))
            elif name=='RTS':lines.append('int lo=smb_pop(s);s->pc=(lo+256*smb_pop(s)+1)&65535;return;')
            elif name=='RTI':lines.append('s->p=smb_pop(s)|32;int lo=smb_pop(s);s->pc=lo+256*smb_pop(s);return;')
            else:
                if name in ('LDA','LDX','LDY'):
                    r=name[-1].lower();lines.append(f's->{r}={value};smb_nz(s,s->{r});')
                elif name in ('STA','STX','STY'):lines.append(f'smb_write(s,address,s->{name[-1].lower()});')
                elif name in ('CMP','CPX','CPY'):lines.append(f'smb_cmp(s,s->{"a" if name=="CMP" else name[-1].lower()},{value});')
                elif name in ('ADC','SBC'):lines.append(f'smb_adc(s,{value}'+('^0xff' if name=='SBC' else '')+');')
                elif name in ('AND','ORA','EOR'):lines.append(f's->a{"&" if name=="AND" else "|" if name=="ORA" else "^"}={value};smb_nz(s,s->a);')
                elif name=='BIT':lines.append(f's->p=(s->p&~194)|({value}&192)|((s->a&{value})?0:2);')
                elif name in ('ASL','ROL','LSR','ROR'):
                    expr=f'smb_shift(s,{value},{("ASL","ROL","LSR","ROR").index(name)})'
                    lines.append(f's->a={expr};' if mode=='acc' else f'smb_write(s,address,{expr});')
                elif name in ('INC','DEC'):
                    lines.append(f'int result=({value}{"+" if name=="INC" else "-"}1)&255;smb_write(s,address,result);smb_nz(s,result);')
                elif name in ('INX','INY','DEX','DEY'):
                    r=name[-1].lower();lines.append(f'{"++" if name.startswith("IN") else "--"}s->{r};smb_nz(s,s->{r});')
                elif name in ('TAX','TAY','TXA','TYA','TSX','TXS'):
                    src={'TAX':'a','TAY':'a','TXA':'x','TYA':'y','TSX':'sp','TXS':'x'}[name]
                    dst={'TAX':'x','TAY':'y','TXA':'a','TYA':'a','TSX':'x','TXS':'sp'}[name]
                    lines.append(f's->{dst}=s->{src};'+('' if name=='TXS' else f'smb_nz(s,s->{dst});'))
                elif name=='PHA':lines.append('smb_push(s,s->a);')
                elif name=='PHP':lines.append('smb_push(s,s->p|48);')
                elif name=='PLA':lines.append('s->a=smb_pop(s);smb_nz(s,s->a);')
                elif name=='PLP':lines.append('s->p=smb_pop(s)|32;')
                elif name in ('CLC','SEC','CLV','CLD','SED','SEI','CLI'):
                    mask={'CLC':1,'SEC':1,'CLV':64,'CLD':8,'SED':8,'SEI':4,'CLI':4}[name]
                    lines.append(f's->p{"|=" if name.startswith("SE") else "&=~"}{mask};')
                elif name!='NOP':raise ValueError(name)
                lines.append(jump(nxt))
            lines.append('}')
        lines.append('}')
        chunks.append('\n'.join(lines))
    idle=symbols['EndlessLoop'];nmi=symbols['NonMaskableInterrupt']
    chunks.append(f'''SMB_HD int smb_logic_frame(SmbLogic* s,const uint8_t* data,int buttons) {{
    if(s->fault)return s->fault;
    if(s->timing.enabled){{
        s->joy[0]=buttons;s->joy[1]=0;s->instructions=0;smb_clock_begin(&s->timing);
        for(;;){{
            smb_clock_length(&s->timing,s->timing.clock);
            if(s->timing.clock>=s->timing.length){{
                smb_clock_frame_end(&s->timing,s->timing.clock);
                if(s->timing.nmi<=s->timing.clock&&!(s->timing.control&s->timing.status&128)){{
                    smb_push(s,s->pc>>8);smb_push(s,s->pc&255);smb_push(s,s->p|32);s->p|=4;s->pc={nmi};s->timing.clock+=7;
                }}
                break;
            }}
            if(s->timing.clock>=s->timing.nmi){{
                s->timing.nmi=0x3fffffff;smb_push(s,s->pc>>8);smb_push(s,s->pc&255);smb_push(s,(s->p|32)&~16);s->p|=4;s->pc={nmi};s->timing.clock+=7;
            }}
            switch(s->pc>>{page_bits}){{
    '''+''.join(f'case {page}:smb_logic_page_{page}(s,data);break;' for page in pages)+'''
            default:s->fault=s->pc|0x40000;break;}
            if(s->fault)break;
        }
        smb_clock_frame_end(&s->timing,s->timing.clock);
        smb_clock_finish(&s->timing);s->p&=207;
        return s->fault;
    }
    '''+f'''
    if(s->pc!={idle}){{s->fault=s->pc|0x80000;return s->fault;}}
    s->joy[0]=buttons;s->joy[1]=0;s->instructions=0;
    smb_push(s,s->pc>>8);smb_push(s,s->pc&255);smb_push(s,(s->p|32)&~16);s->p|=4;s->pc={nmi};
    while(s->pc!={idle}&&!s->fault){{switch(s->pc>>{page_bits}){{
    '''+''.join(f'case {page}:smb_logic_page_{page}(s,data);break;' for page in pages)+'''
    default:s->fault=s->pc|0x40000;break;}}
    return s->fault;
}''')
    return '\n'.join(chunks)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('rom');parser.add_argument('assembly');parser.add_argument('output')
    args=parser.parse_args();rom=Path(args.rom).read_bytes()
    if rom[:4]!=b'NES\x1a' or rom[4]!=2 or rom[6]&4:raise ValueError('expected untrained 32 KiB mapper-0 iNES ROM')
    prg=rom[16:32784]
    if blocks.fnv(rom)!=0x6e01246e5d215cb3:raise ValueError('unsupported ROM fingerprint')
    symbols,pcs=symbols_and_code(Path(args.assembly).read_text(),prg)
    out=Path(args.output);out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text('// Generated from a user-supplied ROM. Keep in ignored build/.\n#pragma once\n'+generate(prg,symbols,pcs))
    out.with_suffix('.symbols.json').write_text(json.dumps(symbols,indent=2)+'\n')
    out.with_suffix('.prg.bin').write_bytes(prg)
    print(f'Compiled {len(pcs)} instruction sites into {out}; NMI={symbols["NonMaskableInterrupt"]:04x}, idle={symbols["EndlessLoop"]:04x}')

if __name__=='__main__':main()
