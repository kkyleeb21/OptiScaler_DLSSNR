#!/usr/bin/env python3
"""Read-only x64 PE/NGX evidence collector. Never loads the inspected DLL.

S1 is a bounded investigation aid, NOT a whole-program backend classifier.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import datetime as dt
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / 'evidence' / 'D24'
GROUPS = ('D3D11', 'D3D12', 'VULKAN', 'CUDA')


class PE:
    def __init__(self, data):
        self.data = data
        if data[:2] != b'MZ':
            raise ValueError('not MZ')
        pe = self.unpack('<I', 0x3c)[0]
        if self.read(pe, 4) != b'PE\0\0':
            raise ValueError('not PE')
        machine, count = self.unpack('<HH', pe + 4)
        opt_size = self.unpack('<H', pe + 20)[0]
        opt = pe + 24
        if machine != 0x8664 or self.unpack('<H', opt)[0] != 0x20b:
            raise ValueError('only AMD64 PE32+ is supported')
        self.read(opt, opt_size)
        if opt_size < 112:
            raise ValueError('short optional header')
        self.base = self.unpack('<Q', opt + 24)[0]
        self.headers = self.unpack('<I', opt + 60)[0]
        nd = self.unpack('<I', opt + 108)[0]
        if nd > (opt_size - 112) // 8:
            raise ValueError('directory count exceeds optional header')
        self.dirs = [self.unpack('<II', opt + 112 + i * 8) for i in range(nd)]
        self.sections = []
        for i in range(count):
            at = opt + opt_size + 40 * i
            raw = self.read(at, 40)
            vs, va, size, offset = struct.unpack_from('<IIII', raw, 8)
            self.read(offset, size)
            self.sections.append(dict(name=raw[:8].rstrip(b'\0').decode('ascii', 'replace'),
                                      va=va, virtual_size=vs, size=size, offset=offset,
                                      executable=bool(struct.unpack_from('<I', raw, 36)[0] & 0x20000000)))

    def read(self, at, size):
        if at < 0 or size < 0 or at + size > len(self.data):
            raise ValueError(f'file range out of bounds: {at:#x}+{size:#x}')
        return self.data[at:at + size]

    def unpack(self, fmt, at):
        return struct.unpack(fmt, self.read(at, struct.calcsize(fmt)))

    def offset(self, rva, size=1):
        if 0 <= rva and rva + size <= self.headers:
            self.read(rva, size)
            return rva
        for s in self.sections:
            delta = rva - s['va']
            if 0 <= delta and delta + size <= s['size']:
                self.read(s['offset'] + delta, size)
                return s['offset'] + delta
        raise ValueError(f'RVA {rva:#x}+{size:#x} has no file-backed range')

    def rread(self, rva, size):
        return self.read(self.offset(rva, size), size)

    def runpack(self, fmt, rva):
        return struct.unpack(fmt, self.rread(rva, struct.calcsize(fmt)))

    def string(self, rva, limit=4096):
        result = bytearray()
        for i in range(limit):
            c = self.rread(rva + i, 1)[0]
            if c == 0:
                return result.decode('ascii', 'replace')
            result.append(c)
        raise ValueError('unterminated PE string')

    def directory(self, index):
        return self.dirs[index] if index < len(self.dirs) else (0, 0)

    def exports(self):
        rva, size = self.directory(0)
        if not rva:
            return []
        base, nf, nn, af, an, ao = self.runpack('<IIIIII', rva + 16)
        if nf > 100000 or nn > 100000:
            raise ValueError('excessive export table')
        names = collections.defaultdict(list)
        for i in range(nn):
            nr = self.runpack('<I', an + i * 4)[0]
            idx = self.runpack('<H', ao + i * 2)[0]
            if idx >= nf:
                raise ValueError('invalid export name ordinal')
            names[idx].append(self.string(nr))
        rows = []
        for i in range(nf):
            address = self.runpack('<I', af + i * 4)[0]
            if not address:
                continue  # EAT hole, not an ordinal-only export
            rows.append(dict(ordinal=base + i, names=names[i], rva=address,
                             forwarder=self.string(address) if rva <= address < rva + size else None))
        return rows

    def imports(self, delay=False):
        rva, size = self.directory(13 if delay else 1)
        if not rva:
            return []
        width = 32 if delay else 20
        rows = []
        for i in range(min(size // width, 10000)):
            v = self.runpack('<' + 'I' * (width // 4), rva + width * i)
            if not any(v):
                return rows
            if delay:
                attrs, name, _, iat, lookup, *_ = v
                if not attrs & 1:
                    name, iat, lookup = name - self.base, iat - self.base, lookup - self.base
            else:
                lookup, _, _, name, iat = v
            lib = self.string(name)
            if not lookup and not delay and v[1]:
                rows.append(dict(dll=lib, symbol='UNRESOLVED_BOUND_IAT', iat_rva=iat, delay=delay))
                continue
            table = lookup or iat
            for n in range(100000):
                value = self.runpack('<Q', table + n * 8)[0]
                if not value:
                    break
                symbol = f'ordinal:{value & 0xffff}' if value >> 63 else self.string(value + 2)
                rows.append(dict(dll=lib, symbol=symbol, iat_rva=iat + n * 8, delay=delay))
            else:
                raise ValueError('unterminated import thunk table')
        raise ValueError('unterminated import descriptor table')

    def runtime_functions(self):
        rva, size = self.directory(3)
        if not rva:
            return []
        if size % 12:
            raise ValueError('malformed x64 exception directory')
        return sorted((a, b) for a, b, _ in
                      struct.iter_unpack('<III', self.rread(rva, size)) if a < b)


def group(name):
    return next((g for g in GROUPS if name.startswith('NVSDK_NGX_' + g + '_')), 'OTHER')


def strings(pe):
    pattern = re.compile(r'Vulkan|VkDevice|D3D11|ID3D11|cuGraphics|cudaGraphics|Blackwell|ScalingRatio|VK_[A-Z]|[.]dll', re.I)
    rows = []
    for s in pe.sections:
        raw = pe.read(s['offset'], s['size'])
        for encoding, regex in [('ascii', rb'[\x20-\x7e]{5,}'), ('utf-16le', rb'(?:[\x20-\x7e]\x00){5,}')]:
            for m in re.finditer(regex, raw):
                value = m[0].decode(encoding)
                if pattern.search(value):
                    rows.append(dict(rva=s['va'] + m.start(), encoding=encoding, text=value[:1024]))
    return rows


def analyze(pe, exports, imports, text_rows, depth=2, max_nodes=150, extra_roots=()):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_GRP_CALL, CS_GRP_JUMP, CS_GRP_RET
    from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    ranges = pe.runtime_functions()
    starts = [a for a, _ in ranges]
    iat = {r['iat_rva']: r['dll'] + '!' + r['symbol'] for r in imports}
    text_map = {r['rva']: r['text'] for r in text_rows}
    roots = {name: e['rva'] for e in exports if not e['forwarder'] for name in e['names']
             if group(name) != 'OTHER' and any(x in name for x in ('CreateFeature', 'EvaluateFeature', 'ReleaseFeature', 'Init'))}
    roots.update({f'manual_{rva:#x}': rva for rva in extra_roots})
    nodes = {}
    pending = collections.deque((rva, 0) for rva in roots.values())
    while pending and len(nodes) < max_nodes:
        rva, level = pending.popleft()
        if rva in nodes:
            continue
        idx = bisect.bisect_right(starts, rva) - 1
        end = ranges[idx][1] if idx >= 0 and ranges[idx][0] <= rva < ranges[idx][1] else rva + 128
        has_boundary = idx >= 0 and ranges[idx][0] <= rva < ranges[idx][1]
        sec = next((s for s in pe.sections if s['executable'] and s['va'] <= rva < s['va'] + s['size']), None)
        if not sec:
            nodes[rva] = dict(rva=rva, level=level, unresolved=['non-executable/unmapped target'], instructions=[], edges=[], xrefs=[])
            continue
        end = min(end, rva + 4096, sec['va'] + sec['size'])
        row = dict(rva=rva, level=level, boundary='pdata-fragment' if has_boundary else '128-byte-fallback',
                   unresolved=[], instructions=[], edges=[], xrefs=[])
        # Recursive basic-block traversal: do not decode bytes after a terminal instruction as code.
        blocks = collections.deque([rva])
        visited = set()
        while blocks and len(visited) < 512:
            pc = blocks.popleft()
            while rva <= pc < end and pc not in visited and len(visited) < 512:
                ins = next(decoder.disasm(pe.rread(pc, min(15, end - pc)), pe.base + pc, count=1), None)
                if ins is None:
                    row['unresolved'].append(f'decode failure at {pc:#x}')
                    break
                visited.add(pc)
                row['instructions'].append(dict(rva=pc, bytes=ins.bytes.hex(), mnemonic=ins.mnemonic, operands=ins.op_str))
                for op in ins.operands:
                    if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                        target = pc + ins.size + op.mem.disp
                        if target in text_map:
                            row['xrefs'].append(dict(at=pc, target=target, text=text_map[target]))
                if ins.group(CS_GRP_CALL) or ins.group(CS_GRP_JUMP):
                    op = ins.operands[0] if ins.operands else None
                    target = op.imm - pe.base if op and op.type == X86_OP_IMM else None
                    kind = 'call' if ins.group(CS_GRP_CALL) else 'branch'
                    edge = dict(at=pc, kind=kind, target=target)
                    if target is None:
                        slot = pc + ins.size + op.mem.disp if op and op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP else None
                        edge['import'] = iat.get(slot)
                        row['unresolved'].append(f'indirect {kind} at {pc:#x}: {edge.get("import") or ins.op_str}')
                    elif kind == 'branch' and rva <= target < end:
                        blocks.append(target)
                    elif level < depth:
                        pending.append((target, level + 1))
                    else:
                        row['unresolved'].append(f'depth limit at {pc:#x} -> {target:#x}')
                    row['edges'].append(edge)
                    if ins.mnemonic == 'jmp':
                        break
                if ins.group(CS_GRP_RET) or ins.mnemonic in ('int3', 'ud2'):
                    break
                pc += ins.size
                if pc >= end:
                    row['unresolved'].append('range boundary reached; continuation not resolved')
        if blocks or len(visited) >= 512:
            row['unresolved'].append('instruction budget reached')
        row['instructions'].sort(key=lambda x: x['rva'])
        # Only exact two-instruction constant-return bodies are identified; not a backend verdict.
        code = row['instructions']
        row['observation'] = 'UNRESOLVED_BACKEND'
        if len(code) == 2 and code[1]['mnemonic'] == 'ret':
            if code[0]['mnemonic'] == 'mov' and re.fullmatch(r'eax, (0x[0-9a-f]+|\d+)', code[0]['operands']):
                row['observation'] = 'CONSTANT_RETURN_BODY: ' + code[0]['operands']
            elif code[0]['mnemonic'] == 'xor' and code[0]['operands'] == 'eax, eax':
                row['observation'] = 'CONSTANT_RETURN_BODY: eax=0'
        nodes[rva] = row
    return dict(roots=roots, nodes=list(nodes.values()), pending_nodes=len(pending), depth=depth,
                limitations=['Direct edges only; indirect calls remain unresolved.',
                             'pdata gives fragments, not necessarily whole functions.',
                             'Shared targets do not prove shared state layout or kernel launch.',
                             'String presence and local RIP xrefs do not prove API support.'])


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding='utf-8')


def command_evidence(command):
    try:
        p = subprocess.run(command, capture_output=True, timeout=30)
        return dict(command=[str(x) for x in command], exit_code=p.returncode,
                    stdout=p.stdout.decode('utf-8', 'replace'), stderr=p.stderr.decode('utf-8', 'replace'))
    except (OSError, subprocess.TimeoutExpired) as exc:
        return dict(command=[str(x) for x in command], error=str(exc))


def file_version(path):
    """Read VERSIONINFO through the Windows resource API; does not load the target DLL."""
    if sys.platform != 'win32':
        return 'UNAVAILABLE: Windows version API required'
    import ctypes as c
    from ctypes import wintypes as w
    api = c.WinDLL('version', use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes = [w.LPCWSTR, c.POINTER(w.DWORD)]
    api.GetFileVersionInfoSizeW.restype = w.DWORD
    api.GetFileVersionInfoW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p]
    api.GetFileVersionInfoW.restype = w.BOOL
    api.VerQueryValueW.argtypes = [c.c_void_p, w.LPCWSTR, c.POINTER(c.c_void_p), c.POINTER(w.UINT)]
    api.VerQueryValueW.restype = w.BOOL
    size = api.GetFileVersionInfoSizeW(str(path), None)
    if not size:
        return 'UNAVAILABLE: no version resource'
    buf = c.create_string_buffer(size)
    ptr, length = c.c_void_p(), w.UINT()
    if not api.GetFileVersionInfoW(str(path), 0, size, buf) or not api.VerQueryValueW(buf, '\\', c.byref(ptr), c.byref(length)) or length.value < 16:
        return 'UNAVAILABLE: version query failed'
    sig, _, ms, ls = struct.unpack('<IIII', c.string_at(ptr, 16))
    if sig != 0xfeef04bd:
        return 'UNAVAILABLE: invalid fixed info'
    return '.'.join(str(v) for v in (ms >> 16, ms & 65535, ls >> 16, ls & 65535))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('dll', type=pathlib.Path)
    ap.add_argument('--output', type=pathlib.Path, required=True)
    ap.add_argument('--capstone', type=pathlib.Path)
    ap.add_argument('--dumpbin', type=pathlib.Path)
    ap.add_argument('--depth', type=int, default=2, choices=range(5))
    ap.add_argument('--root', action='append', type=lambda s: int(s, 0), default=[], help='additional investigation RVA; repeatable')
    ap.add_argument('--expected-sha256', help='refuse a different baseline before producing evidence')
    args = ap.parse_args()
    source, out = args.dll.resolve(), args.output.resolve()
    if not out.is_relative_to(EVIDENCE.resolve()) or out == EVIDENCE.resolve():
        ap.error('output must be a new child directory under evidence/D24')
    if out.exists():
        ap.error('output already exists; use a new run directory')
    data = source.read_bytes()
    if args.expected_sha256 and hashlib.sha256(data).hexdigest().lower() != args.expected_sha256.lower():
        ap.error('baseline SHA256 mismatch')
    pe = PE(data)
    exports = pe.exports()
    imports = pe.imports() + pe.imports(delay=True)
    text_rows = strings(pe)
    out.mkdir(parents=True)
    manifest = dict(schema=1, utc=dt.datetime.now(dt.timezone.utc).isoformat(), source=str(source),
                    sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), image_base=hex(pe.base),
                    file_version=file_version(source),
                    sections=pe.sections, expected_sha256=args.expected_sha256,
                    tool_sha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
                    python=sys.version, inspection='file-only; target DLL never loaded')
    manifest['gpu'] = command_evidence(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'])
    write_json(out / 'exports.json', exports)
    write_json(out / 'imports.json', imports)
    write_json(out / 'strings.json', text_rows)
    rows = ['ordinal\trva\tnames\tforwarder']
    rows += [f'{e["ordinal"]}\t{e["rva"]:#x}\t{",".join(e["names"]) or "<ordinal-only>"}\t{e["forwarder"] or ""}' for e in exports]
    (out / 'exports_parsed.txt').write_text('\n'.join(rows), encoding='utf-8')
    if args.dumpbin:
        for option, filename in [('/exports', 'exports.txt'), ('/imports', 'imports_dumpbin.txt')]:
            result = command_evidence([str(args.dumpbin.resolve()), '/nologo', option, str(source)])
            (out / filename).write_text(result.get('stdout', '') + result.get('stderr', '') + result.get('error', ''), encoding='utf-8')
            manifest[filename] = {k: v for k, v in result.items() if k not in ('stdout', 'stderr')}
    else:
        manifest['dumpbin'] = 'NOT_RUN: no --dumpbin; exports_parsed.txt is independent PE parsing, not dumpbin output'
    summary = ['# NGX export inventory', '', '| Group | Named exports | Init* | Shutdown* | Create/Evaluate/Release | GetCapabilityParameters / GetParameters |', '|---|---:|---|---|---|---|']
    names = [n for e in exports for n in e['names']]
    for g in (*GROUPS, 'OTHER'):
        ns = [n for n in names if group(n) == g]
        summary.append(f'| {g} | {len(ns)} | {any("_Init" in n for n in ns)} | {any("_Shutdown" in n for n in ns)} | ' +
                       ', '.join(f'{verb}={any(n.endswith("_" + verb) for n in ns)}' for verb in ('CreateFeature', 'EvaluateFeature', 'ReleaseFeature')) +
                       ' | ' + ', '.join(n for n in ns if n.endswith(('_GetCapabilityParameters', '_GetParameters'))) + ' |')
    aliases = collections.defaultdict(list)
    for e in exports:
        aliases[e['rva']].extend(e['names'])
    summary += ['', 'Missing parameter-management exports do NOT establish a snippet stub.', '', '## Shared RVAs', '']
    summary += [f'- {rva:#x}: ' + ', '.join(ns) for rva, ns in aliases.items() if len(ns) > 1]
    summary += ['', '## Vulkan extension-query names (exact match)', '']
    for suffix in ('RequiredExtensions', 'RequiredExtensions_Helper', 'GetFeatureDeviceExtensionRequirements', 'GetFeatureInstanceExtensionRequirements'):
        n = 'NVSDK_NGX_VULKAN_' + suffix
        summary.append(f'- {n}: {n in names}')
    summary += ['', 'See strings.json for static candidates; these are NOT returned extension requirements.',
                'See imports.json for normal and delay imports; absence does not rule out dynamic loading.',
                'Ordinal-only exports: ' + str(sum(not e['names'] for e in exports))]
    (out / 'exports_by_group.md').write_text('\n'.join(summary), encoding='utf-8')
    if args.capstone:
        sys.path.insert(0, str(args.capstone.resolve()))
    try:
        import capstone
    except ImportError:
        manifest['S1'] = 'NOT_RUN: capstone unavailable'
    else:
        manifest['capstone_version'] = capstone.__version__
        graph = analyze(pe, exports, imports, text_rows, args.depth, extra_roots=args.root)
        write_json(out / 'S1_graph.json', graph)
        by_rva = {n['rva']: n for n in graph['nodes']}
        lines = ['# S1 bounded evidence — not a backend verdict', '', '| Entry | RVA | Observation | State ctor / kernel launch |', '|---|---|---|---|']
        for name, rva in graph['roots'].items():
            lines.append(f'| {name} | {rva:#x} | {by_rva.get(rva, {}).get("observation", "NOT_VISITED")} | UNRESOLVED |')
        lines += ['', *graph['limitations'], '', f'Pending nodes: {graph["pending_nodes"]}', '']
        for node in graph['nodes']:
            lines += [f'## RVA {node["rva"]:#x}', '', 'First 30 reachable instructions (RVA addresses):', '```text']
            lines += [f'{i["rva"]:08x} {i["bytes"]:30} {i["mnemonic"]} {i["operands"]}' for i in node['instructions'][:30]]
            lines += ['```', '', 'Edges: ' + json.dumps(node['edges']), 'Unresolved: ' + json.dumps(node['unresolved']),
                      'String xrefs: ' + json.dumps(node['xrefs']), '']
        (out / 'S1_callgraph.md').write_text('\n'.join(lines), encoding='utf-8')
    manifest['source_unchanged'] = hashlib.sha256(source.read_bytes()).hexdigest() == manifest['sha256']
    write_json(out / 'manifest.json', manifest)
    write_json(out / 'sha256.json', {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir()) if p.is_file()})
    print(str(out))
    if not manifest['source_unchanged']:
        raise SystemExit('source changed during audit; discard conclusions')


if __name__ == '__main__':
    main()
