// Experimental, fingerprint-gated SR adapter. Included in D18InputProbe.
#include "../dlssnr/Dx11DrawReplay.h"
#include "../dlssnr/SrQualityMode.h"
namespace Wildlands {
using Microsoft::WRL::ComPtr;
namespace Status=DlssNr::WildlandsSr;
namespace Stages=DlssNr::DiagnosticStages;
#include "D18WildlandsSrBytecode.h"
inline std::atomic<bool> enabled{false};
inline ComPtr<ID3D11Buffer> trackedCB;
inline std::atomic<ID3D11Resource*> tracked{nullptr};
inline float shadow[80]{};inline bool shadowValid=false;
inline unsigned long long shadowFrame=0,lastRun=~0ULL,lastLog=0,attempts=0,waits=0;
inline FILE* log=nullptr;
inline ComPtr<ID3D11DeviceContext1> context;

inline ComPtr<ID3D11ComputeShader> prepare;
inline ComPtr<ID3D11VertexShader> fullscreen;
inline ComPtr<ID3D11PixelShader> compose;
inline ComPtr<ID3D11RenderTargetView> offscreenRT;
inline ComPtr<ID3D11RasterizerState> raster;
inline ComPtr<ID3D11Buffer> constants;
inline ComPtr<ID3D11Texture2D> tex[5];
inline ComPtr<ID3D11ShaderResourceView> srv[5];
inline ComPtr<ID3D11UnorderedAccessView> uav[4];
inline DlssNr::NativeNrDx11Bridge nativeNr;
inline NVSDK_NGX_Parameter* params=nullptr;
inline NVSDK_NGX_Handle* handle=nullptr;
inline UINT displayWidth=0,displayHeight=0;
inline DlssNr::SrResolutionContract resolution;
inline UINT width=0,height=0;inline bool initialized=false,fault=false,reset=true;
inline void Event(const char* stage,long code=0){
 static unsigned records=0;if(!log||records>=600)return;++records;fprintf(log,"{\"event\":\"wildlands_sr\",\"stage\":\"%s\",\"code\":%ld,\"tick\":%llu,\"frame\":%llu,\"attempts\":%llu,\"composed\":%llu,\"waiting\":%llu,\"width\":%u,\"height\":%u,\"output_width\":%u,\"output_height\":%u}\n",stage,code,GetTickCount64(),frameNumber,attempts,Status::frames.load(),waits,width,height,resolution.outputWidth,resolution.outputHeight);fflush(log);
}
inline void Fail(const char* stage,long code){if(fault)return;fault=true;Status::failed=true;Status::code=code;reset=true;Event(stage,code);}
struct GpuFrame {ComPtr<ID3D11Query> stage[3];bool pending=false;unsigned done=0,issued=0;unsigned diagnosticStage=Stages::Full;bool producesSr=false;unsigned long long id=0,tick=0;};
inline GpuFrame gpuFrames[4];
inline unsigned long long gpuSubmitted=0,gpuComplete=0,gpuDrained=0,gpuStages[3]{},gpuBusy=0;
inline void GpuEvent(const char* stage,long code=0){
 static unsigned records=0;if(!log||records++>=128)return;
 fprintf(log,"{\"event\":\"wildlands_sr_gpu\",\"stage\":\"%s\",\"code\":%ld,\"submitted\":%llu,\"completed\":%llu,\"prepare_done\":%llu,\"evaluate_done\":%llu,\"compose_done\":%llu,\"busy_skips\":%llu,\"tick\":%llu}\n",stage,code,gpuSubmitted,gpuComplete,gpuStages[0],gpuStages[1],gpuStages[2],gpuBusy,GetTickCount64());fflush(log);
}
inline void PollGpu(){
 if(!context)return;
 for(auto& f:gpuFrames){if(!f.pending)continue;
  while(f.done<f.issued){BOOL ready=FALSE;const HRESULT hr=context->GetData(f.stage[f.done].Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH);
   if(FAILED(hr)){GpuEvent(f.done==0?"prepare_query_error":f.done==1?"evaluate_query_error":"compose_query_error",hr);ComPtr<ID3D11Device> device;context->GetDevice(&device);Event("device_removed_reason",device->GetDeviceRemovedReason());Fail("gpu_completion_failed",hr);return;}
   if(hr!=S_OK||!ready){if(GetTickCount64()-f.tick>1500&&!fault){GpuEvent(f.done==0?"prepare_timeout":f.done==1?"evaluate_timeout":"compose_timeout",WAIT_TIMEOUT);Fail("gpu_pending_timeout",WAIT_TIMEOUT);}break;}
   if(f.diagnosticStage!=Stages::OriginalWriteback&&f.diagnosticStage!=Stages::CopyOnly&&f.diagnosticStage!=Stages::OffscreenReplay)++gpuStages[f.done];++f.done;
  }
  if(f.done==f.issued){f.pending=false;++gpuDrained;if(Status::stagePanel)++Status::stageGpu[f.diagnosticStage];if(f.producesSr&&f.issued==3){++gpuComplete;Status::gpuCompleted=gpuComplete;Status::lastGpuTick=GetTickCount64();}}
 }
}
inline GpuFrame* ReserveGpu(ID3D11Device* device){
 for(auto& f:gpuFrames)if(!f.pending){
  for(auto& q:f.stage)if(!q){D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};auto hr=device->CreateQuery(&desc,&q);if(FAILED(hr)){Fail("gpu_query_allocation",hr);return nullptr;}}
  f.done=0;f.issued=0;f.producesSr=false;return &f;
 }
 ++gpuBusy;reset=true;return nullptr;
}
inline void PresetEvent(const char* stage,unsigned hint){
 static unsigned records=0;if(!log||records>=64)return;++records;
 fprintf(log,"{\"event\":\"sr_preset\",\"stage\":\"%s\",\"hint\":%u,\"submitted\":%llu,\"drained\":%llu,\"tick\":%llu}\n",stage,hint,gpuSubmitted,gpuDrained,GetTickCount64());fflush(log);
}
inline void Enable(const std::filesystem::path& root){
 enabled=GetFileAttributesW((root/L"D18WildlandsSR.enabled").c_str())!=INVALID_FILE_ATTRIBUTES;
 const auto modes=DlssNr::NativeSrProfile::Resolve(enabled,
 GetFileAttributesW((root/L"D18SrDiagnostics.panel.enabled").c_str())!=INVALID_FILE_ATTRIBUTES,
 GetFileAttributesW((root/L"D18SrDiagnostics.post-replay.enabled").c_str())!=INVALID_FILE_ATTRIBUTES,
 GetFileAttributesW((root/L"D18SrDiagnostics.upscale-output.enabled").c_str())!=INVALID_FILE_ATTRIBUTES,
 GetFileAttributesW((root/L"D18SrDiagnostics.evaluate-only").c_str())!=INVALID_FILE_ATTRIBUTES,
 GetFileAttributesW((root/L"D18SrDiagnostics.native-handoff.enabled").c_str())!=INVALID_FILE_ATTRIBUTES);
 Status::stagePanel=modes.stagePanel;Status::postReplay=modes.postReplay;
 Status::offscreenUpscale=modes.upscale;Status::evaluateOnly=modes.evaluateOnly;Status::nativeHandoff=modes.nativeHandoff;
 if(enabled){auto* config=Config::Instance();const unsigned hint=config->RenderPresetOverride.value_or_default()?config->RenderPresetForAll.value_or(config->RenderPresetDLAA.value_or_default()):0;
 Status::preset.requested=hint;Status::preset.prepared=hint;}
 if(enabled){Status::available=true;Status::enabled=Config::Instance()->DlssNrNativeSrEnabled.value_or_default()&&GetFileAttributesW((root/L"D18WildlandsSR.start-disabled").c_str())==INVALID_FILE_ATTRIBUTES;if(Config::Instance()->DlssNrDiagnostics.value_or_default()!=0)log=_wfsopen((root/L"D18WildlandsSR.jsonl").c_str(),L"wb",_SH_DENYNO);Event(Status::enabled?"enabled_dlaa_preview":"armed_start_disabled");if(Status::stagePanel){Status::requestedStage=Status::activeStage=Stages::Observe;Event("diagnostic_stage_panel_armed");}else if(Status::evaluateOnly)Event("diagnostic_evaluate_only_armed");}
}
// Only shadow the selected, exact-size temporal buffer. No GPU readback or stalls.
inline void Shadow(ID3D11DeviceContext* c,ID3D11Resource* r,const void* data){
 if(internalWork||!data||!enabled.load()||r!=tracked.load()||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 std::lock_guard lock(guard);if(r!=tracked.load())return;memcpy(shadow,data,sizeof(shadow));shadowValid=true;shadowFrame=frameNumber;
}
using MapFn=HRESULT(WINAPI*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
using UnmapFn=void(WINAPI*)(ID3D11DeviceContext*,ID3D11Resource*,UINT);
using UpdateFn=void(WINAPI*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);
inline MapFn map=nullptr;inline UnmapFn unmap=nullptr;inline UpdateFn update=nullptr;
inline thread_local ID3D11Resource* mappedResource=nullptr;
inline thread_local void* mappedData=nullptr;
inline HRESULT WINAPI OnMap(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub,D3D11_MAP mode,UINT flags,D3D11_MAPPED_SUBRESOURCE* out){
 CommandLists::Record<14>(c,r,sub,mode,flags,out);
 if(mode!=D3D11_MAP_READ)PostReplay::Mutation(c,r,"map_write");
 const auto hr=map(c,r,sub,mode,flags,out);
 if(!internalWork&&enabled.load()&&r==tracked.load()&&sub==0){
  mappedResource=nullptr;mappedData=nullptr;
  if(SUCCEEDED(hr)&&out&&(mode==D3D11_MAP_WRITE_DISCARD||mode==D3D11_MAP_WRITE)){mappedResource=r;mappedData=out->pData;}
  else {std::lock_guard lock(guard);shadowValid=false;}
 }return hr;
}
inline void WINAPI OnUnmap(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub){
 CommandLists::Record<15>(c,r,sub);
 if(r==mappedResource&&sub==0){Shadow(c,r,mappedData);mappedResource=nullptr;mappedData=nullptr;}unmap(c,r,sub);
}
inline void WINAPI OnUpdate(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub,const D3D11_BOX* box,const void* data,UINT row,UINT depth){
 CommandLists::Record<48>(c,r,sub,box,data,row,depth);
 PostReplay::Mutation(c,r,"update_write");
 update(c,r,sub,box,data,row,depth);
 if(!internalWork&&enabled.load()&&r==tracked.load()){
  if(sub==0&&!box)Shadow(c,r,data);else {std::lock_guard lock(guard);shadowValid=false;}
 }
}
inline UINT uavSlotCount=8;
inline void ResetBindings(){D18Dx11Bindings::Reset(context.Get(),uavSlotCount);}
struct StateScope {
 ScopedInternalContext internal;D18Dx11ManualState::Snapshot snapshot;HRESULT result;
 StateScope():result(snapshot.Capture(context.Get())){if(SUCCEEDED(result))snapshot.Reset();else Fail("manual_state_capture",result);}
 explicit operator bool()const{return SUCCEEDED(result);}
};
inline bool MakeTexture(ID3D11Device* d,unsigned i,DXGI_FORMAT format,bool writable){
 D3D11_TEXTURE2D_DESC desc{};desc.Width=i==3?resolution.outputWidth:width;desc.Height=i==3?resolution.outputHeight:height;desc.MipLevels=desc.ArraySize=1;desc.Format=format;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|(writable?D3D11_BIND_UNORDERED_ACCESS:0);
 HRESULT hr=d->CreateTexture2D(&desc,nullptr,&tex[i]);if(SUCCEEDED(hr))hr=d->CreateShaderResourceView(tex[i].Get(),nullptr,&srv[i]);if(SUCCEEDED(hr)&&writable)hr=d->CreateUnorderedAccessView(tex[i].Get(),nullptr,&uav[i]);if(FAILED(hr)){Fail("texture_allocation",hr);return false;}return true;
}
inline bool EnsureContext(ID3D11Device* d,ID3D11DeviceContext* c){
 if(context)return true;
 if(FAILED(c->QueryInterface(IID_PPV_ARGS(&context)))){Fail("context_interface_unavailable",E_NOINTERFACE);return false;}
 uavSlotCount=d->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64u:8u;return true;
}
inline bool Initialize(ID3D11Device* d,ID3D11DeviceContext* c){
 if(!EnsureContext(d,c))return false;
 HRESULT hr=S_OK;
 D18ExecutionTrace::Point("sr_pipeline_create_begin",c);
 StateScope scope;if(!scope)return false;
 hr=d->CreateComputeShader(numericBytecode,sizeof(numericBytecode),nullptr,&prepare);
 const char vs[]="struct O{float4 p:SV_Position;}; O main(uint i:SV_VertexID){O o;float2 uv=float2((i<<1)&2,i&2);o.p=float4(uv*float2(2,-2)+float2(-1,1),0,1);return o;}";
 const char ps[]="Texture2D<float4> sr:register(t0);Texture2D<float4> original:register(t1);float4 main(float4 p:SV_Position):SV_Target{return float4(sr.Load(int3(p.xy,0)).rgb,original.Load(int3(p.xy,0)).a);}";
 ComPtr<ID3DBlob> code,error;
 if(SUCCEEDED(hr))hr=D3DCompile(vs,sizeof(vs)-1,nullptr,nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
 if(SUCCEEDED(hr))hr=d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&fullscreen);code.Reset();error.Reset();
 if(SUCCEEDED(hr))hr=D3DCompile(ps,sizeof(ps)-1,nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
 if(SUCCEEDED(hr))hr=d->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&compose);
 D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
 if(SUCCEEDED(hr))hr=d->CreateRasterizerState(&rd,&raster);
 D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(shadow);cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 if(SUCCEEDED(hr))hr=d->CreateBuffer(&cb,nullptr,&constants);
 if(FAILED(hr)){Fail("pipeline_creation",hr);return false;}
 if(!MakeTexture(d,0,DXGI_FORMAT_R16G16B16A16_FLOAT,true)||!MakeTexture(d,1,DXGI_FORMAT_R32_FLOAT,true)||!MakeTexture(d,2,DXGI_FORMAT_R16G16_FLOAT,true)||!MakeTexture(d,3,DXGI_FORMAT_R16G16B16A16_FLOAT,true)||!MakeTexture(d,4,DXGI_FORMAT_R10G10B10A2_UNORM,false))return false;
 initialized=true;Status::width=width;Status::height=height;D18ExecutionTrace::Point("sr_pipeline_create_end",c);return true;
}
inline bool CreateNgx(ID3D11Device* d,ID3D11DeviceContext* c){
 if(handle)return true;
 StateScope scope;if(!scope)return false;
 D18ExecutionTrace::Point("sr_ngx_init_begin",c);
 if(!NVNGXProxy::InitDx11(d)||!NVNGXProxy::D3D11_AllocateParameters()||!NVNGXProxy::D3D11_CreateFeature()||!NVNGXProxy::D3D11_EvaluateFeature()){Fail("ngx_initialization",E_FAIL);return false;}
 D18ExecutionTrace::Point("sr_ngx_allocate_begin",c);
 auto nr=NVNGXProxy::D3D11_AllocateParameters()(&params);if(nr!=NVSDK_NGX_Result_Success||!params){Fail("parameters",long(nr));return false;}
 params->Set(NVSDK_NGX_Parameter_Width,width);params->Set(NVSDK_NGX_Parameter_Height,height);params->Set(NVSDK_NGX_Parameter_OutWidth,resolution.outputWidth);params->Set(NVSDK_NGX_Parameter_OutHeight,resolution.outputHeight);
 const auto quality=DlssNr::SelectSrQuality(resolution);
 auto ngxQuality=NVSDK_NGX_PerfQuality_Value_MaxQuality;
 const char* presetKey=NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality;
 switch(quality.mode){
 case DlssNr::SrQualityMode::DLAA:ngxQuality=NVSDK_NGX_PerfQuality_Value_DLAA;presetKey=NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA;break;
 case DlssNr::SrQualityMode::Balanced:ngxQuality=NVSDK_NGX_PerfQuality_Value_Balanced;presetKey=NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced;break;
 case DlssNr::SrQualityMode::Performance:ngxQuality=NVSDK_NGX_PerfQuality_Value_MaxPerf;presetKey=NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance;break;
 case DlssNr::SrQualityMode::UltraPerformance:ngxQuality=NVSDK_NGX_PerfQuality_Value_UltraPerformance;presetKey=NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance;break;
 case DlssNr::SrQualityMode::Quality:break;
 default:Fail("invalid_quality_contract",E_INVALIDARG);return false;
 }
 params->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(ngxQuality));
 const unsigned hint=Status::preset.prepared.load();
 params->Set(presetKey,hint);
 // Bounded alongside existing creation events; payload describes requested NGX parameters.
 static unsigned qualityRecords=0;
 if(log&&qualityRecords++<64){fprintf(log,"{\"event\":\"sr_quality\",\"stage\":\"create_parameters\",\"mode\":\"%s\",\"standard\":%s,\"ratio\":%.8f,\"ngx_quality\":%d,\"preset_key\":\"%s\",\"hint\":%u,\"tick\":%llu}\n",DlssNr::SrQualityName(quality.mode),quality.standard?"true":"false",quality.ratio,int(ngxQuality),presetKey,hint,GetTickCount64());fflush(log);}
 params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_DepthInverted|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure));
 D18ExecutionTrace::Point("sr_ngx_create_begin",c);
 nr=NVNGXProxy::D3D11_CreateFeature()(c,NVSDK_NGX_Feature_SuperSampling,params,&handle);
 if(nr!=NVSDK_NGX_Result_Success||!handle){Fail("feature_creation",long(nr));return false;}
 Status::preset.created=hint;Status::preset.hasCreated=true;PresetEvent("created",hint);
 D18ExecutionTrace::Point("sr_ngx_create_end",c);Event("created");return true;
}
// Caller holds the shared transaction and probe guard; no release before GPU completion.
inline bool ReleaseNgx(){
 nativeNr.ReleaseAfterDrain();
 if(handle){
  StateScope scope;if(!scope)return false;
  auto release=NVNGXProxy::D3D11_ReleaseFeature();
  if(!release){Fail("release_unavailable",E_NOINTERFACE);return false;}
  const auto result=release(handle);
  if(result!=NVSDK_NGX_Result_Success){Fail("release_feature",long(result));return false;}
  handle=nullptr;Status::preset.hasCreated=false;
 }
 if(params){
  auto destroy=NVNGXProxy::D3D11_DestroyParameters();
  if(!destroy){Fail("destroy_parameters_unavailable",E_NOINTERFACE);return false;}
  const auto result=destroy(params);
  if(result!=NVSDK_NGX_Result_Success){Fail("destroy_parameters",long(result));return false;}
  params=nullptr;
 }
 return true;
}
inline void UpdatePreset(){
 const unsigned wanted=Status::preset.requested.load();
 if(wanted==Status::preset.prepared.load()||fault)return;
 static unsigned observed=~0u;
 if(wanted!=observed){observed=wanted;PresetEvent("requested",wanted);}
 if(gpuDrained!=gpuSubmitted)return;
 Numeric::Internal internal;
 if(!ReleaseNgx())return;
 Status::preset.prepared=wanted;reset=true;
 Status::lastTick=0;Status::lastGpuTick=0;
 PresetEvent("prepared_after_drain",wanted);
}
inline void Retire(){
 if(!enabled.load()||!initialized||State::Instance().isShuttingDown)return;
 D18ContextTransaction::Scope transaction(context.Get(),3);if(!transaction)return;
 std::lock_guard lock(guard);Numeric::Internal internal;PollGpu();
 if(fault||gpuDrained!=gpuSubmitted){GpuEvent("retire_deferred");return;}
 if(!ReleaseNgx())return;
 initialized=false;fault=true;GpuEvent("retired_after_gpu_complete");Event("retired");
}

inline bool Texture(ID3D11Resource* r,DXGI_FORMAT format,UINT w,UINT h){
 if(!r)return false;ComPtr<ID3D11Texture2D> t;if(FAILED(r->QueryInterface(IID_PPV_ARGS(&t))))return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);
 return d.Width==w&&d.Height==h&&d.MipLevels==1&&d.ArraySize==1&&d.SampleDesc.Count==1&&(format==DXGI_FORMAT_UNKNOWN||d.Format==format);
}

inline const void* presentOwner=nullptr;
inline bool AcceptPresent(const void* chain,bool primary){
 if(!chain)return false;
 if(!presentOwner){if(!primary)return false;presentOwner=chain;shadowValid=false;reset=true;Event("present_owner_selected");}
 return presentOwner==chain;
}
inline void ReleasePresent(const void* chain){
 std::lock_guard lock(guard);
 if(presentOwner!=chain)return;
 presentOwner=nullptr;shadowValid=false;reset=true;
 if(initialized)Fail("present_owner_released_restart_required",E_ABORT);
}
// Reject unproven partial coverage before allocating/evaluating SR.
struct CoverageInfo {
 UINT topology=0,fill=0,sampleMask=0,writeMask=15,blend=0,alpha=0,depth=0,stencil=0,depthFunc=0,stencilFront=0,stencilBack=0;
 UINT scissor=0,rectCount=0;D3D11_RECT rect{};
};
inline unsigned Coverage(ID3D11DeviceContext* c,UINT w,UINT h,CoverageInfo& info){
 unsigned reason=0;D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);info.topology=UINT(topology);
 if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST&&topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)reason|=4;
 ID3D11Buffer* so[4]{};c->SOGetTargets(4,so);for(auto buffer:so)if(buffer){reason|=4096;buffer->Release();}
 ComPtr<ID3D11GeometryShader> gs;c->GSGetShader(&gs,nullptr,nullptr);if(gs)reason|=8;
 ComPtr<ID3D11HullShader> hs;c->HSGetShader(&hs,nullptr,nullptr);if(hs)reason|=16;
 ComPtr<ID3D11DomainShader> ds;c->DSGetShader(&ds,nullptr,nullptr);if(ds)reason|=32;
 ComPtr<ID3D11RasterizerState> rs;c->RSGetState(&rs);
 if(rs){D3D11_RASTERIZER_DESC desc{};rs->GetDesc(&desc);info.fill=desc.FillMode;info.scissor=desc.ScissorEnable;
  if(desc.FillMode!=D3D11_FILL_SOLID)reason|=64;
  if(desc.ScissorEnable){info.rectCount=1;c->RSGetScissorRects(&info.rectCount,&info.rect);
   if(info.rectCount!=1||info.rect.left>0||info.rect.top>0||info.rect.right<LONG(w)||info.rect.bottom<LONG(h))reason|=128;}}
 ComPtr<ID3D11BlendState> blend;FLOAT factor[4]{};c->OMGetBlendState(&blend,factor,&info.sampleMask);
 if(!(info.sampleMask&1))reason|=256;
 if(blend){D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);auto& rt=desc.RenderTarget[desc.IndependentBlendEnable?1:0];
  info.alpha=desc.AlphaToCoverageEnable;info.blend=rt.BlendEnable;info.writeMask=rt.RenderTargetWriteMask;
  if(info.alpha||info.blend)reason|=512;if((info.writeMask&7)!=7)reason|=1024;}
 ComPtr<ID3D11DepthStencilView> dsv;c->OMGetRenderTargets(0,nullptr,&dsv);
 if(dsv){ComPtr<ID3D11DepthStencilState> state;UINT reference=0;c->OMGetDepthStencilState(&state,&reference);
  if(!state){reason|=2048;info.depth=1;info.depthFunc=D3D11_COMPARISON_LESS;}
  else {D3D11_DEPTH_STENCIL_DESC desc{};state->GetDesc(&desc);info.depth=desc.DepthEnable;info.stencil=desc.StencilEnable;info.depthFunc=desc.DepthFunc;info.stencilFront=desc.FrontFace.StencilFunc;info.stencilBack=desc.BackFace.StencilFunc;if(info.depth||info.stencil)reason|=2048;}}
 return reason;
}
inline void CoverageTrace(ID3D11DeviceContext* c,unsigned reason,UINT count,UINT first,UINT w,UINT h,const CoverageInfo& info){
 static unsigned records=0;static ULONGLONG last=0;const auto now=GetTickCount64();
 if(records>=8||(records&&now-last<1000))return;++records;last=now;
 auto pair=[](UINT a,UINT b){return (UINT64(a)<<32)|b;};
 D18ExecutionTrace::Point("sr_coverage_reason",c,0,reason,pair(w,h));
 D18ExecutionTrace::Point("sr_coverage_draw",c,0,count,first);
 D18ExecutionTrace::Point("sr_coverage_raster",c,0,pair(info.topology,info.fill),pair(info.scissor,info.rectCount));
 D18ExecutionTrace::Point("sr_coverage_scissor",c,0,pair(UINT(info.rect.left),UINT(info.rect.top)),pair(UINT(info.rect.right),UINT(info.rect.bottom)));
 D18ExecutionTrace::Point("sr_coverage_blend",c,0,pair(info.alpha,info.blend),pair(info.sampleMask,info.writeMask));
 D18ExecutionTrace::Point("sr_coverage_depth",c,0,pair(info.depth,info.stencil),pair(info.depthFunc,pair(info.stencilFront,info.stencilBack)&0xffffffff));
 D18ExecutionTrace::Point("sr_coverage_stencil_func",c,0,info.stencilFront,info.stencilBack);
}
#include "D18WildlandsPostReplay.inl"
inline void UpdateDiagnosticStage(){
 if(!Status::stagePanel)return;
 const unsigned wanted=Status::requestedStage;
 if(wanted==Status::activeStage){Status::stagePending=false;return;}
 Status::stagePending=true;
 if(fault||gpuDrained!=gpuSubmitted)return;
 Numeric::Internal internal;
 if(!ReleaseNgx())return;
 Status::activeStage=wanted;Status::stagePending=false;Status::frames=0;Status::lastTick=0;Status::lastGpuTick=0;Status::evaluations=0;reset=true;
 D18ExecutionTrace::Point("sr_diagnostic_mode_applied",context.Get(),0,wanted,gpuDrained);
}
inline void ResizeEvent(const char* stage,const DlssNr::SrResolutionContract& wanted){
 static unsigned records=0;if(!log||records++>=64)return;
 fprintf(log,"{\"event\":\"sr_resize\",\"stage\":\"%s\",\"width\":%u,\"height\":%u,\"output_width\":%u,\"output_height\":%u,\"submitted\":%llu,\"drained\":%llu,\"post_drained\":%s,\"tick\":%llu}\n",stage,wanted.inputWidth,wanted.inputHeight,wanted.outputWidth,wanted.outputHeight,gpuSubmitted,gpuDrained,PostReplay::Drained()?"true":"false",GetTickCount64());fflush(log);
}
// Called only for fully validated current-frame inputs under the context transaction/guard.
inline bool ReconfigureResolution(const DlssNr::SrResolutionContract& wanted){
 if(!initialized || (width==wanted.inputWidth&&height==wanted.inputHeight&&resolution.outputWidth==wanted.outputWidth&&resolution.outputHeight==wanted.outputHeight))return true;
 if(!wanted.Valid()||fault)return false;
 static DlssNr::SrResolutionContract observed;
 if(observed.inputWidth!=wanted.inputWidth||observed.inputHeight!=wanted.inputHeight||observed.outputWidth!=wanted.outputWidth||observed.outputHeight!=wanted.outputHeight){observed=wanted;ResizeEvent("requested",wanted);}
 if(gpuDrained!=gpuSubmitted||!PostReplay::Drained()){reset=true;return false;}
 Numeric::Internal internal;
 if(!ReleaseNgx()||!PostReplay::ResetResolution())return false;
 for(auto& u:uav)u.Reset();for(auto& v:srv)v.Reset();for(auto& t:tex)t.Reset();
 offscreenRT.Reset();prepare.Reset();fullscreen.Reset();compose.Reset();raster.Reset();constants.Reset();
 initialized=false;reset=true;Status::lastTick=0;Status::lastGpuTick=0;Status::handoffGpuTick=0;
 ResizeEvent("released_after_drain",wanted);return true;
}
inline void StageCpu(unsigned mode){
 if(!Status::stagePanel)return;
 auto n=++Status::stageCpu[mode];static ULONGLONG last=0;
 if(n==1||GetTickCount64()-last>1000){last=GetTickCount64();D18ExecutionTrace::Point("sr_diagnostic_cpu_completed",context.Get(),0,mode,n);D18ExecutionTrace::Point("sr_diagnostic_gpu_completed",context.Get(),0,mode,Status::stageGpu[mode]);}
}
inline void AfterDraw(ID3D11DeviceContext* c,const Dx11DrawReplay::Command& command={}){
 if(Status::postReplay)PostReplay::Observe(c,command);
 if(internalWork||!enabled.load()||boundContext!=c||!Numeric::IsTarget(boundShader))return;
 Status::lastTargetTick=GetTickCount64();Status::inputGate=1;
 D18ExecutionTrace::Span trace(D18ExecutionTrace::srBudget,"sr_target_begin","sr_target_end",c);
 trace.Step("sr_target_context",0,executeDepth,Status::enabled.load());
 const bool srOn=Status::enabled.load()&&Config::Instance()->DLSSEnabled.value_or_default();
 if(DlssNr::WildlandsScale::request.PauseSr()||(!srOn&&DlssNr::WildlandsScale::request.phase!=DlssNr::NativeScaleRequest::Phase::AwaitingInput)){Status::inputGate=srOn?3u:2u;reset=true;return;}
 Status::inputGate=4;
 D18ContextTransaction::Scope transaction(c,2);if(!transaction)return;
 std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock()||fault||lastRun==frameNumber||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 if(Status::preset.Pending()){Status::inputGate=5;reset=true;return;}
 Status::inputGate=6;
 if(Status::stagePanel&&(Status::stagePending||Status::requestedStage!=Status::activeStage))return;
 const unsigned mode=Status::stagePanel?Status::activeStage.load():(Status::evaluateOnly?Stages::Evaluate:Stages::Full);
 ComPtr<ID3D11Device> d;c->GetDevice(&d);if(d.Get()!=observedDevice)return;
 trace.Step("sr_device_contract",0,d->GetCreationFlags(),d->GetFeatureLevel());
 ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);if(!Numeric::IsTarget(ps.Get()))return;
 lastRun=frameNumber;++attempts;
 ComPtr<ID3D11Predicate> predicate;c->GetPredication(&predicate,nullptr);if(predicate){++waits;reset=true;return;}
 ComPtr<ID3D11Buffer> cb;c->PSGetConstantBuffers(5,1,&cb);if(!cb){++waits;reset=true;return;}
 ComPtr<ID3D11DeviceContext1> ranges;c->QueryInterface(IID_PPV_ARGS(&ranges));if(ranges){UINT first=0,count=0;ComPtr<ID3D11Buffer> ranged;ranges->PSGetConstantBuffers1(5,1,&ranged,&first,&count);if(first!=0||count<20){++waits;reset=true;return;}}
 D3D11_BUFFER_DESC cbd{};cb->GetDesc(&cbd);
 if(cbd.ByteWidth!=sizeof(shadow)){++waits;reset=true;return;}
 if(trackedCB.Get()!=cb.Get()){tracked=cb.Get();trackedCB=cb;shadowValid=false;}
 if(!shadowValid||shadowFrame!=frameNumber){Status::inputGate=7;++waits;reset=true;if(GetTickCount64()-lastLog>1000){lastLog=GetTickCount64();Event("waiting_fresh_temporal_constants");}return;}
 Status::inputGate=8;
 for(float v:shadow)if(!std::isfinite(v)){++waits;reset=true;return;}
 if(shadow[46]<64||shadow[47]<64||shadow[46]>8192||shadow[47]>8192){++waits;reset=true;return;}
 const UINT w=UINT(shadow[46]),h=UINT(shadow[47]);
 const DlssNr::SrResolutionContract requested{w,h,Status::offscreenUpscale?displayWidth:w,Status::offscreenUpscale?displayHeight:h};
 if(!requested.Valid()){++waits;reset=true;if(GetTickCount64()-lastLog>1000){lastLog=GetTickCount64();Event("waiting_valid_output_resolution");}return;}
 if(w<64||h<64||w>8192||h>8192||UINT64(w)*h>16777216||shadow[46]!=float(w)||shadow[47]!=float(h)||fabs(shadow[44]*w-1)>1e-4||fabs(shadow[45]*h-1)>1e-4||fabs(shadow[48])>1||fabs(shadow[49])>1||fabs(shadow[50]*w-shadow[48])>1e-3||fabs(shadow[51]*h-shadow[49])>1e-3){++waits;reset=true;return;}
 ComPtr<ID3D11ShaderResourceView> inputs[3];ComPtr<ID3D11Resource> resources[3];const UINT slots[]={0,2,5};
 for(unsigned i=0;i<3;++i){c->PSGetShaderResources(slots[i],1,&inputs[i]);if(!inputs[i]){++waits;reset=true;return;}D3D11_SHADER_RESOURCE_VIEW_DESC vd{};inputs[i]->GetDesc(&vd);inputs[i]->GetResource(&resources[i]);
  bool fmt=i==0?vd.Format==DXGI_FORMAT_R10G10B10A2_UNORM:i==1?(vd.Format==DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS||vd.Format==DXGI_FORMAT_R32_FLOAT):vd.Format==DXGI_FORMAT_R16G16_FLOAT;
  if(!fmt||vd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||vd.Texture2D.MostDetailedMip!=0||!Texture(resources[i].Get(),DXGI_FORMAT_UNKNOWN,w,h)){++waits;reset=true;return;}}
 ComPtr<ID3D11RenderTargetView> outputRT[2];ID3D11RenderTargetView* rawRT[2]{};c->OMGetRenderTargets(2,rawRT,nullptr);for(unsigned i=0;i<2;++i)outputRT[i].Attach(rawRT[i]);
 if(!outputRT[0]||!outputRT[1]){++waits;reset=true;return;}ComPtr<ID3D11Resource> outputResource[2];
 for(unsigned i=0;i<2;++i){outputRT[i]->GetResource(&outputResource[i]);if(!Texture(outputResource[i].Get(),DXGI_FORMAT_R10G10B10A2_UNORM,w,h)){++waits;reset=true;return;}for(auto& r:resources)if(r==outputResource[i])return;}
 if(outputResource[0]==outputResource[1])return;
 D3D11_VIEWPORT vp{};UINT n=1;c->RSGetViewports(&n,&vp);if(n!=1||vp.TopLeftX!=0||vp.TopLeftY!=0||vp.Width!=float(w)||vp.Height!=float(h)){++waits;reset=true;return;}
 CoverageInfo coverageInfo;const unsigned coverage=(!presentOwner?1u:0u)|(!command.Supported()?2u:0u)|Coverage(c,w,h,coverageInfo);
 Status::coverageReason=coverage;
 if(coverage){Status::inputGate=9;CoverageTrace(c,coverage,command.count,command.first,w,h,coverageInfo);++waits;reset=true;Status::coverageBlocked=true;
  if(GetTickCount64()-lastLog>1000){lastLog=GetTickCount64();Event("output_coverage_rejected");}return;}
 Status::inputGate=10;
 DlssNr::WildlandsScale::ObserveInput(w,h,requested.outputWidth,requested.outputHeight);
 if(!srOn){reset=true;return;}
 if(mode==Stages::Observe){Status::coverageBlocked=false;StageCpu(mode);return;}
 if(mode!=Stages::Observe)transaction.StartSampling();
 if(mode==Stages::StateOnly){if(!EnsureContext(d.Get(),c))return;{transaction.Step("context_manual_scope_begin");{StateScope scope;if(!scope)return;}transaction.Step("context_manual_scope_restored");}Status::coverageBlocked=false;StageCpu(mode);return;}
 Dx11DrawReplay::Geometry geometry;
 if(!geometry.Capture(c)){Status::coverageReason=8192;Status::coverageBlocked=true;++waits;reset=true;return;}
 Status::coverageBlocked=false;
 if(!ReconfigureResolution(requested))return;
 GpuFrame* pending=ReserveGpu(d.Get());if(!pending)return;
 if(!initialized){resolution=requested;Status::outputWidth=resolution.outputWidth;Status::outputHeight=resolution.outputHeight;width=w;height=h;Numeric::Internal internal;trace.Step("sr_initialize_begin");if(!Initialize(d.Get(),c))return;trace.Step("sr_initialize_end");}
 if(Stages::NeedsNgx(mode)&&!CreateNgx(d.Get(),c))return;
 if(Status::lastTick&&GetTickCount64()-Status::lastTick.load()>250)reset=true;
 StateScope scope;if(!scope)return;
 trace.Step("sr_state_enter");
 pending->diagnosticStage=mode;pending->pending=true;pending->id=++gpuSubmitted;pending->tick=GetTickCount64();
 if(mode==Stages::OriginalWriteback||mode==Stages::CopyOnly||mode==Stages::OffscreenReplay){
  if(mode==Stages::OffscreenReplay&&!offscreenRT){
   // Private target is created only for this diagnostic stage. No change to
   // the normal SR output texture descriptor or production allocation path.
   D3D11_TEXTURE2D_DESC td{};tex[4]->GetDesc(&td);td.BindFlags=D3D11_BIND_RENDER_TARGET;
   ComPtr<ID3D11Texture2D> target;auto hr=d->CreateTexture2D(&td,nullptr,&target);
   if(SUCCEEDED(hr))hr=d->CreateRenderTargetView(target.Get(),nullptr,&offscreenRT);
   if(FAILED(hr)){pending->pending=false;--gpuSubmitted;Fail("diagnostic_offscreen_allocation",hr);return;}
  }
  c->CopyResource(tex[4].Get(),outputResource[1].Get());
  if(mode==Stages::CopyOnly){c->End(pending->stage[0].Get());pending->issued=1;StageCpu(mode);return;}
  geometry.Apply(context.Get());auto rt=mode==Stages::OffscreenReplay?offscreenRT.Get():outputRT[1].Get();c->OMSetRenderTargets(1,&rt,nullptr);c->PSSetShader(compose.Get(),nullptr,0);
  ID3D11ShaderResourceView* originals[]={srv[4].Get(),srv[4].Get()};c->PSSetShaderResources(0,2,originals);command.Run(c);
  c->End(pending->stage[0].Get());pending->issued=1;StageCpu(mode);return;
 }
 if(mode==Stages::Full)c->CopyResource(tex[4].Get(),outputResource[1].Get());
c->UpdateSubresource(constants.Get(),0,nullptr,shadow,0,0);
 ID3D11ShaderResourceView* rawViews[]={inputs[0].Get(),inputs[1].Get(),inputs[2].Get()};c->CSSetShaderResources(0,3,rawViews);auto b=constants.Get();c->CSSetConstantBuffers(0,1,&b);
 ID3D11UnorderedAccessView* rawUav[]={uav[0].Get(),uav[1].Get(),uav[2].Get()};c->CSSetUnorderedAccessViews(0,3,rawUav,nullptr);c->CSSetShader(prepare.Get(),nullptr,0);c->Dispatch((w+7)/8,(h+7)/8,1);c->End(pending->stage[0].Get());pending->issued=1;ResetBindings();
 if(mode==Stages::Prepare||mode==Stages::Create){StageCpu(mode);return;}
 params->Set(NVSDK_NGX_Parameter_Color,static_cast<ID3D11Resource*>(tex[0].Get()));params->Set(NVSDK_NGX_Parameter_Depth,static_cast<ID3D11Resource*>(tex[1].Get()));params->Set(NVSDK_NGX_Parameter_MotionVectors,static_cast<ID3D11Resource*>(tex[2].Get()));params->Set(NVSDK_NGX_Parameter_Output,static_cast<ID3D11Resource*>(tex[3].Get()));
 params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,shadow[48]);params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,shadow[49]);
 params->Set(NVSDK_NGX_Parameter_MV_Scale_X,1.0f);params->Set(NVSDK_NGX_Parameter_MV_Scale_Y,1.0f);params->Set(NVSDK_NGX_Parameter_Reset,int(reset));params->Set(NVSDK_NGX_Parameter_Sharpness,0.0f);
 params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,w);params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,h);
 params->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,1.0f);params->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,1.0f);
 auto result=NVNGXProxy::D3D11_EvaluateFeature()(c,handle,params,nullptr);
 // NR runs under the SR transaction and state guard, before private post processing.
 if(result==NVSDK_NGX_Result_Success && SUCCEEDED(d->GetDeviceRemovedReason()) && (mode==Stages::Evaluate||mode==Stages::Full)){
  ResetBindings();
  const int nrResult=nativeNr.Run(c,params,NVSDK_NGX_DLSS_Feature_Flags_DepthInverted|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes);
  if(nrResult)D18ExecutionTrace::Point("native_nr_return",c,nrResult==1?0:nrResult,gpuSubmitted,nrResult==1);
  ResetBindings();
 }
 if(result==NVSDK_NGX_Result_Success && SUCCEEDED(d->GetDeviceRemovedReason()) && Status::nativeHandoff){
  ResetBindings();
  DlssNr::NativeFgDx11::Submit(d.Get(),c,params,{w,h,resolution.outputWidth,resolution.outputHeight,
      resolution.outputWidth,resolution.outputHeight,true,false,false,true,false});
  ResetBindings();
 }
 // Fence covers SR, optional NR and FG's DX11 shared-input copies.
 c->End(pending->stage[1].Get());pending->issued=2;
 trace.Step("sr_evaluate_return",result==NVSDK_NGX_Result_Success?0:long(result),gpuSubmitted,gpuComplete);
 if(result!=NVSDK_NGX_Result_Success){Fail("evaluate",long(result));return;}
 if(FAILED(d->GetDeviceRemovedReason())){Fail("device_removed",d->GetDeviceRemovedReason());return;}
 if(mode==Stages::Evaluate){
  if(Status::postReplay){PostReplay::enabled=true;PostReplay::Begin(d.Get(),outputResource[1].Get());}
  ++Status::evaluations;reset=false;StageCpu(mode);
  if(Status::evaluations==1||GetTickCount64()-lastLog>1000){lastLog=GetTickCount64();Event("diagnostic_evaluate_only_no_writeback");}
  // Leave issued=2. Poll only the submitted prepare/evaluate queries and
  // drain the slot without incrementing compose/completed-frame counters.
  return;
 }
 ResetBindings();geometry.Apply(context.Get());auto rt=outputRT[1].Get();c->OMSetRenderTargets(1,&rt,nullptr);c->PSSetShader(compose.Get(),nullptr,0);
 ID3D11ShaderResourceView* finalViews[]={srv[3].Get(),srv[4].Get()};c->PSSetShaderResources(0,2,finalViews);command.Run(c);c->End(pending->stage[2].Get());pending->issued=3;pending->producesSr=true;StageCpu(mode);
 if(!Status::frames.load()){D18ExecutionTrace::Point("sr_replay_command",c,0,UINT(command.kind),(UINT64(command.count)<<32)|command.first);D18ExecutionTrace::Point("sr_replay_instances",c,0,command.instances,command.firstInstance);D18ExecutionTrace::Point("sr_replay_base_vertex",c,0,UINT(command.baseVertex),0);}
 reset=false;++Status::frames;Status::lastTick=GetTickCount64();
 trace.Step("sr_composed",0,gpuSubmitted,gpuComplete);
 if(Status::frames==1||GetTickCount64()-lastLog>1000){lastLog=GetTickCount64();Event("composed");}
}
// Summary works even if it is enabled after startup. No extra marker/file/capture.
inline void RecordProgress(){
 const auto mode=static_cast<DlssNr::Diagnostics::Mode>((std::min)(2u,Config::Instance()->DlssNrDiagnostics.value_or_default()));
 if(mode==DlssNr::Diagnostics::Mode::Off)return;
 static unsigned records=0;static ULONGLONG last=0;const auto now=GetTickCount64();
 if(records>=128 || (records && now-last<2000))return;
 ++records;last=now;
 char reason[96]{};const auto target=Status::lastTargetTick.load();
 snprintf(reason,sizeof(reason),"gate=%u;age_ms=%llu;eval=%llu;gpu=%llu;handoff=%llu",Status::inputGate.load(),target?now-target:~0ULL,Status::evaluations.load(),Status::gpuCompleted.load(),Status::handoffGpuCompleted.load());
 DlssNr::Diagnostics::Event e{};e.type="native_sr_progress";e.reason=reason;e.frame=frameNumber;
 e.width=Status::width;e.height=Status::height;e.networkWidth=Status::outputWidth;e.networkHeight=Status::outputHeight;
 e.flags=(Status::enabled?1u:0u)|(Config::Instance()->DLSSEnabled.value_or_default()?2u:0u)|(Status::failed?4u:0u);
 e.result=Status::coverageReason;e.featureGeneration=DlssNr::WildlandsScale::request.generation;
 DlssNr::Diagnostics::Record(mode,e);
}
inline void Frame(){
 if(!enabled.load())return;
 if(Status::stagePanel&&observedDevice){
  const auto hr=observedDevice->GetDeviceRemovedReason();
  if(FAILED(hr))Fail("diagnostic_device_removed",hr);
 }
 PostReplay::Frame();
 PollGpu();
 if(Status::nativeHandoff){
  DlssNr::WildlandsScale::TryInstall((ProbeRoot()/L"D18NativeScale.jsonl").c_str());
  DlssNr::WildlandsScale::Tick(gpuSubmitted==gpuDrained,PostReplay::Drained());
 }
 UpdateDiagnosticStage();
 UpdatePreset();
 RecordProgress();
 static unsigned long long gpuLogTick=0;
 if(GetTickCount64()-gpuLogTick>1000&&gpuSubmitted){gpuLogTick=GetTickCount64();GpuEvent("progress");}
 // Legacy F9 belongs exclusively to the explicitly armed diagnostic stage panel.
 // Release users control native SR through its existing menu controls.
 if constexpr(DlssNr::BuildProfile::Diagnostic){
  if(Status::stagePanel){
   static bool held=false;
   DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
   const bool down=pid==GetCurrentProcessId()&&(GetAsyncKeyState(VK_F9)&0x8000);
   if(down&&!held){Status::SetEnabled(!Status::enabled.load());reset=true;Event(Status::enabled?"toggle_on":"toggle_off");}
   held=down;
  }
 }
}
}

