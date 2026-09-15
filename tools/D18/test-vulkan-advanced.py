"""Build candidate production Vulkan chain/helper code into an owned real-NR host."""
from pathlib import Path
import argparse,subprocess,shutil,hashlib,json,os,time
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True,type=Path);p.add_argument('--runtime-dir',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--formats',action='store_true');a=p.parse_args()
r=a.candidate.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);core=r/'core-source/OptiScaler';external=Path('E:/DLSSNR/workspace/dlss5/worktrees/optiscaler-internal-scaling/external')
production=(core/'dlssnr/DlssNrFeature_Vk.cpp').read_text(encoding='utf-8-sig')
types=production[production.index('using PFN_VkProbe'):production.index('VkState g_vk;')]
# These optional exposure/capture/timer objects are not exercised by this chain
# fixture. Actual production ring, images, feature calls and shader code are used.
prefix='''#pragma once
#define NOMINMAX
#include <windows.h>
#include <memory>
#include <array>
#include <cmath>
#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include <dlssnr/NativeControlAbi.h>
#include <dlssnr/D24VkTracking.h>
#include <shaders/dlssnr/DlssNr_Vk.h>
#include "dx11_probe_parameters.h"
using namespace DlssNr;
struct NativeExposureVk{};struct NativeTimingVk{};namespace ColourCapture{struct Batch{};}
static NVSDK_NGX_Result HostAllocateParameters(NVSDK_NGX_Parameter** p){*p=new ProbeParameters;return NVSDK_NGX_Result_Success;}
static void HostDestroyParameters(NVSDK_NGX_Parameter* p){delete static_cast<ProbeParameters*>(p);}
#define NVSDK_NGX_VULKAN_AllocateParameters HostAllocateParameters
'''
helpers=production[production.index('void DestroyImage('):production.index('// ---------------------------------------------------------------------------------------------\n// Bring-up')]
text=prefix+types+'\nstatic VkState g_vk;\nstatic void Fail(const char* reason){g_vk.failed=true;printf("FAIL %s\\n",reason);}\n'+helpers+'\n#include "NativeAdvanced_Vk.inl"\n'
(out/'host-production.h').write_text(text,encoding='utf-8')
for n in ['nvngx_dlssnr.dll','nvngx.dll_dlssnr.dll']:shutil.copy2(a.runtime_dir/n,out/n)
source=Path(__file__).with_name('vulkan-advanced-host.cpp').resolve();testinc=Path('E:/DLSSNR/tools/D24/vulkan/test_include')
cmd=f'@echo off\ncall "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /O2 /EHsc /std:c++20 /wd4324 /I"{out}" /I"{testinc}" /I"{core}" /I"{core}/dlssnr" /I"{r}/native-source/bg3-native" /I"{external}/vulkan/include" /I"{external}/nvngx_dlss_sdk" "{source}" "{core}/shaders/Shader_Vk.cpp" "{core}/shaders/dlssnr/DlssNr_Vk.cpp" "{core}/library/vulkan/vulkan-1.lib" /Fo"{out}/" /Fe"{out}/host.exe"\n'
(out/'build.cmd').write_text(cmd,encoding='utf-8')
env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['Path']=os.environ['PATH']
run=subprocess.run(['cmd.exe','/d','/c',str(out/'build.cmd')],capture_output=True,cwd=out,env=env);(out/'build.log').write_bytes(run.stdout+run.stderr)
if run.returncode:print((run.stdout+run.stderr).decode(errors='replace'));raise SystemExit(run.returncode)
cases=[]
for fmt in ([109,97,122,37,91,64] if a.formats else [109]):
 start=time.monotonic();log=out/f'host-{fmt}.log'
 with log.open('wb') as f:
  try: run=subprocess.run([str(out/'host.exe'),str(fmt)],stdout=f,stderr=subprocess.STDOUT,cwd=out,timeout=180);code=run.returncode
  except subprocess.TimeoutExpired:code='timeout'
 cases.append(dict(format=fmt,exit=code,seconds=time.monotonic()-start,validation_layer_available='validation_layer=1' in log.read_text(errors='replace')))
 print(json.dumps(cases[-1]),flush=True)
 if code:break
result=dict(passed=all(c['exit']==0 for c in cases),cases=cases,scope='real Vulkan NR chain, production image/layout helpers and shader; isolated parameter host, not game/hook acceptance; partial-allocation failure and neutral composition checked',runtime={n:hashlib.sha256((a.runtime_dir/n).read_bytes()).hexdigest() for n in ['nvngx_dlssnr.dll','nvngx.dll_dlssnr.dll']},source={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [core/'dlssnr/DlssNrFeature_Vk.cpp',core/'dlssnr/NativeAdvanced_Vk.inl',core/'shaders/dlssnr/DlssNr_Vk.cpp']})
(out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result));raise SystemExit(0 if result['passed'] else 1)
