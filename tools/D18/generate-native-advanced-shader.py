"""Embed the candidate's shared HLSL for native DX11 D3DCompile."""
from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True,type=Path);a=p.parse_args()
native=a.candidate/'native-source/bg3-native';shader=a.candidate/'core-source/OptiScaler/shaders/dlssnr/precompile'
names=['dlssnr.hlsl','dlssnr_multipass.hlsl']
source=(shader/names[1]).read_text(encoding='utf-8-sig').replace('#include "dlssnr.hlsl"',(shader/names[0]).read_text(encoding='utf-8-sig')).encode('utf-8')+b'\0'
(native/'advanced_shader.h').write_text('// Generated from candidate shared HLSL.\nstatic const char advancedSource[]={\n'+',\n'.join(','.join(str(x) for x in source[i:i+128]) for i in range(0,len(source),128))+'};\n',encoding='utf-8')
(native/'advanced-shader-provenance.json').write_text(json.dumps({n:hashlib.sha256((shader/n).read_bytes()).hexdigest() for n in names},indent=2),encoding='utf-8')
