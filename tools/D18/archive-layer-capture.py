"""Opt-in bounded archive adapter for the shared diagnostics collector. Never arms capture."""
import argparse, importlib.util, json, shutil
from pathlib import Path
spec=importlib.util.spec_from_file_location('layer_analysis',Path(__file__).with_name('analyze-layer-capture.py'))
analysis=importlib.util.module_from_spec(spec);spec.loader.exec_module(analysis)

def archive(source,output):
    result,files=analysis.inspect(source)
    out=Path(output)
    out.mkdir(parents=True,exist_ok=False)
    for file in files:shutil.copy2(file,out/file.name)
    # Validate the copied snapshot again; catches changes during a live read.
    copied,_=analysis.inspect(out)
    if copied['sha256']!=result['sha256']:raise ValueError('source changed during archive; retained snapshot is not verified')
    copied['source']=str(Path(source).resolve());analysis.write_report(copied,out)
    return copied

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('output');a=p.parse_args()
    r=archive(a.source,a.output);print(json.dumps(dict(complete=r['complete'],frames=r['frames'],raw_bytes=r['raw_bytes'])))
