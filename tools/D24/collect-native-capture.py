"""Snapshot native capture evidence and run the shared summaries once, offline."""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path

def collect(game,out,regions):
    if out.exists():raise ValueError('Use a new evidence directory; existing evidence is preserved')
    log=game/'D24Native.log'
    text=log.read_text(encoding='utf-8-sig');out.mkdir(parents=True)
    (out/log.name).write_text(text,encoding='utf-8')
    names=set();manifest=[];missing=[]
    for line in text.splitlines():
        try:r=json.loads(line)
        except ValueError:continue
        if r.get('event')=='dx11_crop':
            name=r['file']
            if Path(name).name!=name or not name.startswith('D24Capture_'):raise ValueError('Invalid capture filename')
            names.add(name)
    names.update(('OptiScaler.log','OptiScaler.ini','D24CaptureStatus.txt'))
    for name in sorted(names):
        src=game/name
        if not src.is_file():missing.append(name);continue
        shutil.copy2(src,out/name)
        manifest.append(dict(file=name,bytes=(out/name).stat().st_size,sha256=hashlib.sha256((out/name).read_bytes()).hexdigest()))
    (out/'snapshot.json').write_text(json.dumps(dict(files=manifest,missing=missing,game=str(game),limitations=['Snapshot while running may end in a partial capture; missing observations are reported.']),indent=2))
    tools=Path(__file__).parent
    subprocess.run([sys.executable,str(tools/'summarize_runtime.py'),str(out/log.name),'--output',str(out/'runtime-summary.json')],check=True)
    subprocess.run([sys.executable,str(tools/'analyse-colour-capture.py'),str(out/log.name),'--dx11-import',str(out/'packets'),'--out',str(out/'analysis.json')],check=True)
    args=[sys.executable,str(tools/'analyse-capture-sequence.py'),str(out/'packets'),'--out',str(out/'sequence.json')]
    for region in regions:args+=['--region',region]
    subprocess.run(args,check=True)
    analysis=json.loads((out/'analysis.json').read_text());coverage=json.loads((out/'packets/coverage.json').read_text())
    guide_complete=sum(all(v['coverage']=='compared' for v in a['guide_checks'].values()) for a in analysis)
    (out/'READOUT.md').write_text(f'# Native capture evidence\n\nComplete colour frames: {len(analysis)}\n\nFrames with all guide comparisons: {guide_complete}\n\nMissing files: {len(missing)}\n\nSee analysis.json for guide conversion and post-model mutation checks; sequence.json for temporal comparisons; runtime-summary.json for capture duration, reset, epoch and errors.\n\nThis is diagnostic coverage, not a gameplay or flicker verdict.\n',encoding='utf-8')
    print(json.dumps(dict(evidence=str(out),colour_frames=len(analysis),guide_frames=guide_complete,progress=coverage['progress'][-1:])))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--game',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--region',action='append',default=[]);a=p.parse_args()
    collect(a.game,a.out,a.region)
