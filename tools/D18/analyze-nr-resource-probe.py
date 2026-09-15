"""Validate bounded timing evidence and summarize per-process local memory, not game FPS."""
from pathlib import Path
import argparse,csv,hashlib,json
import numpy as np
p=argparse.ArgumentParser();p.add_argument('runs',nargs='+',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();assert not a.output.exists();a.output.mkdir(parents=True)
results=[];identity=None
for run in a.runs:
 m=json.loads((run/'manifest.json').read_text());assert m['schema']=='d18-nr-resource-probe-v1' and m['complete'] and m['exit_code']==0
 for file,h in m['files'].items():assert hashlib.sha256((run/file).read_bytes()).hexdigest().upper()==h
 contract={k:m[k] for k in ['executable_sha256','runtime_sha256','driver_sha256','debug_layer_enabled','ratio','style','passes','frames','warmup','cross_frame_gpu_wait']}
 if identity is None:identity=contract
 assert identity==contract,'Incompatible benchmark identity'
 rows=list(csv.DictReader((run/'timings.csv').open()));assert len(rows)==48 and [int(r['frame']) for r in rows]==list(range(48))
 frequencies={int(r['frequency']) for r in rows};assert len(frequencies)==1 and min(frequencies)>0
 values=np.array([[int(r['pass1_ticks']),int(r['pass2_ticks'])] for r in rows],dtype=np.float64)/next(iter(frequencies))*1000
 assert np.isfinite(values).all() and (values>0).all()
 def stats(v):return dict(mean_ms=float(v.mean()),median_ms=float(np.median(v)),p95_ms=float(np.percentile(v,95)),min_ms=float(v.min()),max_ms=float(v.max()))
 memory=m['memory_samples'];assert len(memory)==2+m['created_features']+48+1
 before=next(x['local_usage'] for x in memory if x['stage']=='before_features');end=next(x['local_usage'] for x in memory if x['stage']=='after_probe')
 row=dict(run=run.name,mode=m['mode'],width=m['width'],height=m['height'],ratio=m['ratio'],created_features=m['created_features'],nr_gpu_total=stats(values[16:].sum(axis=1)),per_pass=[stats(values[16:,i]) for i in range(2)],local_end_mib=end/1048576,local_delta_mib=(end-before)/1048576,observed_local_max_mib=max(x['local_usage'] for x in memory)/1048576,start=m['start'],end=m['end'])
 results.append(row)
(a.output/'analysis.json').write_text(json.dumps(dict(schema='d18-nr-resource-analysis-v1',identity=identity,runs=results,boundary='32 measured frames after 16 warmup; synthetic two-pass host, no game scene/FG/SR/compose. Local usage samples are not peak VRAM; no board power measured.'),indent=2),encoding='utf-8')
for r in results:print(r['run'],'MiB',r['local_end_mib'],'deltaMiB',r['local_delta_mib'],'GPU',r['nr_gpu_total'])
