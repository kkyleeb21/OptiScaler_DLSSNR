"""Ensure a diagnostic graph cannot join unrelated frames or stale writers."""
import importlib.util
from pathlib import Path
s=importlib.util.spec_from_file_location('chain',Path(__file__).with_name('trace-resolution-chain.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def resource(uid,w,h,slot=0):return dict(present=True,resource_uid=uid,width=w,height=h,slot=slot)
def binding(sample,shader,inputs,outputs,frame=1):return dict(event='input_binding',context_type=0,session=1,sweep=1,frame=frame,device_id=1,context_id='a',sample=sample,shader_hash=shader,stage='PS',textures=inputs,render_targets=outputs)
index={'seed':['dcl_output o1.xyzw'],'copy':['dcl_resource_texture2d t0','dcl_output o0.xyzw'],'other':['dcl_output o0.xyzw']}
seed=binding(1,'seed',[],[resource(1,1280,720,1)])
copy=binding(2,'copy',[resource(1,1280,720)],[resource(2,1920,1080)])
result=m.trace([seed,copy],index,'seed',1);assert result['counts']=={'seed':1,'larger':1}
copy['frame']=2;assert len(m.trace([seed,copy],index,'seed',1)['paths'])==1
copy['frame']=1;copy['sample']=3
overwrite=binding(2,'other',[],[resource(1,1280,720)])
assert len(m.trace([seed,overwrite,copy],index,'seed',1)['paths'])==1
copy['context_type']=1;assert len(m.trace([seed,copy],index,'seed',1)['paths'])==1
print('Resolution chain: growth, frame isolation, writer invalidation, deferred exclusion passed')
