"""Fixed-stock, isolated native Vulkan NR validation with a bounded child process."""
import pathlib, hashlib, subprocess, datetime, shutil, json
ROOT=pathlib.Path(__file__).resolve().parents[3]
BUILD=ROOT/'builds/D24_Vulkan'
EXPECTED='e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e'
def main():
    runtime=BUILD/'nvngx_dlssnr.dll'
    if hashlib.sha256(runtime.read_bytes()).hexdigest()!=EXPECTED: raise RuntimeError('Stock hash mismatch')
    out=ROOT/'evidence/D24'/('vk-native-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
    shutil.copy2(pathlib.Path(__file__).with_name('probe.cpp'),out/'probe.cpp')
    shutil.copy2(pathlib.Path(__file__),out/'run.py')
    build=subprocess.run(['cmd','/d','/c',str(BUILD/'build.cmd')],capture_output=True,timeout=60)
    (out/'build.txt').write_bytes(build.stdout+build.stderr)
    if build.returncode: raise RuntimeError('Build failed: '+str(out))
    try:
        run=subprocess.run([str(BUILD/'nvngx.dll.vkprobe.exe')],cwd=out,capture_output=True,timeout=45)
        (out/'events.txt').write_bytes(run.stdout);(out/'stderr.txt').write_bytes(run.stderr)
        txt=run.stdout.decode(errors='replace')
        frames=[line for line in txt.splitlines() if line.startswith('frame=')]
        passed=run.returncode==0 and len(frames)==60 and all('evaluate=00000001' in x for x in frames) and 'pixel_bad=0' in txt and 'ngx_shutdown=00000001' in txt
        result={'passed':passed,'exit_code':run.returncode,'frames':len(frames),'stock_sha256':EXPECTED,'executable_sha256':hashlib.sha256((BUILD/'nvngx.dll.vkprobe.exe').read_bytes()).hexdigest(),'scope':'isolated host, 256x256 RGBA32F, constant guides, per-frame fences; not game proof'}
    except subprocess.TimeoutExpired as e:
        (out/'events.txt').write_bytes(e.stdout or b'');result={'passed':False,'timeout':True}
    (out/'result.json').write_text(json.dumps(result,indent=2))
    print(out);print(json.dumps(result,indent=2))
    if not result['passed']: raise SystemExit(1)
if __name__=='__main__': main()
