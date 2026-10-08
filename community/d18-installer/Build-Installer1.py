"""Offline D18 release builder. Uses only explicitly supplied local components."""
import argparse, hashlib, json, os, shutil, subprocess, sys, zipfile
from pathlib import Path

S = Path(__file__).resolve().parents[2]
N = S / 'community/d18-installer'
def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest().upper()
def save(p, value):
    p.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
def files(p): return {x.relative_to(p).as_posix():x for x in sorted(p.rglob('*')) if x.is_file()}
def run(cmd, log, cwd=None, env=None):
    save(log.with_suffix('.command.json'), cmd)
    with log.open('w',encoding='utf-8') as f:
        r=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
    save(log.with_suffix('.exit.json'), {'exit_code':r.returncode})
    if r.returncode: raise RuntimeError(str(log)+' exit '+str(r.returncode))
def archive(base, target):
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,p in files(base).items():
            i=zipfile.ZipInfo(name,(2026,10,8,0,0,0)); i.compress_type=zipfile.ZIP_DEFLATED
            i.external_attr=0o100644<<16
            z.writestr(i,p.read_bytes())
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        assert set(z.namelist())==set(files(base))
        for n,p in files(base).items(): assert hashlib.sha256(z.read(n)).hexdigest().upper()==sha(p)

# Asset URL of the optional components ZIP on the GitHub release that carries this package.
OPTIONAL_URL='https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/download/dlssnr-d18-v0.4.0/DLSSNR_D18_0.4.0_optional_components.zip'
def main():
    a=argparse.ArgumentParser();a.add_argument('mode',choices=['core','stage','refresh','exe','zip'])
    a.add_argument('--build',required=True,type=Path);a.add_argument('--report',required=True,type=Path)
    a.add_argument('--previous',required=True,type=Path);args=a.parse_args()
    b=args.build.resolve();r=args.report.resolve();prev=args.previous.resolve()
    b.mkdir(exist_ok=True);r.mkdir(exist_ok=True)
    name='DLSSNR_D18_0.4.0_release';pkg=b/name;d=pkg/'D18';opt=b/'DLSSNR_D18_0.4.0_optional_components'
    head=subprocess.check_output(['git','-C',str(S),'rev-parse','HEAD'],text=True).strip()
    if args.mode=='core':
        out=b/'core-output';out.mkdir(exist_ok=True)
        envcmd=b/'compiler-env.cmd';envcmd.write_text('@echo off\nset PATH=C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer;%PATH%\ncall C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat >nul\nset\n')
        raw=subprocess.check_output(['cmd','/d','/c',str(envcmd)],text=True)
        env={k.upper():v for l in raw.splitlines() for k,sep,v in [l.partition('=')] if sep and k};env['CL']='/MP4 /FS';env['_CL_']='/Z7'
        (S/'OptiScaler/resource_build_date.h').write_text('#define VER_BUILD_DATE "20261008"\n')
        (S/'OptiScaler/resource_build_commit.h').write_text('#define VER_BUILD_COMMIT "'+head[:7]+'"\n')
        dep=Path('E:/DLSSNR/workspace/dlss5/worktrees/optiscaler-internal-scaling')
        env['LINK']=' '.join('/LIBPATH:"'+str(dep/'OptiScaler/library'/n)+'"' for n in ['fsr2','fsr2_212','fsr31','vulkan','d3dx','detours'])
        props=b/'release-build.props';props.write_text('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemDefinitionGroup><Link><GenerateDebugInformation>false</GenerateDebugInformation></Link></ItemDefinitionGroup></Project>\n')
        save(r/'core-build-environment.json',{k:env[k] for k in ['CL','_CL_','LINK']})
        cmd=[r'C:\BuildTools\MSBuild\Current\Bin\MSBuild.exe',str(S/'OptiScaler/OptiScaler.vcxproj'),'/p:Configuration=Release','/p:Platform=x64','/p:D18DiagnosticBuild=0','/p:SolutionDir='+str(dep)+'\\','/p:OutDir='+str(out)+'\\','/p:IntDir='+str(b/'core-obj')+'\\','/p:ForceImportBeforeCppTargets='+str(props),'/p:PostBuildEventUseInBuild=false','/p:PreBuildEventUseInBuild=false','/m:2','/nologo','/verbosity:minimal']
        run(cmd,r/'core-build.log',out,env)
    elif args.mode=='stage':
        if pkg.exists() or opt.exists(): raise RuntimeError('Stage already exists; use a new build directory')
        shutil.copytree(prev,pkg)
        for old in ['D18Install.exe','D18Uninstall.exe']:
            (pkg/old).unlink(missing_ok=True)
        # Diagnostic toolkit/source and old WinForms entry points are not release payload.
        for owned in [d/'tools',d/'runtime_patch_source']:
            if not owned.resolve().is_relative_to(b): raise RuntimeError('Cleanup target escapes build root: '+str(owned))
            if owned.exists(): shutil.rmtree(owned)
        for old in ['D18-DependencyDialog.ps1','D18-Setup.ps1','D18-Setup.cmd','D18-GuiCommon.ps1','public-tools.json']:
            (d/old).unlink(missing_ok=True)
        for p in d.glob('*.ps1'):
            if (N/p.name).is_file(): shutil.copy2(N/p.name,p)
        for p in [N/'D18-InstallerBridge.ps1']:
            shutil.copy2(p,d/p.name)
        shutil.copy2(b/'core-output/OptiScaler.dll',d/'payload/OptiScaler.dll')
        shutil.copy2(N/'OptiScaler.ini.d18',d/'payload/OptiScaler.ini.d18')
        for n in ['D24Native.dll','D18RuntimeCheck.exe']:
            shutil.copy2(b/'native-output'/n,d/'payload'/n)
        opt.mkdir()
        names=['amd_fidelityfx_framegeneration_dx12.dll','amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_upscaler_dx12.dll','amd_fidelityfx_vk.dll','libxell.dll','libxess.dll','libxess_dx11.dll','libxess_fg.dll']
        rows=[]
        for n in names:
            p=d/'payload/OptiScaler'/n; q=opt/'payload/OptiScaler'/n;q.parent.mkdir(parents=True,exist_ok=True)
            source=p if p.is_file() else prev.parent/prev.name.replace('_release','_optional_components')/'payload/OptiScaler'/n
            shutil.copy2(source,q);p.unlink(missing_ok=True);rows.append(dict(path='OptiScaler/'+n,size=q.stat().st_size,sha256=sha(q)))
        shutil.copytree(d/'payload/Licenses',opt/'Licenses')
        save(opt/'optional-manifest.json',dict(schema='d18-optional-v1',version='0.4.0',files=rows))
        save(d/'optional-components.json',dict(schema='d18-optional-v1',url=OPTIONAL_URL,files=rows))
        m=json.loads((d/'payload_manifest.json').read_text(encoding='utf-8-sig'))
        m['generated_at']='2026-10-08'
        m.update(release_name=name,release_version='0.4.0',version='0.4.0',source_commit=head[:7],source_changes='加 release040.patch / plus release040.patch',core_source_commit=head[:7],binary_source_commit=head[:7],baseline_commit=head[:7],installer_revision='wpf-release-040',contains_nvidia_runtime=False)
        for k in list(m):
            if 'candidate' in k:m.pop(k)
        m['files']=[dict(path=n.replace('/','\\'),size=p.stat().st_size,sha256=sha(p)) for n,p in files(d/'payload').items()]
        save(d/'payload_manifest.json',m)
        save(d/'SOURCE_PROVENANCE.json',dict(release_name=name,source_commit=head[:7],source_changes='加 release040.patch / plus release040.patch',uncommitted_changes=True,rebuilt_components=['OptiScaler.dll','D24Native.dll','D18RuntimeCheck.exe','D18Setup.exe'],retained_components='Local read-only 0.3.1 packages: forwarder, DXC, Agility SDK, optional SDKs; original vendor versions retained',nvidia_runtime_bundled=False))
        (d/'BUILD_PROFILE.txt').write_text('D18 0.4.0 Release x64; D18DiagnosticBuild=0; source db8776a plus release040.patch.\n')
        (d/'REVISION.txt').write_text(head[:7]+' + release040.patch\n')
        shutil.copy2(N/'VERSION',d/'VERSION')
        (pkg/'安装卸载说明_INSTALL_UNINSTALL.txt').write_text('运行 D18Setup.exe 安装或卸载。也可使用 D18 目录中的 PowerShell 脚本。Run D18Setup.exe to install or uninstall. PowerShell CLI scripts are in D18.\n',encoding='utf-8')
        (d/'SHA256SUMS.txt').unlink(missing_ok=True);(d/'ARCHIVE_SHA256SUMS.txt').unlink(missing_ok=True)
        archive(d,b/'embedded.zip')
    elif args.mode=='refresh':
        for p in d.glob('*.ps1'):
            if (N/p.name).is_file(): shutil.copy2(N/p.name,p)
        shutil.copy2(b/'core-output/OptiScaler.dll',d/'payload/OptiScaler.dll')
        shutil.copy2(N/'OptiScaler.ini.d18',d/'payload/OptiScaler.ini.d18')
        for n in ['D24Native.dll','D18RuntimeCheck.exe']:shutil.copy2(b/'native-output'/n,d/'payload'/n)
        m=json.loads((d/'payload_manifest.json').read_text(encoding='utf-8-sig'))
        m['generated_at']='2026-10-08'
        m['files']=[dict(path=n.replace('/','\\'),size=p.stat().st_size,sha256=sha(p)) for n,p in files(d/'payload').items()]
        save(d/'payload_manifest.json',m)
        for doc in ['README_CN.md','README.md','RELEASE_NOTES_CN.md','RELEASE_NOTES_EN.md','COMMON_FEATURES.md','VERSION']:
            shutil.copy2(N/doc,d/doc)
        # Include current hashes for conservative manual cleanup without losing historical catalog entries.
        cat=json.loads((d/'uninstall-catalog.json').read_text(encoding='utf-8-sig'))
        # The checker runs from the package and is never an installed game file.
        cat['files']=[x for x in cat['files'] if x['path'] not in ['D18RuntimeCheck.exe','_storage_\\D18RuntimeCheck.exe']]
        lookup={x['path']:x for x in cat['files']}
        for prefix in ['', '_storage_\\']:
            for proxy,source,kind in [(p,'OptiScaler.dll','core') for p in ['dxgi.dll','d3d12.dll','winmm.dll','version.dll','dbghelp.dll']]+[('D24Native.dll','D24Native.dll','native')]:
                target=prefix+proxy;digest=sha(d/'payload'/source)
                if target not in lookup:
                    lookup[target]=dict(path=target,kind=kind,sha256=[]);cat['files'].append(lookup[target])
                if digest not in lookup[target]['sha256']:lookup[target]['sha256'].append(digest)
        save(d/'uninstall-catalog.json',cat)
        oc=json.loads((d/'optional-components.json').read_text(encoding='utf-8-sig'));oc['url']=OPTIONAL_URL;save(d/'optional-components.json',oc)
        m.update(source_commit=head[:7],core_source_commit=head[:7],binary_source_commit=head[:7],baseline_commit=head[:7]);m.update(source_changes='加 release040.patch / plus release040.patch',source_tag='dlssnr-d18-v0.4.0',installer_revision='wpf-release-040');save(d/'payload_manifest.json',m)
        save(d/'SOURCE_PROVENANCE.json',dict(release_name=name,source_commit=head[:7],source_changes='加 release040.patch / plus release040.patch',source_tag='dlssnr-d18-v0.4.0',uncommitted_changes=True,rebuilt_components=['OptiScaler.dll','D24Native.dll','D18RuntimeCheck.exe','D18Setup.exe'],retained_components='0.3.1 packages: forwarder, DXC, Agility SDK, optional SDKs; original vendor versions retained',nvidia_runtime_bundled=False))
        (d/'BUILD_PROFILE.txt').write_text('D18 0.4.0 Release x64; D18DiagnosticBuild=0; source '+head[:7]+' plus release040.patch.'+chr(10));(d/'REVISION.txt').write_text(head[:7]+' + release040.patch'+chr(10))
        save(d/'release-manifest.json',dict(version='0.4.0',source_commit=head[:7],source_changes='加 release040.patch / plus release040.patch',source_tag='dlssnr-d18-v0.4.0',diagnostic_build=False,rebuilt_components=['OptiScaler.dll','D24Native.dll','D18RuntimeCheck.exe','D18Setup.exe'],retained_components='Forwarder, DXC, Agility and optional vendor SDKs from the read-only 0.3.1 packages; all D18 version resources rebuilt for 0.4.0',contains_nvidia_runtime=False,optional_url=OPTIONAL_URL,payload_files=m['files']))
        (d/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+n+'\n' for n,p in files(d).items() if n!='SHA256SUMS.txt'),encoding='utf-8')
        archive(d,b/'embedded.zip')
    elif args.mode=='exe':
        framework=Path(os.environ['SYSTEMROOT'])/'Microsoft.NET/Framework64/v4.0.30319'
        wpf=framework/'WPF'
        sma=Path(os.environ['SYSTEMROOT'])/'Microsoft.NET/assembly/GAC_MSIL/System.Management.Automation/v4.0_3.0.0.0__31bf3856ad364e35/System.Management.Automation.dll'
        cmd=[str(framework/'csc.exe'),'/nologo','/target:winexe','/platform:x64','/optimize+','/out:'+str(b/'D18Setup.exe'),'/win32manifest:'+str(N/'wpf/app.manifest'),'/resource:'+str(b/'embedded.zip')+',D18.bundle']
        for dll in ['System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','System.Management.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll','System.Windows.Forms.dll']:
            cmd.append('/reference:'+str(framework/dll))
        for dll in ['WindowsBase.dll','PresentationCore.dll','PresentationFramework.dll']:cmd.append('/reference:'+str(wpf/dll))
        cmd.append('/reference:'+str(sma));cmd += [str(p) for p in sorted((N/'wpf').glob('*.cs'))]
        run(cmd,r/'setup-build.log',b)
        shutil.copy2(b/'D18Setup.exe',pkg/'D18Setup.exe');shutil.copy2(N/'wpf/D18Setup.exe.config',b/'D18Setup.exe.config');shutil.copy2(N/'wpf/D18Setup.exe.config',pkg/'D18Setup.exe.config')
    else:
        (pkg/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+n+'\n' for n,p in files(pkg).items() if n!='SHA256SUMS.txt'),encoding='utf-8')
        archive(pkg,b/(name+'.zip'));archive(opt,b/(opt.name+'.zip'))
        artifacts=[b/(name+'.zip'),b/(opt.name+'.zip'),b/'D18Setup.exe',b/'core-output/OptiScaler.dll']
        (b/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(b).as_posix()+'\n' for p in artifacts),encoding='utf-8')
        save(r/'ARTIFACTS.json',[dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)) for p in artifacts])
        save(r/'PACKAGE_FILES.json',{base.name:[dict(path=n,bytes=p.stat().st_size,sha256=sha(p)) for n,p in files(base).items()] for base in [pkg,opt]})
    print(args.mode+' OK')
if __name__=='__main__': main()
