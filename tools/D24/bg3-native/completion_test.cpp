#include <windows.h>
#include "dx11_completion.h"
#include <cassert>
#include <cstdio>
struct Fence {
 UINT64 before=0,after=1;unsigned reads=0;HRESULT arm=S_OK;
 UINT64 GetCompletedValue(){return reads++?after:before;}
 HRESULT SetEventOnCompletion(UINT64,HANDLE){return arm;}
};
struct Context { HRESULT signal=S_OK;unsigned flushes=0; HRESULT Signal(Fence*,UINT64){return signal;} void Flush(){++flushes;} };
int main(){
 for(int scenario=0;scenario<8;++scenario){
  Context c;Fence f;UINT64 value=0;unsigned long long polls=0;DWORD result=WAIT_OBJECT_0;
  if(scenario==1)f.before=1;
  if(scenario==2)c.signal=E_FAIL;
  if(scenario==3)f.before=UINT64_MAX;
  if(scenario==4)f.arm=E_OUTOFMEMORY;
  if(scenario==5)result=WAIT_TIMEOUT;
  if(scenario==6)f.after=0;
  if(scenario==7)f.after=UINT64_MAX;
  auto hr=waitDx11Fence(&c,&f,nullptr,value,polls,[&](HANDLE,DWORD){return result;});
  assert(SUCCEEDED(hr)==(scenario<2));
  assert(polls<=2);assert(c.flushes==(scenario==2?0u:1u));
 }
 puts("8 completion paths PASS: ready, event, signal failure, device removal, arm failure, timeout, stale event, removal after wake");
}
