"""Compile only the candidate's advanced HLSL with its bundled SPIR-V DXC."""
from pathlib import Path
import argparse,subprocess,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True,type=Path);a=p.parse_args()
r=a.candidate.resolve();shaders=r/'core-source/OptiScaler/shaders';compiler=shaders/'shader_tools/dxc.exe';src=shaders/'dlssnr/precompile/dlssnr_multipass.hlsl';out=r/'shader-build';out.mkdir(exist_ok=True)
spv=out/'advanced.spv';command=[str(compiler),'-spirv','-T','cs_6_0','-E','CSMain','-D','VK_MODE=1','-fvk-use-dx-layout','-Fo',str(spv),str(src)]
run=subprocess.run(command,capture_output=True,timeout=90);(out/'compile.log').write_bytes(run.stdout+run.stderr)
if run.returncode:raise RuntimeError((run.stdout+run.stderr).decode(errors='replace'))
b=spv.read_bytes();assert b[:4]==b'\x03\x02\x23\x07'
header=src.parent/'DlssNr_Advanced_Vk.h'
# Keep the symbol consumed by DlssNr_Vk.cpp stable.
symbol='dlssnr_advanced_spv'
header.write_text('// Generated from shared advanced NR HLSL.\n#pragma once\nstatic const unsigned char '+symbol+'[]={\n'+',\n'.join(','.join(str(x) for x in b[i:i+128]) for i in range(0,len(b),128))+'};\n',encoding='utf-8')
record=dict(command=command,compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(),spirv_sha256=hashlib.sha256(b).hexdigest(),inputs={n:hashlib.sha256((src.parent/n).read_bytes()).hexdigest() for n in ['dlssnr.hlsl','dlssnr_multipass.hlsl']},header_sha256=hashlib.sha256(header.read_bytes()).hexdigest())
(out/'provenance.json').write_text(json.dumps(record,indent=2),encoding='utf-8');print(json.dumps(record))
