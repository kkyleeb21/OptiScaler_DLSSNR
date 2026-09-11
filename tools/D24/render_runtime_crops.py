"""Render latest four-stage crop group from diagnostic log (display flip optional)."""
import argparse,json
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--flip-y',action='store_true');a=p.parse_args()
s=json.loads((a.folder/'summary.json').read_text());rows={}
for r in s['crops']:
 if r.get('coverage')=='center_crop': rows.setdefault(r['frame'],{})[r['stage']]=r
frames=sorted(f for f,r in rows.items() if len(r)==4)[-4:]
stages=['sr','input','model','composed'];size=384
im=Image.new('RGB',(size*4,(size+22)*len(frames)));draw=ImageDraw.Draw(im)
for iy,f in enumerate(frames):
 for ix,stage in enumerate(stages):
  r=rows[f][stage];v=np.fromfile(a.folder/r['file'],dtype='<f2').reshape(r['height'],r['width'],4)[:,:,:3].astype(float)
  if a.flip_y:v=v[::-1]
  v=np.clip(v,0,1)
  if stage in ('sr','composed'):v=np.where(v<=.0031308,v*12.92,1.055*v**(1/2.4)-.055)
  im.paste(Image.fromarray((v*255).astype('uint8')).resize((size,size)),(ix*size,iy*(size+22)+22));draw.text((ix*size,iy*(size+22)),f'{f} {stage}',fill='white')
im.save(a.folder/'crop-grid.png')
