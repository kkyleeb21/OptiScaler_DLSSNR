"""Build isolated optimized release/diagnostic cores; no deployment/publication."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,subprocess,time
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True,type=Path);p.add_argument('--dependencies',required=True,type=Path);p.add_argument('--resume',action='store_true',help='Incrementally rebuild these same isolated profile directories');a=p.parse_args()
r=a.candidate.resolve();src=r/'core-source';dep=a.dependencies.resolve();assert r.is_relative_to(Path('E:/DLSSNR/builds'))
assert (src/'OptiScaler/dlssnr/BuildProfile.h').is_file() and (dep/'external').is_dir()
envtext=subprocess.check_output(['cmd','/d','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True)
env={k.upper():v for line in envtext.splitlines() for k,sep,v in [line.partition('=')] if k and sep}
msbuild=Path(env['VSINSTALLDIR'])/'MSBuild/Current/Bin/MSBuild.exe';results=[]
for name,flag in [('release',0),('diagnostic',1)]:
    output=r/'outputs'/name;obj=r/'intermediate'/name
    if not a.resume: assert not output.exists() and not obj.exists()
    assert output.resolve().is_relative_to(r) and obj.resolve().is_relative_to(r)
    output.mkdir(parents=True,exist_ok=a.resume);obj.mkdir(parents=True,exist_ok=a.resume)
    args=[str(msbuild),str(src/'OptiScaler/OptiScaler.vcxproj'),'/p:Configuration=Release','/p:Platform=x64','/p:D18DiagnosticBuild='+str(flag),'/p:SolutionDir='+str(dep)+'\\','/p:OutDir='+str(output)+'\\','/p:IntDir='+str(obj)+'\\','/p:PostBuildEventUseInBuild=false','/p:PreBuildEventUseInBuild=false','/m:2','/nologo','/verbosity:minimal']
    start=time.time()
    with (r/(name+'-build.log')).open('w',encoding='utf-8') as log:q=subprocess.run(args,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=900)
    dll=output/'OptiScaler.dll'
    row=dict(profile=name,diagnostic_build=flag,configuration='Release',command=args,exit_code=q.returncode,seconds=time.time()-start,sha256=hashlib.sha256(dll.read_bytes()).hexdigest().upper() if dll.is_file() else None,scope='D18 core candidate; not public release or gameplay acceptance',created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    (output/'build-profile.json').write_text(json.dumps(row,indent=2),encoding='utf-8');results.append(row)
    (r/'profile-build-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(name,'exit',q.returncode,'seconds',round(row['seconds'],2),'sha256',row['sha256'],flush=True)
    if q.returncode:raise SystemExit(q.returncode)
