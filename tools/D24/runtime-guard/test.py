"""Shared native/CLI guard checks plus real installer fixtures. Never launches a game."""
from pathlib import Path
import argparse,ctypes,hashlib,json,os,subprocess
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
ap=argparse.ArgumentParser()
for n in ('package','old-package','output','stock','community'):ap.add_argument('--'+n,required=True,type=Path)
a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=False)
checker=a.package/'payload/D18RuntimeCheck.exe';addon=ctypes.CDLL(str(a.package/'payload/D24Native.dll'))
native=addon.D24CheckRuntime;native.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_uint)];native.restype=ctypes.c_int
results=[]
def check(path,expected,label):
    p=subprocess.run([str(checker),str(path)],capture_output=True,text=True,timeout=30);r=json.loads(p.stdout)
    offset=ctypes.c_uint();accepted=native(str(path),ctypes.byref(offset))==1
    assert accepted==r['accepted']==expected,(label,r,accepted)
    assert offset.value==r['offset'];assert p.returncode==(0 if expected else 1)
    results.append(dict(case=label,kind='shared_guard',result='pass',details=r))
    print(label,'pass',flush=True)
original_hashes={str(p):sha(p) for p in (a.stock,a.community)}
check(a.stock,True,'stock');check(a.community,True,'community')
base=a.stock.read_bytes();mut=a.output/'mutation.dll'
def mutation(label,offset,value,expected):
    data=bytearray(base);data[offset:offset+len(value)]=value;mut.write_bytes(data);check(mut,expected,label)
mutation('model_data_change',0x100000,bytes([base[0x100000]^1]),True)
mutation('host_code_change',0x2000,bytes([base[0x2000]^1]),False)
mutation('patch_field_change',95238,bytes([base[95238]^1]),False)
rules=json.loads((Path(__file__).parent/'rules.json').read_text())
rdata=next(s for s in rules['profiles'][0]['layout']['sections'] if s['name']=='.rdata')
vt=rdata['offset']+0xb55f8-rdata['rva']
mutation('vtable_change',vt,bytes([base[vt]^1]),False)
mutation('wrong_arch',int.from_bytes(base[60:64],'little')+4,b'\x4c\x01',False)
mut.write_bytes(base[:256]);check(mut,False,'truncated')
# This mutation preserves the patch sites but changes the host code, testing preflight rejection.
data=bytearray(base);data[0x2000]^=1;mut.write_bytes(data)
shell=r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
env=dict(os.environ,PSModulePath=r'C:\Windows\System32\WindowsPowerShell\v1.0\Modules;C:\Program Files\WindowsPowerShell\Modules')
def run(package,script,game,label,*args,expected=0):
    with (a.output/(label+'.log')).open('wb') as log:
        p=subprocess.run([shell,'-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(package/script),'-GameDir',str(game),'-Yes',*args],stdout=log,stderr=subprocess.STDOUT,env=env,timeout=180)
    assert p.returncode==expected,(label,p.returncode,str(a.output/(label+'.log')))
def snapshot(game):
    state=json.loads((game/'.dlssnr-d18-install.json').read_text(encoding='utf-8-sig'))
    return {n:sha(game/n) for n in ['.dlssnr-d18-install.json']+[e['target_relative'] for e in state['files']]}
for api in ('DX11','Vulkan','None'):
    game=a.output/api;game.mkdir();(game/'Fixture.exe').write_text('Not executable; never launched')
    (game/'dxgi.dll').write_text('original proxy sentinel');proxy=sha(game/'dxgi.dll')
    # Old reference installation -> candidate with modified runtime -> self-upgrade -> uninstall.
    run(a.old_package,'Install-D18.ps1',game,api+'-old','-RuntimePath',str(a.stock),'-NativeApi',api,'-ProxyName','dxgi.dll')
    ini=game/'OptiScaler.ini';text=ini.read_text(encoding='utf-8-sig')+'\n[RuntimeGuardFixture]\nKeepMe=42\n';ini.write_text(text,encoding='utf-8');config=sha(ini)
    before=snapshot(game)
    if api=='DX11':
        run(a.package,'Install-D18.ps1',game,api+'-reject','-RuntimePath',str(mut),expected=1)
        assert snapshot(game)==before,'Rejected runtime modified the previous installation'
        assert 'DX11_LAYOUT_CONFLICT' in (a.output/(api+'-reject.log')).read_text(encoding='utf-8-sig')
    run(a.package,'Install-D18.ps1',game,api+'-upgrade','-RuntimePath',str(a.community))
    assert sha(ini)==config
    state=json.loads((game/'.dlssnr-d18-install.json').read_text(encoding='utf-8-sig'))
    runtime=game/('nvngx_dlssnr.dll' if api=='None' else 'D24Runtime.dll')
    assert sha(runtime)=='6A944B12FF62E36F23C3C0F0C896B60346891F5C3BFEC8D05B4E4C79C811B674'
    check(runtime,True,api+'-patched-community')
    if api=='DX11':assert state['runtime_layout']['accepted'] is True
    run(a.package,'Install-D18.ps1',game,api+'-self-upgrade')
    assert sha(ini)==config and sha(runtime)=='6A944B12FF62E36F23C3C0F0C896B60346891F5C3BFEC8D05B4E4C79C811B674'
    run(a.package,'Uninstall-D18.ps1',game,api+'-uninstall')
    assert sha(game/'dxgi.dll')==proxy and not runtime.exists()
    assert not (game/'D18RuntimeCheck.exe').exists()
    results.append(dict(case=api,kind='old_to_candidate_self_upgrade_uninstall',result='pass',config_preserved=True,original_proxy_restored=True))
    print(api,'install/upgrade/uninstall pass',flush=True)
for p,h in original_hashes.items():assert sha(Path(p))==h
(a.output/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print('ALL PASS',len(results),flush=True)
