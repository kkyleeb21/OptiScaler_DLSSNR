#pragma once
#include <cstdint>
#include <d3d11_4.h>
#include <wrl/client.h>
template<class Context,class Fence,class Await>
HRESULT waitDx11Fence(Context* context,Fence* fence,HANDLE ready,UINT64& value,unsigned long long& polls,Await await){
  if(value==UINT64_MAX-1)return E_FAIL;
  const auto target=++value;
  HRESULT hr=context->Signal(fence,target);if(FAILED(hr))return hr;
  context->Flush();
  auto completed=fence->GetCompletedValue();++polls;
  if(completed==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
  if(completed>=target)return S_OK;
  hr=fence->SetEventOnCompletion(target,ready);if(FAILED(hr))return hr;
  const auto result=await(ready,2000);
  if(result!=WAIT_OBJECT_0)return result==WAIT_TIMEOUT?DXGI_ERROR_WAIT_TIMEOUT:E_FAIL;
  completed=fence->GetCompletedValue();++polls;
  if(completed==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
  // An event alone never proves completion.
  return completed>=target?S_OK:E_FAIL;
}
struct Dx11Completion {
 Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context;
 Microsoft::WRL::ComPtr<ID3D11Fence> fence;
 HANDLE ready=nullptr; UINT64 value=0;
 ~Dx11Completion(){if(ready)CloseHandle(ready);}
 bool initialize(ID3D11Device* device,ID3D11DeviceContext* ctx){
  Microsoft::WRL::ComPtr<ID3D11Device5> d;
  if(FAILED(device->QueryInterface(IID_PPV_ARGS(&d)))||FAILED(ctx->QueryInterface(IID_PPV_ARGS(&context))))return false;
  if(FAILED(d->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return false;
  ready=CreateEventW(nullptr,FALSE,FALSE,nullptr);return ready!=nullptr;
 }
 bool available()const{return fence&&context&&ready;}
 HRESULT wait(unsigned long long& polls){
  return waitDx11Fence(context.Get(),fence.Get(),ready,value,polls,[](HANDLE h,DWORD ms){return WaitForSingleObject(h,ms);});
 }
};
