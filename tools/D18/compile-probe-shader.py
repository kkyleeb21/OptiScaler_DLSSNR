"""Compile bounded local diagnostic HLSL with the system compiler; emit a header."""
import argparse,ctypes as c,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('output');a=p.parse_args()
data=Path(a.source).read_bytes()
if len(data)>64*1024:raise SystemExit('source budget exceeded')
dll=c.WinDLL('d3dcompiler_47.dll',winmode=0x800)
fn=dll.D3DCompile;fn.argtypes=[c.c_void_p,c.c_size_t,c.c_char_p,c.c_void_p,c.c_void_p,c.c_char_p,c.c_char_p,c.c_uint,c.c_uint,c.POINTER(c.c_void_p),c.POINTER(c.c_void_p)];fn.restype=c.c_long
blob=c.c_void_p();error=c.c_void_p();buf=c.create_string_buffer(data)
hr=fn(buf,len(data),None,None,None,b'main',b'cs_5_0',1<<15,0,c.byref(blob),c.byref(error))
def read(b):
 vt=c.cast(b,c.POINTER(c.POINTER(c.c_void_p))).contents
 ptr=c.WINFUNCTYPE(c.c_void_p,c.c_void_p)(vt[3])(b);size=c.WINFUNCTYPE(c.c_size_t,c.c_void_p)(vt[4])(b)
 data=c.string_at(ptr,size);c.WINFUNCTYPE(c.c_ulong,c.c_void_p)(vt[2])(b);return data
errors=read(error) if error else b''
if hr<0:raise SystemExit(errors.decode(errors='replace'))
code=read(blob);out=Path(a.output)
out.write_text('// Generated from probe-temporal-grid.hlsl; SHA256 '+hashlib.sha256(code).hexdigest()+'\ninline constexpr unsigned char numericBytecode[]={\n'+',\n'.join(','.join(str(x) for x in code[i:i+24]) for i in range(0,len(code),24))+'\n};\n')
print('Compiled',len(code),'bytes',hashlib.sha256(code).hexdigest())
