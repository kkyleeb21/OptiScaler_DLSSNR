"""Shared DX11 pixel decoder. Typeless guide interpretation matches adapter SRVs."""
from pathlib import Path
import numpy as np

def read_crop(root, row):
    name=row['file']
    if Path(name).name!=name or not name.startswith('D24Capture_'):
        raise ValueError('invalid_filename')
    if not row['ok']:raise ValueError('capture_write_failed')
    data=(Path(root)/name).read_bytes();w,h=row['width'],row['height'];fmt=row.get('format',10)
    layouts={2:('<f4',4),10:('<f2',4),15:('<f4',2),16:('<f4',2),19:('<f4',2),
             26:('<u4',1),33:('<f2',2),34:('<f2',2),39:('<f4',1),41:('<f4',1),
             44:('<u4',1),53:('<u2',1),56:('<u2',1)}
    if fmt not in layouts:raise ValueError('unsupported_format: '+str(fmt))
    dt,c=layouts[fmt];stride=w*c*np.dtype(dt).itemsize
    if min(w,h)<=0 or len(data)!=stride*h or row.get('row_bytes',stride)!=stride:raise ValueError('invalid_size')
    a=np.frombuffer(data,dtype=dt).reshape(h,w,c)
    if fmt==19:a=a[...,:1] # R32_FLOAT_X8X24_TYPELESS view, stencil is not depth
    elif fmt==44:a=(a&0xffffff).astype(np.float32)/16777215
    elif fmt in (53,56):a=a.astype(np.float32)/65535
    elif fmt==26:
        def uf(bits,m):
            exponent=bits>>m;mantissa=bits&((1<<m)-1)
            normal=(1+mantissa.astype(np.float32)/(1<<m))*np.exp2(exponent.astype(np.float32)-15)
            return np.where(exponent==0,mantissa.astype(np.float32)*2.0**(1-15-m),np.where(exponent==31,np.where(mantissa==0,np.inf,np.nan),normal))
        bits=a[...,0];a=np.stack([uf(bits&2047,6),uf((bits>>11)&2047,6),uf((bits>>22)&1023,5),np.ones((h,w))],-1)
    return a.astype(np.float32)

def statistics(a):
    finite=np.isfinite(a);v=a[finite]
    return dict(nonfinite=int((~finite).sum()),mean=float(v.mean()) if v.size else None,
                minimum=float(v.min()) if v.size else None,maximum=float(v.max()) if v.size else None,
                zero_fraction=float(np.mean(a==0)),
                channel_percentiles=[np.percentile(a[...,i][np.isfinite(a[...,i])],[0,1,50,99,100]).tolist() if np.isfinite(a[...,i]).any() else None for i in range(a.shape[-1])])
