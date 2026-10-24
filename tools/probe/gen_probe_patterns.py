# SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
# SPDX-License-Identifier: GPL-3.0-or-later

"""Generates src/runner/probe_patterns.c from the Steam fm.exe open in IDA (IDAPython).

`fm24ctl probe` looks for these patterns in another build of fm.exe (e.g. the Epic Games one) to
report where the functions and data the harness uses are in that build.

Every address the harness relies on (src/dll/game.h, src/dll/sites.h, src/dll/site_members.c) is
mapped to its function. Each function is stored as its bytes plus a mask that hides the bytes that
move when code is linked at another address:
  - RIP-relative operands (data, jump tables, vtables);
  - calls and jumps that leave the function;
  - immediates and displacements that look like image addresses or RVAs.
Each data address is stored as an instruction in one of those functions that references it.

Run in IDA with the repo path set:
    REPO = r"C:\\path\\to\\fm-24-match-harness"; exec(open(REPO + r"\\tools\\probe\\gen_probe_patterns.py").read())
"""

import os
import re

import ida_bytes
import ida_funcs
import ida_ua
import idaapi
import idautils

BASE = idaapi.get_imagebase()
ADDRESS_FLOOR = 0x100000  # displacements at least this large are RVAs, not struct offsets
IMAGE_END = 0x1E000000
MIN_REFERENCE_SIZE = 64  # smaller functions may hold too few fixed bytes to be found


def read(path):
    with open(os.path.join(REPO, path), encoding='utf-8') as f:
        return f.read()


def targets():
    game = read('src/dll/game.h')
    functions, data = game.split('enum GameData')
    data = data.split('};')[0]
    pattern = r'(RVA_\w+) = (0x[0-9A-F]+)'
    sites = re.findall(r'(SITE_\w+) =\s*(0x[0-9A-F]+)', read('src/dll/sites.h'))
    members = re.findall(r'\{(0x[0-9A-F]+), "', read('src/dll/site_members.c'))
    return (
        [(name, int(value, 16)) for name, value in re.findall(pattern, functions)],
        [(name, int(value, 16)) for name, value in re.findall(pattern, data)],
        [(name, int(value, 16)) for name, value in sites] + [('member', int(v, 16)) for v in members],
    )


def function_of(rva):
    """The function containing rva, or ending exactly at it (end markers)."""
    f = ida_funcs.get_func(BASE + rva) or ida_funcs.get_func(BASE + rva - 1)
    if not f:
        raise RuntimeError('no function at 0x%x' % rva)
    return f


def mask_of(f):
    start, end = f.start_ea, f.end_ea
    mask = [True] * (end - start)

    def hide(ea, count):
        for i in range(ea - start, min(ea - start + count, len(mask))):
            mask[i] = False

    for ea in idautils.Heads(start, end):
        insn = ida_ua.insn_t()
        if not ida_ua.decode_insn(insn, ea):
            hide(ea, ida_bytes.get_item_size(ea))
            continue
        for op in insn.ops:
            if op.type == ida_ua.o_void:
                break
            if op.type == ida_ua.o_mem and op.offb:
                hide(ea + op.offb, insn.size - op.offb if op.offb + 8 > insn.size else 4)
            elif op.type in (ida_ua.o_near, ida_ua.o_far) and op.offb:
                if not start <= op.addr < end:
                    hide(ea + op.offb, insn.size - op.offb)
            elif op.type == ida_ua.o_imm and op.offb:
                if BASE <= op.value < BASE + IMAGE_END or ADDRESS_FLOOR <= op.value < IMAGE_END:
                    hide(ea + op.offb, ida_ua.get_dtype_size(op.dtype))
            elif op.type in (ida_ua.o_displ, ida_ua.o_phrase) and op.offb:
                if ADDRESS_FLOOR <= (op.addr & 0xFFFFFFFF) < IMAGE_END:
                    hide(ea + op.offb, 4)
    return mask


def data_reference(rva, functions):
    """(function rva, instruction offset, operand offset, instruction size) of a RIP-relative
    reference to rva, preferring functions already in `functions`."""
    found = []
    for ea in idautils.DataRefsTo(BASE + rva):
        f = ida_funcs.get_func(ea)
        insn = ida_ua.insn_t()
        if not f or not ida_ua.decode_insn(insn, ea):
            continue
        for op in insn.ops:
            if op.type == ida_ua.o_void:
                break
            if op.type == ida_ua.o_mem and op.offb and op.addr == BASE + rva:
                disp = ida_bytes.get_dword(ea + op.offb)
                disp = disp - (1 << 32) if disp & 0x80000000 else disp
                if ea + insn.size + disp == BASE + rva:
                    size = f.end_ea - f.start_ea
                    found.append((f.start_ea - BASE not in functions, size < MIN_REFERENCE_SIZE, size, f, ea, op.offb, insn.size))
    if not found:
        raise RuntimeError('no RIP-relative reference to 0x%x' % rva)
    # Prefer functions already listed, then ones large enough to find, then the smallest.
    _, _, _, f, ea, offb, size = min(found, key=lambda x: x[:3])
    return f, ea - f.start_ea, offb, size


def main():
    function_targets, data_targets, site_targets = targets()
    functions = {}  # function rva -> (name, [reasons])

    def add(f, reason):
        entry = functions.setdefault(f.start_ea - BASE, [ida_funcs.get_func_name(f.start_ea), []])
        if reason not in entry[1]:
            entry[1].append(reason)

    for name, rva in function_targets:
        f = function_of(rva)
        add(f, name)
        # A jmp thunk: also find what it jumps to.
        insn = ida_ua.insn_t()
        if f.end_ea - f.start_ea <= 8 and ida_ua.decode_insn(insn, f.start_ea) and insn.itype == idaapi.NN_jmp:
            add(ida_funcs.get_func(insn.ops[0].addr), name + ' (thunk target)')
    for name, rva in site_targets:
        add(function_of(rva), 'sites')
    references = []
    for name, rva in data_targets:
        f, offset, offb, size = data_reference(rva, functions)
        add(f, name + ' (reference)')
        references.append((name, rva, f.start_ea - BASE, offset, offb, size))

    order = sorted(functions)
    index = {rva: i for i, rva in enumerate(order)}
    out = []
    out.append('// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>')
    out.append('// SPDX-License-Identifier: GPL-3.0-or-later')
    out.append('')
    out.append('// GENERATED by tools/probe/gen_probe_patterns.py from the Steam fm.exe (PE timestamp')
    out.append('// 1744804480) in IDA; do not edit.')
    out.append('')
    out.append('#include "probe_patterns.h"')
    out.append('')
    out.append('')
    byte_lines, mask_lines, entries = [], [], []
    offset = 0
    mask_offset = 0
    for rva in order:
        f = ida_funcs.get_func(BASE + rva)
        size = f.end_ea - f.start_ea
        data = ida_bytes.get_bytes(f.start_ea, size)
        mask = mask_of(f)
        bits = bytearray((size + 7) // 8)
        for i, keep in enumerate(mask):
            if keep:
                bits[i // 8] |= 1 << (i % 8)
        name, reasons = functions[rva]
        entries.append('\t{"%s", "%s", 0x%X, %u, %u, %u},' % (name, ', '.join(reasons), rva, size, offset, mask_offset))
        for i in range(0, size, 16):
            byte_lines.append('\t' + ' '.join('0x%02X,' % b for b in data[i:i + 16]))
        for i in range(0, len(bits), 16):
            mask_lines.append('\t' + ' '.join('0x%02X,' % b for b in bits[i:i + 16]))
        offset += size
        mask_offset += len(bits)
    out.append('const uint8_t probePatternBytes[] = {')
    out += byte_lines
    out.append('};')
    out.append('')
    out.append('// Bit i % 8 of byte i / 8: byte i of the function must match (else it may differ).')
    out.append('const uint8_t probePatternMask[] = {')
    out += mask_lines
    out.append('};')
    out.append('')
    out.append('const ProbeFunction probeFunctions[] = {')
    out += entries
    out.append('};')
    out.append('const size_t probeFunctionCount = sizeof probeFunctions / sizeof probeFunctions[0];')
    out.append('')
    out.append('const ProbeData probeData[] = {')
    for name, rva, frva, ioff, offb, size in references:
        out.append('\t{"%s", 0x%X, %u, %u, %u, %u},' % (name, rva, index[frva], ioff, offb, size))
    out.append('};')
    out.append('const size_t probeDataCount = sizeof probeData / sizeof probeData[0];')
    with open(os.path.join(REPO, 'src/runner/probe_patterns.c'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    return len(order), offset, len(references)


print(main())
