"""Validate V7 numeric archive and summarize the observed temporal reprojection formula.
No SR execution or claim that the game's history rejection policy is suitable for DLSS.
"""
import argparse,json,struct,math,statistics,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log');p.add_argument('values');p.add_argument('--output',required=True);a=p.parse_args()
log=Path(a.log);value_path=Path(a.values)
if log.stat().st_size>128*1024*1024 or value_path.stat().st_size>32*1024*1024:raise SystemExit('input budget exceeded')
events=[json.loads(x) for x in log.read_text(encoding='utf-8-sig').splitlines()]
shots=[e for e in events if e.get('event')=='numeric_snapshot'];data=value_path.read_bytes()
if len(shots)>72:raise SystemExit('snapshot budget exceeded')
def stats(values):
 vals=[x for x in values if math.isfinite(x)]
 return dict(count=len(vals),min=min(vals) if vals else None,max=max(vals) if vals else None,mean=statistics.fmean(vals) if vals else None)
rows=[];ranges=[]
for shot in shots:
 if not shot['complete']:continue
 sizes=shot['sizes'];start=shot['offset'];end=start+sum(sizes)
 if len(sizes)!=4 or sizes[0]!=64*36*3*16 or any(n<=0 or n>65536 or n%16 for n in sizes[1:]) or not(0<=start<end<=len(data)):raise ValueError('invalid snapshot range')
 if any(start<old_end and end>old_start for old_start,old_end in ranges):raise ValueError('overlapping snapshot range')
 ranges.append((start,end));grid=struct.unpack_from('<'+str(sizes[0]//4)+'f',data,start);start+=sizes[0];cbs=[]
 for i in range(3):
  floats=struct.unpack_from('<'+str(sizes[i+1]//4)+'f',data,start);start+=sizes[i+1]
  first=shot['cb_first'][i]
  if first<0:raise ValueError('invalid constant buffer offset')
  cbs.append(floats[first*4:])
 if len(cbs[0])<161*4 or len(cbs[1])<43*4 or len(cbs[2])<19*4:raise ValueError('short constant buffer')
 if any(not math.isfinite(x) for cb in cbs for x in cb):
  rows.append(dict(id=shot['id'],frame=shot['frame'],valid=False,reason='nonfinite_constant_buffer'));continue
 view=cbs[0][160*4];t=[cbs[2][i:i+4] for i in range(0,len(cbs[2]),4)]
 depths=[];alpha=[];motion_x=[];motion_y=[];uv_errors=[];object_pixels=0;camera_pixels=0;clamped=0;invalid=0;dx=[];dy=[]
 for i in range(64*36):
  color=grid[i*12:i*12+4];z,mx,my,_=grid[i*12+4:i*12+8];u,v,qx,qy=grid[i*12+8:i*12+12]
  if not all(math.isfinite(x) for x in (*color,z,mx,my,u,v,qx,qy)):invalid+=1;continue
  depths.append(z);alpha.append(color[3]);motion_x.append(mx);motion_y.append(my)
  uv_errors.extend((qx-(t[11][2]*u-t[12][0])*view*t[11][0],qy-(t[11][3]*v-t[12][1])*t[11][1]))
  if z>=.005 and color[3]>0:
   object_pixels+=1;vx,vy=.5*mx,.5*my
   # Report the game clamp separately; use unmodified candidate displacement in stats.
   clamped+=max(abs(vx),abs(vy))>.05
   hx,hy=u+vx+t[12][2],v+vy+t[12][3]
  else:
   camera_pixels+=1;clip=(2*u-1,1-2*v,z,1)
   x=sum(c*k for c,k in zip(clip,t[13]));y=sum(c*k for c,k in zip(clip,t[14]));w=sum(c*k for c,k in zip(clip,t[16]))
   if abs(w)<1e-8:invalid+=1;continue
   hx,hy=.5*x/w+.5,-.5*y/w+.5
  if not(math.isfinite(hx) and math.isfinite(hy)):invalid+=1;continue
  dx.append((hx-u)*shot['width']);dy.append((hy-v)*shot['height'])
 rows.append(dict(id=shot['id'],session=shot['session'],group=shot['group'],step=shot['step'],frame=shot['frame'],valid=True,
                  nonfinite_or_singular_samples=invalid,object_branch_samples=object_pixels,camera_branch_samples=camera_pixels,game_large_motion_clamp_samples=clamped,
                  raw_depth=stats(depths),alpha=stats(alpha),raw_motion_x=stats(motion_x),raw_motion_y=stats(motion_y),uv_transform_error=stats(uv_errors),candidate_pixel_dx=stats(dx),candidate_pixel_dy=stats(dy),
                  view_scale=view,temporal_11=t[11],temporal_12=t[12],reprojection_rows=t[13:17],cb2_rows39_42=[cbs[1][i*4:i*4+4] for i in range(39,43)],resources=shot['resources']))
result=dict(schema='d18-input-values-summary-v1',archive_sha256=hashlib.sha256(data).hexdigest(),snapshots=rows,
            issued=[e for e in events if e.get('event')=='numeric_issued'],capture_stats=[e for e in events if e.get('event')=='numeric_stats'],
            failures=[e for e in events if e.get('event') in ('numeric_failure','numeric_pending_timeout')],complete_capture=False,sr_evaluation='not_tested',
            interpretation='Sparse diagnostic grid and a reconstruction of observed game formulas; not DLSS input acceptance. Motion direction, jitter convention, colour encoding and complete object coverage remain unvalidated.')
Path(a.output).write_text(json.dumps(result,indent=2,allow_nan=False));print('Complete snapshots:',len(rows),'Invalid snapshots:',sum(not x['valid'] for x in rows),'Failures:',len(result['failures']))
