#pragma once
#include "Sh0_Dx12.h"
#include "V8_Passes.h"
#include "DlssNr_Common.h"
#include "precompile/V8Native_Dx12.h"
#include <dlssnr/V8Gate.h>
namespace DlssNr::V8Dx12 {
using Microsoft::WRL::ComPtr;
inline V8::Selection Select(int requested,const DlssNrConstants& c,bool ordinary,bool linearInput,bool linearOutput,bool prefilter,unsigned format){
 if(!requested)return {0,"default"};
 if(c.NetworkRatioX==1&&c.NetworkRatioY==1)return {0,"ratio100_identity"};
 if(format!=26&&format!=10)return {0,"requires_r11_or_fp16_output"};
 if(c.ValidX||c.ValidY||(c.ValidWidth&&c.ValidWidth!=c.Width)||(c.ValidHeight&&c.ValidHeight!=c.Height))return {0,"requires_full_frame_rect"};
 return V8::Select(requested,c,ordinary,linearInput,linearOutput,prefilter,97);
}
struct Pipeline{
 ComPtr<ID3D12Device>device;ComPtr<ID3D12RootSignature>root;ComPtr<ID3D12PipelineState>pso;
 bool Init(ID3D12Device*d){
  device=d;D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,4}};
  D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[1].DescriptorTable={2,ranges};
  D3D12_ROOT_SIGNATURE_DESC rd{2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob>b,e;
  if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&b,&e))||FAILED(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={v8_dx12,sizeof(v8_dx12)};
  if(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso))))return false;pso->SetName(L"D18 V8 fixed guided reconstruction");return true;
 }
};
struct Slot{
 std::shared_ptr<Pipeline>pipeline;std::array<ComPtr<ID3D12Resource>,V8::Count>scratch;
 ComPtr<ID3D12DescriptorHeap>heap;ComPtr<ID3D12Resource>constants;std::function<bool()>complete;
 bool Ready()const{return !complete||complete();}
};
inline std::mutex retiredMutex;
inline std::vector<std::shared_ptr<Slot>>retired;
class Renderer{
 std::shared_ptr<Pipeline>pipeline;std::array<std::shared_ptr<Slot>,3>slots{};bool failed=false;
public:
 const char*reason="unprepared";
 ~Renderer(){std::lock_guard lock(retiredMutex);for(auto&s:slots)if(s&&!s->Ready())retired.push_back(std::move(s));}
 void Poll(){std::lock_guard lock(retiredMutex);for(auto it=retired.begin();it!=retired.end();)if((*it)->Ready())it=retired.erase(it);else ++it;}
 std::shared_ptr<Slot>Prepare(ID3D12Device*d,ID3D12Resource*sr,ID3D12Resource*input,ID3D12Resource*mo,ID3D12Resource*target,const DlssNrConstants& c,std::function<bool()>complete){
  Poll();if(failed){reason="initialization_failed_using_original";return {};}
  {std::lock_guard lock(retiredMutex);if(retired.size()>=9){reason="retired_submissions_pending";return {};}}
  auto fail=[&](const char*why)->std::shared_ptr<Slot>{failed=true;reason=why;return {};};
  for(auto*r:{sr,input,mo,target}){if(!r){reason="missing_resource";return {};};auto t=r->GetDesc();if(t.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||t.Width!=c.Width||t.Height!=c.Height||t.DepthOrArraySize!=1||t.SampleDesc.Count!=1||t.MipLevels!=1){reason="unsupported_resource_shape";return {};}}
  if(!pipeline){auto p=std::make_shared<Pipeline>();if(!p->Init(d))return fail("pipeline_failed_using_original");pipeline=p;}
  std::shared_ptr<Slot>*available=nullptr;for(auto&s:slots)if(!s||s->Ready()){available=&s;break;}
  if(!available){reason="three_submissions_pending";return {};}
  auto&s=*available;
  if(!s){auto n=std::make_shared<Slot>();n->pipeline=pipeline;auto desc=target->GetDesc();desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;desc.Alignment=0;
   for(int i=0;i<V8::Count;++i){desc.Width=V8::ScratchWidth(i,c.Width);desc.Height=V8::ScratchHeight(i,c.Height);n->scratch[i]=Sh0::Make(d,desc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);if(!n->scratch[i])return fail("allocation_failed_using_original");}
   n->constants=Sh0::Buffer(d,9*256);D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=9*6;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
   if(!n->constants||FAILED(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&n->heap))))return fail("descriptor_allocation_failed");s=n;
  }
  auto resource=[&](int i)->ID3D12Resource*{if(i>=0)return s->scratch[i].Get();if(i==V8::SourceOriginal)return sr;if(i==V8::ProxyInput)return input;if(i==V8::ModelOutput)return mo;if(i==V8::Destination)return target;return nullptr;};
  void*mem=nullptr;D3D12_RANGE empty{};if(FAILED(s->constants->Map(0,&empty,&mem)))return fail("constants_map_failed");
  auto cpu=s->heap->GetCPUDescriptorHandleForHeapStart();auto inc=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);unsigned ordinal=0;
  for(auto&p:V8::Passes(c.Width,c.Height)){
   auto cc=c;cc.Mode=p.mode;memcpy((char*)mem+ordinal*256,&cc,sizeof(cc));++ordinal;
   for(int i:{p.source,p.model,p.original,p.coeff}){auto*r=resource(i);D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=r?r->GetDesc().Format:DXGI_FORMAT_R32G32B32A32_FLOAT;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d->CreateShaderResourceView(r,&v,cpu);cpu.ptr+=inc;}
   for(int i:{p.target,p.keep}){auto*r=resource(i);D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=r?r->GetDesc().Format:DXGI_FORMAT_R32G32B32A32_FLOAT;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d->CreateUnorderedAccessView(r,nullptr,&v,cpu);cpu.ptr+=inc;}
  }
  s->constants->Unmap(0,nullptr);s->complete=std::move(complete);reason="v8_fixed";return s;
 }
 void Record(ID3D12GraphicsCommandList*cmd,const std::shared_ptr<Slot>&s,const DlssNrConstants&c){
  auto*p=s->pipeline.get();ID3D12DescriptorHeap*heaps[]={s->heap.Get()};cmd->SetDescriptorHeaps(1,heaps);cmd->SetComputeRootSignature(p->root.Get());cmd->SetPipelineState(p->pso.Get());
  const auto inc=p->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);std::array<D3D12_RESOURCE_STATES,V8::Count>states;states.fill(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);unsigned ordinal=0;
  auto transition=[&](int i,D3D12_RESOURCE_STATES to){if(i>=0){Sh0::Transition(cmd,s->scratch[i].Get(),states[i],to);states[i]=to;}};
  for(auto&pass:V8::Passes(c.Width,c.Height)){
   for(int i:{pass.source,pass.model,pass.original,pass.coeff})transition(i,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   transition(pass.target,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(pass.keep,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   auto gpu=s->heap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(ordinal)*6*inc;cmd->SetComputeRootConstantBufferView(0,s->constants->GetGPUVirtualAddress()+ordinal*256);cmd->SetComputeRootDescriptorTable(1,gpu);cmd->Dispatch((pass.width+7)/8,(pass.height+7)/8,1);++ordinal;
  }
  for(int i=0;i<V8::Count;++i)transition(i,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 }
};
}
