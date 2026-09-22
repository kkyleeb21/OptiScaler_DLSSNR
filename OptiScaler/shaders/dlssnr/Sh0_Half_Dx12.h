#pragma once
#include "Sh0_Dx12.h"
#include "Sh0_Half_Shader.h"
namespace DlssNr::Sh0 {
struct HalfPipeline {
 ComPtr<ID3D12Device>device;ComPtr<ID3D12RootSignature>root;ComPtr<ID3D12PipelineState>pso[5];
 bool Init(ID3D12Device*d){
  device=d;D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,3}};
  D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[1].DescriptorTable={2,ranges};
  D3D12_ROOT_SIGNATURE_DESC desc{2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob>blob,error;
  if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))||FAILED(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
  const char*entry[]={"Downsample","HalfHorizontal","HalfVertical","FineHorizontal","ApplyHalf"};
  for(int i=0;i<5;i++){ComPtr<ID3DBlob>code;auto hr=D3DCompile(HalfShaderSource,strlen(HalfShaderSource),"D18_SH0_HalfG2",nullptr,nullptr,entry[i],"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(FAILED(hr))return false;
   D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={code->GetBufferPointer(),code->GetBufferSize()};if(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i]))))return false;
  }
  return true;
 }
};
struct HalfSlot:Slot {std::shared_ptr<HalfPipeline>halfPipeline;ComPtr<ID3D12Resource>halfTemp;};
class HalfRenderer {
 std::shared_ptr<HalfPipeline>pipeline;std::array<std::shared_ptr<HalfSlot>,3>slots{};
public:
 ~HalfRenderer(){std::lock_guard lock(retiredMutex);for(auto&s:slots)if(s&&!s->Ready())retired.push_back(std::move(s));}
 void Poll(){Collect();for(auto&s:slots)if(s)s->Poll();}
 std::shared_ptr<HalfSlot>Prepare(ID3D12Device*d,ID3D12Resource*target,Constants c,std::function<bool()>complete){
  Poll();auto desc=target->GetDesc();
  {std::lock_guard lock(retiredMutex);if(retired.size()>=9){Status("Half G2 bypass: retired submissions unresolved");return {};}}
  if((desc.Format!=DXGI_FORMAT_R11G11B10_FLOAT&&desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&desc.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT)||desc.SampleDesc.Count!=1||desc.DepthOrArraySize!=1||!c.validWidth||!c.validHeight||c.originX+c.validWidth>c.width||c.originY+c.validHeight>c.height){Status("Half G2 bypass: unsupported format or valid rect");return {};}
  if(!pipeline){auto p=std::make_shared<HalfPipeline>();if(!p->Init(d)){Status("Half G2 bypass: pipeline initialization failed");return {};}pipeline=p;}
  std::shared_ptr<HalfSlot>*available=nullptr;for(auto&s:slots)if(!s||s->Ready()){available=&s;break;}
  if(!available){Status("Half G2 bypass: all three submission slots pending");return {};}
  auto&s=*available;
  if(!s||s->width!=c.width||s->height!=c.height||s->composed->GetDesc().Format!=desc.Format){
   auto n=std::make_shared<HalfSlot>();n->halfPipeline=pipeline;n->width=c.width;n->height=c.height;n->halfG2=true;
   auto rd=desc;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;rd.MipLevels=1;rd.Alignment=0;
   n->composed=Make(d,rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   rd.Format=DXGI_FORMAT_R32_FLOAT;n->horizontal=Make(d,rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   rd.Width=(c.width+1)/2;rd.Height=(c.height+1)/2;rd.Format=DXGI_FORMAT_R32G32_FLOAT;
   n->vertical=Make(d,rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);n->halfTemp=Make(d,rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   n->constants=Buffer(d,256);D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=25;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
   if(!n->composed||!n->horizontal||!n->vertical||!n->halfTemp||!n->constants||FAILED(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&n->heap)))){Status("Half G2 bypass: allocation failed");return {};}s=n;
  }
  if(c.debug&&!s->diagnostic){
   auto rd=desc;rd.Width=s->debugWidth=(c.width+63)/64;rd.Height=s->debugHeight=(c.height+63)/64;rd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;rd.MipLevels=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;rd.Alignment=0;rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
   s->diagnostic=Make(d,rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);UINT64 bytes=0;d->GetCopyableFootprints(&rd,0,1,0,&s->debugFootprint,nullptr,nullptr,&bytes);s->readback=Buffer(d,bytes,true);
   if(!s->diagnostic||!s->readback){s->diagnostic.Reset();s->readback.Reset();Status("Half G2 bypass: diagnostic allocation failed");return {};}
  }
  void*mem=nullptr;D3D12_RANGE empty{};if(FAILED(s->constants->Map(0,&empty,&mem))){Status("Half G2 bypass: constants map failed");return {};}memcpy(mem,&c,sizeof(c));s->constants->Unmap(0,nullptr);
  auto inc=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=s->heap->GetCPUDescriptorHandleForHeapStart();
  for(int pass=0;pass<5;pass++){
   auto*aux=pass==1?s->vertical.Get():pass==2?s->halfTemp.Get():pass==4?s->horizontal.Get():s->composed.Get();
   ID3D12Resource*srvs[]={s->composed.Get(),aux,pass==4?s->vertical.Get():s->composed.Get()};
   auto*dst=pass==0||pass==2?s->vertical.Get():pass==1?s->halfTemp.Get():pass==3?s->horizontal.Get():target;
   for(auto*r:srvs){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=r->GetDesc().Format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d->CreateShaderResourceView(r,&v,cpu);cpu.ptr+=inc;}
   for(auto*r:{dst,c.debug?s->diagnostic.Get():dst}){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=r->GetDesc().Format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d->CreateUnorderedAccessView(r,nullptr,&v,cpu);cpu.ptr+=inc;}
  }
  s->recorded=c;s->complete=std::move(complete);Status("Half G2 prepared");return s;
 }
 void Record(ID3D12GraphicsCommandList*cmd,std::shared_ptr<HalfSlot>s,const Constants&c,bool onlyApply=false,ID3D12QueryHeap*queries=nullptr){
  auto*p=s->halfPipeline.get();auto inc=p->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if(queries)cmd->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,0);
  Transition(cmd,s->composed.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12DescriptorHeap*heaps[]={s->heap.Get()};cmd->SetDescriptorHeaps(1,heaps);cmd->SetComputeRootSignature(p->root.Get());cmd->SetComputeRootConstantBufferView(0,s->constants->GetGPUVirtualAddress());
  auto dispatch=[&](int pass,bool half){auto gpu=s->heap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(pass)*5*inc;cmd->SetPipelineState(p->pso[pass].Get());cmd->SetComputeRootDescriptorTable(1,gpu);auto w=half?(c.width+1)/2:c.width,h=half?(c.height+1)/2:c.height;cmd->Dispatch((w+7)/8,(h+7)/8,1);};
  if(!onlyApply){
   dispatch(0,true);Transition(cmd,s->vertical.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   dispatch(1,true);Transition(cmd,s->halfTemp.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(cmd,s->vertical.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   dispatch(2,true);Transition(cmd,s->vertical.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(cmd,s->halfTemp.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   dispatch(3,false);
  }else Transition(cmd,s->vertical.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(cmd,s->horizontal.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  dispatch(4,false);
  Transition(cmd,s->composed.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Transition(cmd,s->horizontal.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Transition(cmd,s->vertical.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(queries)cmd->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,1);
  if(c.debug){Transition(cmd,s->diagnostic.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=s->diagnostic.Get();to.pResource=s->readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=s->debugFootprint;cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);Transition(cmd,s->diagnostic.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);s->pendingDebug=true;}
  Status("Half G2 recorded; completion pending");
 }
};
}
