#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <string>
#include <cstring>
#include <atomic>
#include "Sh0_Shader.h"

namespace DlssNr::Sh0 {
using Microsoft::WRL::ComPtr;
struct Constants {
    uint32_t width=0,height=0,mode=0,debug=0;
    float mid=.3f,fine=0,white=512,epsilon=.00019650281637217395f;
    float darkLo=.020636500883055946f,darkHi=.04127300176611189f,highLo=.5390625f,highHi=5.6875f;
    uint32_t originX=0,originY=0,validWidth=0,validHeight=0;
};
static_assert(sizeof(Constants)==64);
struct DebugSnapshot {uint32_t width=0,height=0,mode=0;float mid=0,fine=0;bool halfG2=false;std::vector<std::array<float,4>> values;uint64_t revision=0;std::string status="Off";};
inline std::mutex debugMutex;
inline std::atomic<bool> DebugEnabled{false}; // Session-only, never persisted.
inline DebugSnapshot debugSnapshot;
inline DebugSnapshot Snapshot(){std::lock_guard lock(debugMutex);return debugSnapshot;}
inline void Status(const char*s){std::lock_guard lock(debugMutex);debugSnapshot.status=s;}
inline void Transition(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&v);}
inline ComPtr<ID3D12Resource> Make(ID3D12Device*d,const D3D12_RESOURCE_DESC&r,D3D12_HEAP_TYPE h,D3D12_RESOURCE_STATES s){D3D12_HEAP_PROPERTIES p{};p.Type=h;ComPtr<ID3D12Resource>out;if(FAILED(d->CreateCommittedResource(&p,D3D12_HEAP_FLAG_NONE,&r,s,nullptr,IID_PPV_ARGS(&out))))return {};return out;}
inline ComPtr<ID3D12Resource> Buffer(ID3D12Device*d,UINT64 n,bool readback=false){D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=n;r.Height=1;r.DepthOrArraySize=1;r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return Make(d,r,readback?D3D12_HEAP_TYPE_READBACK:D3D12_HEAP_TYPE_UPLOAD,readback?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_GENERIC_READ);}
struct Pipeline {
 ComPtr<ID3D12Device>device;ComPtr<ID3D12RootSignature>root;ComPtr<ID3D12PipelineState>pso[3];
 bool Init(ID3D12Device*d){
  device=d;D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,2}};
  D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[1].DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC rd{2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob>blob,err;
  if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&err))||FAILED(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  const char*entries[]={"Horizontal","Vertical","Apply"};
  for(int i=0;i<3;i++){ComPtr<ID3DBlob>code;auto hr=D3DCompile(ShaderSource,strlen(ShaderSource),"D18_SH0",nullptr,nullptr,entries[i],"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&err);if(FAILED(hr))return false;D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={code->GetBufferPointer(),code->GetBufferSize()};if(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i]))))return false;}
  pso[0]->SetName(L"D18 SH0 Horizontal");pso[1]->SetName(L"D18 SH0 Vertical");pso[2]->SetName(L"D18 SH0 Apply");
  return true;
 }
};
struct Slot {
 std::shared_ptr<Pipeline>pipeline;ComPtr<ID3D12Resource>composed,horizontal,vertical,diagnostic,readback,constants;
 ComPtr<ID3D12DescriptorHeap>heap;std::function<bool()>complete;bool pendingDebug=false,halfG2=false;
 uint32_t width=0,height=0,debugWidth=0,debugHeight=0;D3D12_PLACED_SUBRESOURCE_FOOTPRINT debugFootprint{};Constants recorded;
 bool Ready()const{return !complete||complete();}
 void Poll(){
  if(!pendingDebug||!Ready())return;
  void*mem=nullptr;D3D12_RANGE range{0,SIZE_T(readback->GetDesc().Width)};
  if(SUCCEEDED(readback->Map(0,&range,&mem))){
   std::lock_guard lock(debugMutex);debugSnapshot.width=debugWidth;debugSnapshot.height=debugHeight;debugSnapshot.values.resize(size_t(debugWidth)*debugHeight);
   for(uint32_t y=0;y<debugHeight;y++)memcpy(debugSnapshot.values.data()+size_t(y)*debugWidth,(char*)mem+size_t(y)*debugFootprint.Footprint.RowPitch,debugWidth*16);
   ++debugSnapshot.revision;debugSnapshot.mode=recorded.mode;debugSnapshot.mid=recorded.mid;debugSnapshot.fine=recorded.fine;debugSnapshot.halfG2=halfG2;debugSnapshot.status="Completed GPU diagnostic; FI unchanged";D3D12_RANGE empty{};readback->Unmap(0,&empty);
  }
  pendingDebug=false;
 }
};
inline std::mutex retiredMutex;
inline std::vector<std::shared_ptr<Slot>> retired;
inline void Collect(){std::lock_guard lock(retiredMutex);for(auto it=retired.begin();it!=retired.end();)if((*it)->Ready())it=retired.erase(it);else ++it;}

class Renderer {
 std::shared_ptr<Pipeline>pipeline;
 // Three complete private scratch sets. Never reuse based on CPU frame age.
 std::array<std::shared_ptr<Slot>,3>slots{};
public:
 ~Renderer(){std::lock_guard lock(retiredMutex);for(auto&s:slots)if(s&&!s->Ready())retired.push_back(std::move(s));}
 void Poll(){Collect();for(auto&s:slots)if(s)s->Poll();}
 std::shared_ptr<Slot> Prepare(ID3D12Device*d,ID3D12Resource*target,Constants c,std::function<bool()>completed){
  Poll();const auto desc=target->GetDesc();
  {std::lock_guard lock(retiredMutex);if(retired.size()>=9){Status("SH0 bypass: retired submissions unresolved");return {};}}
  if((desc.Format!=DXGI_FORMAT_R11G11B10_FLOAT&&desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&desc.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT)||desc.SampleDesc.Count!=1||desc.DepthOrArraySize!=1){Status("SH0 bypass: unsupported input format");return {};}
  if(!pipeline){auto p=std::make_shared<Pipeline>();if(!p->Init(d)){Status("SH0 bypass: pipeline initialization failed");return {};}pipeline=p;}
  std::shared_ptr<Slot>*available=nullptr;for(auto&s:slots)if(!s||s->Ready()){available=&s;break;}
  if(!available){Status("SH0 bypass: all three submission slots pending");return {};}
  auto&s=*available;
  if(!s||s->width!=c.width||s->height!=c.height||s->composed->GetDesc().Format!=desc.Format){
   auto n=std::make_shared<Slot>();n->pipeline=pipeline;n->width=c.width;n->height=c.height;
   auto rd=desc;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;rd.MipLevels=1;rd.Alignment=0;
   n->composed=Make(d,rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   rd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;n->horizontal=Make(d,rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);n->vertical=Make(d,rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   n->constants=Buffer(d,256);D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=12;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
   if(!n->composed||!n->horizontal||!n->vertical||!n->constants||FAILED(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&n->heap)))){Status("SH0 bypass: scratch allocation failed");return {};}
   s=n;
  }
  if(c.debug&&!s->diagnostic){
   auto rd=desc;rd.Width=s->debugWidth=(c.width+63)/64;rd.Height=s->debugHeight=(c.height+63)/64;rd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;rd.MipLevels=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;rd.Alignment=0;rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
   s->diagnostic=Make(d,rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);UINT64 bytes=0;d->GetCopyableFootprints(&rd,0,1,0,&s->debugFootprint,nullptr,nullptr,&bytes);s->readback=Buffer(d,bytes,true);
   if(!s->diagnostic||!s->readback){s->diagnostic.Reset();s->readback.Reset();Status("SH0 bypass: diagnostic allocation failed");return {};}
  }
  void*mem=nullptr;D3D12_RANGE empty{};if(FAILED(s->constants->Map(0,&empty,&mem))){Status("SH0 bypass: constants map failed");return {};}memcpy(mem,&c,sizeof(c));s->constants->Unmap(0,nullptr);
  auto step=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=s->heap->GetCPUDescriptorHandleForHeapStart();
  for(int pass=0;pass<3;pass++){
   ID3D12Resource*srvs[]={s->composed.Get(),pass==0?s->composed.Get():pass==1?s->horizontal.Get():s->vertical.Get()};
   ID3D12Resource*dst=pass==0?s->horizontal.Get():pass==1?s->vertical.Get():target;
   for(auto*r:srvs){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=r->GetDesc().Format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d->CreateShaderResourceView(r,&v,cpu);cpu.ptr+=step;}
   for(auto*r:{dst,c.debug?s->diagnostic.Get():dst}){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=r->GetDesc().Format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d->CreateUnorderedAccessView(r,nullptr,&v,cpu);cpu.ptr+=step;}
  }
  s->recorded=c;s->complete=std::move(completed);Status("SH0 prepared");return s;
 }
 void Record(ID3D12GraphicsCommandList*cmd,std::shared_ptr<Slot>s,const Constants&c,bool onlyApply=false,ID3D12QueryHeap*queries=nullptr){
  auto*p=s->pipeline.get();auto*d=p->device.Get();auto inc=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto gpu=s->heap->GetGPUDescriptorHandleForHeapStart();
  Transition(cmd,s->composed.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12DescriptorHeap*heaps[]={s->heap.Get()};cmd->SetDescriptorHeaps(1,heaps);cmd->SetComputeRootSignature(p->root.Get());cmd->SetComputeRootConstantBufferView(0,s->constants->GetGPUVirtualAddress());
  if(queries)cmd->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,0);
  if(!onlyApply){
   cmd->SetPipelineState(p->pso[0].Get());cmd->SetComputeRootDescriptorTable(1,gpu);cmd->Dispatch((c.width+7)/8,(c.height+7)/8,1);
   Transition(cmd,s->horizontal.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   gpu.ptr+=4*inc;cmd->SetPipelineState(p->pso[1].Get());cmd->SetComputeRootDescriptorTable(1,gpu);cmd->Dispatch((c.width+7)/8,(c.height+7)/8,1);
   Transition(cmd,s->vertical.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }else Transition(cmd,s->vertical.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  gpu=s->heap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=8*inc;cmd->SetPipelineState(p->pso[2].Get());cmd->SetComputeRootDescriptorTable(1,gpu);cmd->Dispatch((c.width+7)/8,(c.height+7)/8,1);
  Transition(cmd,s->composed.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(!onlyApply)Transition(cmd,s->horizontal.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  Transition(cmd,s->vertical.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(queries)cmd->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,1);
  if(c.debug){
   Transition(cmd,s->diagnostic.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
   D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=s->diagnostic.Get();to.pResource=s->readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=s->debugFootprint;cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);Transition(cmd,s->diagnostic.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);s->pendingDebug=true;
  }
  Status("SH0 recorded; completion pending");
 }
};
}
