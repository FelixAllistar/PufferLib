#!/usr/bin/env python3
"""Build bounded translation units for the optimized ROM-derived runtime.

The verification generator remains the source of instruction semantics. Splitting
its pages lets the ordinary CUDA optimizer work without optimizing the entire
game in one compiler process. Outputs contain ROM-derived code and stay local.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

import generate_logic as logic


def write_changed(path, content):
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def fuse_blocks(body, data, sites, page, page_bits):
    """Add guarded straight-line paths; retain every original boundary path.

    Only ordinary RAM/ROM accesses can be fused. IO, indirect addressing,
    calls, returns and branches retain their instruction-level paths. A guard
    proves the entire run fits before both timing boundaries and the watchdog.
    This lets the compiler remove intermediate flags that no instruction reads.
    """
    fragments = re.findall(r'(p([0-9a-f]{4}):\{ // .*?\n\})', body, re.S)
    by_pc = {int(pc, 16): text for text, pc in fragments}
    local = [pc for pc in sites if pc >> page_bits == page]
    leaders = {local[0]}
    safe = {}
    for pc in sites:
        name, mode = logic.OPS[data[pc - 0x8000]]
        nxt = pc + logic.LENGTH[mode]
        operand = data[(pc + 1 - 0x8000) & 32767]
        addr = operand + 256 * data[(pc + 2 - 0x8000) & 32767]
        control = name in logic.blocks.BRANCHES or name in ('JSR', 'JMP', 'RTS', 'RTI')
        if name in logic.blocks.BRANCHES:
            leaders.add((nxt + (operand if operand < 128 else operand - 256)) & 65535)
        if name in ('JSR', 'JMP') and mode == 'abs':
            leaders.add(addr)
        if control:
            leaders.add(nxt)
        ok = not control and mode not in ('ix', 'iy', 'ind')
        if mode in ('abs', 'ax', 'ay'):
            end = addr + (255 if mode != 'abs' else 0)
            ok &= end < 8192 or (addr >= 32768 and end <= 65535 and name not in
                                 ('STA', 'STX', 'STY', 'INC', 'DEC', 'ASL', 'LSR', 'ROL', 'ROR'))
        safe[pc] = ok
        if not ok:
            leaders.update((pc, nxt))
    runs = []
    run = []
    for pc in local:
        if run and (pc in leaders or len(run) == 16 or
                    pc != run[-1] + logic.LENGTH[logic.OPS[data[run[-1] - 0x8000]][1]]):
            runs.append(run)
            run = []
        if safe[pc]:
            run.append(pc)
        elif run:
            runs.append(run)
            run = []
    if run:
        runs.append(run)
    runs = [run for run in runs if len(run) >= 2]
    fast = []
    entries = {run[0] for run in runs}
    def retarget(text):
        return re.sub(r'goto p([0-9a-f]{4});',
                      lambda m: f'goto {"f" if int(m[1], 16) in entries else "p"}{m[1]};', text)
    body = retarget(body)
    for run in runs:
        cost = sum(logic.CYCLES[data[pc - 0x8000]] + 1 for pc in run)
        first = run[0]
        lines = [f'f{first:04x}:{{',
                 f'if(!s->timing.enabled||s->timing.clock+{cost}>=s->timing.length||'
                 f's->timing.clock+{cost}>=s->timing.nmi||s->instructions>{200000-len(run)})goto p{first:04x};',
                 f's->instructions+={len(run)};']
        for i, pc in enumerate(run):
            fragment = by_pc[pc]
            fragment = re.sub(r'^p[0-9a-f]{4}:\{ // [^\n]*\n', '{\n', fragment)
            fragment = fragment.replace('if(++s->instructions>200000){s->fault=-1;return;}\n', '')
            fragment = re.sub(r'if\(s->timing.enabled\)\{if\(s->timing.clock>=s->timing.length\|\|s->timing.clock>=s->timing.nmi\)\{s->pc=\d+;return;\}s->timing.clock\+=(\d+);\}',
                              r's->timing.clock+=\1;', fragment)
            fragment = fragment.replace('if(s->timing.enabled)', '')
            if i + 1 < len(run):
                fragment = fragment.replace(f'goto p{run[i+1]:04x};', '')
            lines.append(retarget(fragment))
        lines.append('}')
        fast.append('\n'.join(lines))
    return body.rstrip()[:-1] + '\n' + '\n'.join(fast) + '\n}'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('rom', type=Path)
    parser.add_argument('assembly', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--idle-skip', type=int, choices=(0, 1), default=1)
    parser.add_argument('--poll-skip', type=int, choices=(0, 1), default=1)
    parser.add_argument('--register-cache', type=int, choices=(0, 1), default=1)
    parser.add_argument('--fuse-blocks', type=int, choices=(0, 1), default=1)
    parser.add_argument('--page-bits', type=int, choices=(8, 9, 10), default=9)
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if logic.blocks.fnv(rom) != 0x6e01246e5d215cb3:
        raise ValueError('unsupported ROM fingerprint')
    data = rom[16:32784]
    symbols, sites = logic.symbols_and_code(args.assembly.read_text(), data)
    generated = logic.generate(data, symbols, sites, args.page_bits)
    page_text, frame = generated.split('SMB_HD int smb_logic_frame', 1)
    pages = re.findall(r'(SMB_PAGE void smb_logic_page_(\d+)\(.*?)(?=\nSMB_PAGE void smb_logic_page_|\Z)',
                       page_text, flags=re.S)
    if not pages:
        raise ValueError('no generated pages')
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    code_mask = bytearray(32768)
    for pc in sites:
        size = logic.LENGTH[logic.OPS[data[pc - 0x8000]][1]]
        code_mask[pc - 0x8000:pc - 0x8000 + size] = b'\1' * size
    mask_path = output / 'code_mask.bin'
    if not mask_path.exists() or mask_path.read_bytes() != code_mask:
        mask_path.write_bytes(code_mask)
    header = ('// Local generated declarations; do not distribute ROM-derived output.\n'
              '#pragma once\n#include "logic.h"\n'
              '#ifdef __CUDACC__\n#define SMB_RUNTIME __device__ __noinline__\n'
              '#else\n#define SMB_RUNTIME __attribute__((noinline))\n#endif\n')
    header += ''.join(f'SMB_RUNTIME void smb_logic_page_{number}(SmbLogic*,const uint8_t*);\n'
                      for _, number in pages)
    header += 'SMB_RUNTIME int smb_logic_frame(SmbLogic*,const uint8_t*,int);\n'
    write_changed(output / 'generated_logic.h', header)
    sources = []
    for body, number in pages:
        includes = '#include "generated_logic.h"\n'
        # A self-jump changes no registers or memory. Advance by an integer
        # number of its three-cycle executions, retaining boundary overshoot.
        # Interrupt/frame checks still occur at the same instruction boundary.
        if args.idle_skip and int(number) == symbols['EndlessLoop'] >> args.page_bits:
            pc = symbols['EndlessLoop']
            if data[pc - 0x8000:pc - 0x8000 + 3] != bytes((0x4c, pc & 255, pc >> 8)):
                raise ValueError('unexpected idle instruction')
            marker = f'p{pc:04x}:{{ // JMP abs\n'
            replacement = marker + f'''if(s->timing.enabled){{
    int stop=s->timing.length<s->timing.nmi?s->timing.length:s->timing.nmi;
    int count=s->timing.clock<stop?(stop-s->timing.clock+2)/3:0;
    s->instructions+=(uint32_t)count;
    if(s->instructions>200000){{s->fault=-1;return;}}
    s->timing.clock+=count*3;s->pc={pc};return;
}}
'''
            if body.count(marker) != 1:
                raise ValueError('missing idle entry')
            body = body.replace(marker, replacement)
        for name, wait_for_set, branch in (('Sprite0Clr', 0, 0xd0), ('Sprite0Hit', 1, 0xf0)):
            pc = symbols[name]
            if args.poll_skip and int(number) == pc >> args.page_bits:
                if data[pc - 0x8000:pc - 0x8000 + 7] != bytes((0xad, 2, 0x20, 0x29, 0x40, branch, 0xf9)):
                    raise ValueError('unexpected sprite polling loop')
                marker = f'p{pc:04x}:{{ // LDA abs\n'
                if body.count(marker) != 1:
                    raise ValueError('missing polling entry')
                body = body.replace(marker, marker + f'smb_runtime_poll_skip(s,{wait_for_set});\n')
                if 'runtime_poll.h' not in includes:
                    includes += '#include "runtime_poll.h"\n'
        body = body.replace('SMB_PAGE void ', 'SMB_RUNTIME void ', 1)
        # The data array and state are separate allocations in both runtimes.
        # This also lets CUDA use its read-only path for immutable program data.
        body = body.replace('SmbLogic* s,const uint8_t* data',
                            'SmbLogic* __restrict__ s,const uint8_t* __restrict__ data')
        if args.fuse_blocks:
            body = fuse_blocks(body, data, sites, int(number), args.page_bits)
        if args.register_cache:
            includes += '#include "runtime_registers.h"\n'
            body = re.sub(r's->(a|x|y|p|sp)\b', r'r.\1', body)
            body = body.replace('s->timing.clock', 'clock').replace('s->instructions', 'instructions')
            for name in ('nz', 'cmp', 'adc', 'shift'):
                body = body.replace(f'smb_{name}(s,', f'smb_reg_{name}(r,')
            body = body.replace('smb_push(s,', 'smb_reg_push(s,r,').replace('smb_pop(s)', 'smb_reg_pop(s,r)')
            body = body.replace('smb_read(s,data,address)', 'smb_reg_read(s,data,address,clock)')
            # Indirect JMP operands are literal addresses, unlike other reads.
            body = re.sub(r'smb_read\(s,data,(\d+)\)', r'smb_reg_read(s,data,\1,clock)', body)
            body = body.replace('smb_write(s,address,', 'smb_reg_write(s,address,')
            # Writes are emitted one per line; append the clock to that call.
            body = re.sub(r'(smb_reg_write\(s,address,.*?)\);', r'\1,clock);', body)
            body = body.replace('smb_runtime_poll_skip(s,', 'smb_reg_poll(s,r,clock,instructions,')
            spill = ('s->a=r.a;s->x=r.x;s->y=r.y;s->p=r.p;s->sp=r.sp;'
                     's->timing.clock=clock;s->instructions=instructions;return;')
            body = body.replace('return;', spill)
            entry = ('\nSmbRegisters r={s->a,s->x,s->y,s->p,s->sp};'
                     '\nint clock=s->timing.clock;uint32_t instructions=s->instructions;\n')
            first_brace = body.index('{') + 1
            body = body[:first_brace] + entry + body[first_brace:]
        filename = f'page_{number}.cpp'
        write_changed(output / filename, includes + body + '\n')
        sources.append(filename)
    write_changed(output / 'frame.cpp', '#include "generated_logic.h"\nSMB_RUNTIME int smb_logic_frame' + frame)
    sources.append('frame.cpp')
    write_changed(output / 'sources.mk', 'RUNTIME_GENERATED := ' + ' '.join(sources) + '\n')
    # Mark the included makefile current even when only generator comments or
    # options changed; unchanged translation units retain their object caches.
    (output / 'sources.mk').touch()
    write_changed(output / 'generation.json', json.dumps({
        'rom_sha256': hashlib.sha256(rom).hexdigest(), 'instruction_sites': len(sites),
        'pages': len(pages), 'idle_skip': bool(args.idle_skip), 'poll_skip': bool(args.poll_skip),
        'register_cache': bool(args.register_cache), 'fuse_blocks': bool(args.fuse_blocks),
        'page_bits': args.page_bits, 'sources': sources,
    }, indent=2) + '\n')
    print(f'Generated {len(pages)} runtime pages; idle skip={args.idle_skip}')


if __name__ == '__main__':
    main()
