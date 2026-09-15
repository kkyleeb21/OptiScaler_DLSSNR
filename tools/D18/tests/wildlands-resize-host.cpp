#include "core-source/OptiScaler/hooks/D18Dx11ManualState.h"
#include "core-source/OptiScaler/dlssnr/SrResolutionContract.h"
#include "core-source/OptiScaler/dlssnr/Dx11ReplaySurface.h"
#include "core-source/OptiScaler/dlssnr/Dx11CommandListWrites.h"
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cmath>
#include <cstring>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"user32.lib")
using Microsoft::WRL::ComPtr;
struct DebugDump {ComPtr<ID3D11Device> d;~DebugDump(){
 FILE* f=nullptr;fopen_s(&f,"debug.txt","w");if(!f)return;fprintf(f,"removed_reason=%08lx\n",static_cast<unsigned long>(d->GetDeviceRemovedReason()));ComPtr<ID3D11InfoQueue> q;
 if(SUCCEEDED(d.As(&q))){auto count=q->GetNumStoredMessages();for(UINT64 i=0;i<count;++i){SIZE_T size=0;q->GetMessage(i,nullptr,&size);if(size>65536)continue;std::vector<char> buffer(size);auto msg=reinterpret_cast<D3D11_MESSAGE*>(buffer.data());if(SUCCEEDED(q->GetMessage(i,msg,&size))&&(i<16||msg->Severity<=D3D11_MESSAGE_SEVERITY_ERROR))fprintf(f,"%u %s\n",unsigned(msg->Severity),msg->pDescription);}}fclose(f);
}};
int wmain(int argc,wchar_t** argv){
 if(!DlssNr::SrResolutionContract{2880,1620,3840,2160}.Valid() ||
    !DlssNr::SrResolutionContract{2560,1440,3840,2160}.Valid() ||
    !DlssNr::SrResolutionContract{1920,1080,3840,2160}.Valid() ||
    !DlssNr::SrResolutionContract{3840,2160,3840,2160}.Valid() ||
    DlssNr::SrResolutionContract{2880,1080,3840,2160}.Valid() ||
    DlssNr::SrResolutionContract{3840,2160,1920,1080}.Valid() ||
    DlssNr::SrResolutionContract{2880,1620,0,0}.Valid() ||
    DlssNr::SrResolutionContract{8192,8192,8192,8192}.Valid())return 99;
 if(argc!=7&&argc!=8&&argc!=9&&argc!=10)return 2;
 const bool handoffMode=wcscmp(argv[3],L"offscreen-handoff")==0;
 const bool computeMode=handoffMode||wcscmp(argv[3],L"offscreen-compute")==0;
 const bool post=computeMode||wcscmp(argv[3],L"offscreen-post")==0;
 const bool offscreen=wcsncmp(argv[3],L"offscreen-",10)==0;auto module=LoadLibraryW(argv[1]);if(!module)return 2;
 const bool draw6=wcscmp(argv[3],L"draw6")==0;
 const bool presetCycle=handoffMode||wcscmp(argv[3],L"preset-cycle")==0;
 const bool cycle=wcscmp(argv[3],L"stage-cycle")==0;
 const auto stageSetter=(argc>=8&&wcstoull(argv[7],nullptr,16)!=0)?reinterpret_cast<void(*)(unsigned)>(reinterpret_cast<unsigned char*>(module)+wcstoull(argv[7],nullptr,16)):nullptr;
 const bool transition=wcscmp(argv[3],L"ui-transition")==0;
 const bool replay=wcscmp(argv[3],L"ui-replay")==0;
 const bool ui=offscreen||presetCycle||cycle||wcscmp(argv[3],L"ui")==0||wcscmp(argv[3],L"ui-release")==0||wcscmp(argv[3],L"ui-disabled")==0||replay||transition;
 const bool noDebug=wcscmp(argv[3],L"release")==0||wcscmp(argv[3],L"ui-release")==0;const bool stress=wcscmp(argv[3],L"stress")==0||noDebug||ui;
 // RVA comes from this exact DLL's linker map. This calls the existing menu
 // shortcut handler inside the isolated host; no system input is injected.
 const auto keyUp=reinterpret_cast<void(*)(UINT)>(reinterpret_cast<unsigned char*>(module)+wcstoull(argv[4],nullptr,16));
 const auto uiPresent=reinterpret_cast<void(*)(IDXGISwapChain*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*,IUnknown*,HWND,bool)>(reinterpret_cast<unsigned char*>(module)+wcstoull(argv[5],nullptr,16));
 const auto setSr=reinterpret_cast<void(*)(bool)>(reinterpret_cast<unsigned char*>(module)+wcstoull(argv[6],nullptr,16));
 const bool dynamic=wcscmp(argv[3],L"dynamic")==0,stale=wcscmp(argv[3],L"stale")==0,predicated=wcscmp(argv[3],L"predicated")==0;
 const bool range=wcscmp(argv[3],L"range")==0,missing=wcscmp(argv[3],L"missing")==0;
 const UINT baseTw=offscreen?((post||wcscmp(argv[3],L"offscreen-75")==0)?2880u:wcscmp(argv[3],L"offscreen-67")==0?2560u:1920u):(wcscmp(argv[3],L"4k")==0||stress)?3840u:128u,baseTh=offscreen?((post||wcscmp(argv[3],L"offscreen-75")==0)?1620u:wcscmp(argv[3],L"offscreen-67")==0?1440u:1080u):(wcscmp(argv[3],L"4k")==0||stress)?2160u:128u;
 const bool resizeCycle=handoffMode&&GetEnvironmentVariableW(L"D18_HOST_RESIZE",nullptr,0)>0;
 UINT tw=wcscmp(argv[3],L"offscreen-100")==0?3840u:wcscmp(argv[3],L"offscreen-58")==0?2227u:wcscmp(argv[3],L"offscreen-33")==0?1280u:baseTw;
 UINT th=wcscmp(argv[3],L"offscreen-100")==0?2160u:wcscmp(argv[3],L"offscreen-58")==0?1253u:wcscmp(argv[3],L"offscreen-33")==0?720u:baseTh;
 HWND w=CreateWindowExW(0,L"STATIC",L"D18 SR UI fixture",WS_OVERLAPPEDWINDOW,0,0,1920,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!w)return 3;
 DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=offscreen?3840u:tw;sd.BufferDesc.Height=offscreen?2160u:th;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=1;sd.OutputWindow=w;sd.Windowed=TRUE;
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;ComPtr<IDXGISwapChain> swap;D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0,actual{};
 if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,(noDebug?0u:UINT(D3D11_CREATE_DEVICE_DEBUG)),&level,1,D3D11_SDK_VERSION,&sd,&swap,&d,&actual,&c))||actual!=level)return 4;
 DebugDump debug{d};
 ComPtr<ID3D11DeviceContext> deferred;if(replay&&FAILED(d->CreateDeferredContext(0,&deferred)))return 40;
 ComPtr<ID3D11DeviceContext1> c1;c.As(&c1);if(range&&!c1)return 5;
 std::ifstream f(argv[2],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});ComPtr<ID3D11PixelShader> ps;
 if(bytes.empty()||FAILED(d->CreatePixelShader(bytes.data(),bytes.size(),nullptr,&ps)))return 6;
 const char* vsSource=draw6?"struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;}; O main(float3 p:POSITION,float2 uv:TEXCOORD0){O o;o.p=float4(p,1);o.uv=uv;return o;}":"struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;}; O main(uint id:SV_VertexID){O o;o.uv=float2((id<<1)&2,id&2);o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}";
 ComPtr<ID3DBlob> vsCode,vsErrors;ComPtr<ID3D11VertexShader> vertex;
 if(FAILED(D3DCompile(vsSource,strlen(vsSource),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vsCode,&vsErrors))||FAILED(d->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vertex)))return 28;
 ComPtr<ID3D11Buffer> geometryBuffer;ComPtr<ID3D11InputLayout> geometryLayout;
 if(draw6){
  float vertices[][5]={{-1,1,0,0,0},{1,1,0,1,0},{-1,-1,0,0,1},{-1,-1,0,0,1},{1,1,0,1,0},{1,-1,0,1,1}};
  D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(vertices);bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;D3D11_SUBRESOURCE_DATA init{vertices,0,0};
  if(FAILED(d->CreateBuffer(&bd,&init,&geometryBuffer)))return 70;
  D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
  if(FAILED(d->CreateInputLayout(elements,2,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),&geometryLayout)))return 71;
  auto v=geometryBuffer.Get();UINT stride=20,offset=0;c->IASetVertexBuffers(0,1,&v,&stride,&offset);c->IASetInputLayout(geometryLayout.Get());
 }
 c->VSSetShader(vertex.Get(),nullptr,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 D3D11_VIEWPORT vp{};vp.Width=float(tw);vp.Height=float(th);vp.MaxDepth=1;c->RSSetViewports(1,&vp);
 ComPtr<ID3D11Texture2D> outputTextures[2];ComPtr<ID3D11RenderTargetView> outputViews[2];
 for(UINT i=0;i<2;++i){D3D11_TEXTURE2D_DESC td{};td.Width=tw;td.Height=th;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R10G10B10A2_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|(computeMode?D3D11_BIND_UNORDERED_ACCESS:0);
  if(FAILED(d->CreateTexture2D(&td,nullptr,&outputTextures[i]))||FAILED(d->CreateRenderTargetView(outputTextures[i].Get(),nullptr,&outputViews[i])))return 29;
 }ID3D11RenderTargetView* rt[]={outputViews[0].Get(),outputViews[1].Get()};c->OMSetRenderTargets(2,rt,nullptr);
 ComPtr<ID3D11Texture2D> textures[3];ComPtr<ID3D11ShaderResourceView> views[3];
 const DXGI_FORMAT formats[]={DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
 const UINT pixels[]={256u|(512u<<10)|(768u<<20)|(1u<<30),0x3f400000,0xa4002800};
 for(UINT i=0;i<3;++i){std::vector<UINT> data(tw*th,pixels[i]);D3D11_TEXTURE2D_DESC td{};td.Width=tw;td.Height=th;td.MipLevels=td.ArraySize=1;td.Format=formats[i];td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA init{data.data(),tw*4,0};
  if(FAILED(d->CreateTexture2D(&td,&init,&textures[i]))||FAILED(d->CreateShaderResourceView(textures[i].Get(),nullptr,&views[i])))return 7;
 }
 ComPtr<ID3D11Texture2D> displayTexture;ComPtr<ID3D11RenderTargetView> displayTarget;ComPtr<ID3D11ComputeShader> noOpCs;
 ComPtr<ID3D11ComputeShader> mutationCs;ComPtr<ID3D11UnorderedAccessView> mutationUav;
 ComPtr<ID3D11PixelShader> copyShader;ComPtr<ID3D11Texture2D> copyTexture;ComPtr<ID3D11RenderTargetView> copyTarget;ComPtr<ID3D11ShaderResourceView> lateView,copyView;
 ComPtr<ID3D11PixelShader> lateShader;ComPtr<ID3D11Texture2D> lateTexture,lateDepth;ComPtr<ID3D11RenderTargetView> lateTarget;ComPtr<ID3D11ShaderResourceView> rootView;ComPtr<ID3D11DepthStencilView> lateDsv;ComPtr<ID3D11DepthStencilState> lateState;
 ComPtr<ID3D11ComputeShader> realCompute;ComPtr<ID3D11Buffer> realCb;ComPtr<ID3D11Texture2D> motionTexture;ComPtr<ID3D11ShaderResourceView> motionView;ComPtr<ID3D11RenderTargetView> motionTarget;ComPtr<ID3D11UnorderedAccessView> rootUav;
 ComPtr<ID3D11UnorderedAccessView> motionUav;
 ComPtr<ID3D11PixelShader> branchBlur,branchComposite;ComPtr<ID3D11Buffer> branchCb1,branchCb6;
 if(handoffMode){
  for(UINT i=0;i<2;++i){std::ifstream branchFile(i?L"c8780f432eb9201d.cso":L"3f4d34c5e5c3d1cd.cso",std::ios::binary);std::vector<char> code((std::istreambuf_iterator<char>(branchFile)),{});if(code.empty()||FAILED(d->CreatePixelShader(code.data(),code.size(),nullptr,i?&branchComposite:&branchBlur)))return 170;}
  float constants[161*4]{};constants[160*4]=1;D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(constants);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA data{constants,0,0};if(FAILED(d->CreateBuffer(&bd,&data,&branchCb1)))return 171;
  float weights[11*4]{};for(UINT i=0;i<5;++i)weights[i*4]=.1f;bd.ByteWidth=sizeof(weights);data.pSysMem=weights;if(FAILED(d->CreateBuffer(&bd,&data,&branchCb6)))return 172;
 }
 if(computeMode){
  std::ifstream cf(L"real-compute.cso",std::ios::binary);std::vector<char> bc((std::istreambuf_iterator<char>(cf)),{});if(bc.empty()||FAILED(d->CreateComputeShader(bc.data(),bc.size(),nullptr,&realCompute)))return 120;
  D3D11_TEXTURE2D_DESC td{};outputTextures[1]->GetDesc(&td);
  if(FAILED(d->CreateTexture2D(&td,nullptr,&motionTexture))||FAILED(d->CreateShaderResourceView(motionTexture.Get(),nullptr,&motionView))||FAILED(d->CreateRenderTargetView(motionTexture.Get(),nullptr,&motionTarget))||FAILED(d->CreateUnorderedAccessView(outputTextures[1].Get(),nullptr,&rootUav)))return 121;
  if(handoffMode&&FAILED(d->CreateUnorderedAccessView(motionTexture.Get(),nullptr,&motionUav)))return 173;
  float values[]={.5f,1.0f/float(tw),1.0f/float(th),1,1,0,0,0,1,1,1,1};D3D11_BUFFER_DESC bd{};bd.ByteWidth=48;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA data{values,0,0};if(FAILED(d->CreateBuffer(&bd,&data,&realCb)))return 122;
 }
 if(post){
  std::ifstream lf(L"late-target.cso",std::ios::binary);std::vector<char> bc((std::istreambuf_iterator<char>(lf)),{});
  if(bc.empty()||FAILED(d->CreatePixelShader(bc.data(),bc.size(),nullptr,&lateShader)))return 101;
  if(FAILED(d->CreateShaderResourceView(outputTextures[1].Get(),nullptr,&rootView)))return 102;
  D3D11_TEXTURE2D_DESC td{};td.Width=3840;td.Height=2160;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  td.BindFlags|=D3D11_BIND_UNORDERED_ACCESS;
  if(FAILED(d->CreateTexture2D(&td,nullptr,&lateTexture))||FAILED(d->CreateRenderTargetView(lateTexture.Get(),nullptr,&lateTarget)))return 103;
  const char mutationCode[]="RWTexture2D<float4> target:register(u1);cbuffer B:register(b5){float4 constantValue;}[numthreads(1,1,1)]void main(uint3 id:SV_DispatchThreadID){target[id.xy]=constantValue;}";
  ComPtr<ID3DBlob> mutationBlob,mutationError;
  if(FAILED(D3DCompile(mutationCode,sizeof(mutationCode)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&mutationBlob,&mutationError))||FAILED(d->CreateComputeShader(mutationBlob->GetBufferPointer(),mutationBlob->GetBufferSize(),nullptr,&mutationCs))||FAILED(d->CreateUnorderedAccessView(lateTexture.Get(),nullptr,&mutationUav)))return 116;
  std::ifstream cf(L"copy-target.cso",std::ios::binary);std::vector<char> cbc((std::istreambuf_iterator<char>(cf)),{});
  if(cbc.empty()||FAILED(d->CreatePixelShader(cbc.data(),cbc.size(),nullptr,&copyShader)))return 113;
  if(FAILED(d->CreateTexture2D(&td,nullptr,&copyTexture))||FAILED(d->CreateRenderTargetView(copyTexture.Get(),nullptr,&copyTarget))||FAILED(d->CreateShaderResourceView(copyTexture.Get(),nullptr,&copyView))||FAILED(d->CreateShaderResourceView(lateTexture.Get(),nullptr,&lateView)))return 114;
  td.BindFlags=D3D11_BIND_RENDER_TARGET;
  if(FAILED(d->CreateTexture2D(&td,nullptr,&displayTexture))||FAILED(d->CreateRenderTargetView(displayTexture.Get(),nullptr,&displayTarget)))return 117;
  const char noOpCode[]="[numthreads(1,1,1)]void main(){}";ComPtr<ID3DBlob> noOpBlob;
  if(FAILED(D3DCompile(noOpCode,sizeof(noOpCode)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&noOpBlob,&mutationError))||FAILED(d->CreateComputeShader(noOpBlob->GetBufferPointer(),noOpBlob->GetBufferSize(),nullptr,&noOpCs)))return 118;
  td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  if(FAILED(d->CreateTexture2D(&td,nullptr,&lateDepth))||FAILED(d->CreateDepthStencilView(lateDepth.Get(),nullptr,&lateDsv)))return 104;
  D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_ALWAYS;
  if(FAILED(d->CreateDepthStencilState(&dd,&lateState)))return 105;
 }
 const UINT slots[]={0,2,5};for(UINT i=0;i<3;++i){auto p=views[i].Get();if(missing&&i==2)p=nullptr;c->PSSetShaderResources(slots[i],1,&p);}
 for(UINT slot:{1u,3u,4u}){auto p=views[0].Get();c->PSSetShaderResources(slot,1,&p);}
 std::vector<float> cbData[3];cbData[0].resize(range?1024:752);cbData[1].resize(252);cbData[2].resize(range?192:80);
 const UINT firsts[]={range?16u:0u,0,range?16u:0u};const UINT counts[]={192,64,32};
 cbData[0][firsts[0]*4+160*4]=1.0f;
 auto& temporal=cbData[2];const auto base=firsts[2]*4;
 temporal[base+11*4]=1.0f/tw;temporal[base+11*4+1]=1.0f/th;temporal[base+11*4+2]=float(tw);temporal[base+11*4+3]=float(th);
 temporal[base+12*4]=0.25f;temporal[base+12*4+1]=-0.25f;temporal[base+12*4+2]=0.25f/tw;temporal[base+12*4+3]=-0.25f/th;
 for(UINT i=0;i<4;++i)temporal[base+(13+i)*4+i]=1;
 ComPtr<ID3D11Buffer> buffers[3];const UINT cbSlots[]={1,2,5};
 for(UINT i=0;i<3;++i){D3D11_BUFFER_DESC bd{};bd.ByteWidth=static_cast<UINT>(cbData[i].size()*4);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(dynamic&&i==2){bd.Usage=D3D11_USAGE_DYNAMIC;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;}D3D11_SUBRESOURCE_DATA init{cbData[i].data(),0,0};if(FAILED(d->CreateBuffer(&bd,&init,&buffers[i])))return 8;
  auto p=buffers[i].Get();if(range)c1->PSSetConstantBuffers1(cbSlots[i],1,&p,&firsts[i],&counts[i]);else c->PSSetConstantBuffers(cbSlots[i],1,&p);
 }
 ComPtr<ID3D11SamplerState> sampler;D3D11_SAMPLER_DESC samplerDesc{};samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;
 if(FAILED(d->CreateSamplerState(&samplerDesc,&sampler)))return 9;auto sp=sampler.Get();c->PSSetSamplers(0,1,&sp);c->PSSetSamplers(10,1,&sp);c->PSSetSamplers(12,1,&sp);
 const char csCode[]="RWStructuredBuffer<uint> a:register(u0); [numthreads(1,1,1)] void main(uint3 p:SV_DispatchThreadID){a[0]=1;}";
 ComPtr<ID3DBlob> code,errors;ComPtr<ID3D11ComputeShader> oldCS;
 if(FAILED(D3DCompile(csCode,sizeof(csCode)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&code,&errors))||FAILED(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&oldCS)))return 10;
 ComPtr<ID3D11Buffer> oldOutput;D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=4;
 if(FAILED(d->CreateBuffer(&bd,nullptr,&oldOutput)))return 11;
 ComPtr<ID3D11UnorderedAccessView> oldUAV;D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=8;ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_COUNTER;
 if(FAILED(d->CreateUnorderedAccessView(oldOutput.Get(),&ud,&oldUAV)))return 12;
 ID3D11UnorderedAccessView* oldRaw=oldUAV.Get();UINT counter=7;c->CSSetUnorderedAccessViews(0,1,&oldRaw,&counter);
 ID3D11ShaderResourceView* oldViews[]={views[1].Get(),views[1].Get(),views[1].Get()};c->CSSetShaderResources(0,3,oldViews);c->CSSetSamplers(0,1,&sp);c->CSSetShader(oldCS.Get(),nullptr,0);
 ID3D11Buffer* oldCBs[]={buffers[0].Get(),buffers[0].Get()};UINT oldFirst[]={16,32},oldCounts[]={16,16};
 if(range)c1->CSSetConstantBuffers1(1,2,oldCBs,oldFirst,oldCounts);else c->CSSetConstantBuffers(1,2,oldCBs);
 ComPtr<ID3D11Predicate> predicate;D3D11_QUERY_DESC pred{D3D11_QUERY_OCCLUSION_PREDICATE,0};if(FAILED(d->CreatePredicate(&pred,&predicate)))return 13;c->Begin(predicate.Get());c->End(predicate.Get());c->SetPredication(nullptr,FALSE);if(range){D18Dx11ManualState::Snapshot snapshot;if(FAILED(snapshot.Capture(c.Get())))return 97;snapshot.Reset();}
 if(FAILED(swap->Present(0,0)))return 14;
 ComPtr<ID3D11CommandList> disjointList,affectedList,prefixList,overflowList,retainedList;
 ComPtr<ID3D11Texture2D> isolatedTexture;ComPtr<ID3D11RenderTargetView> isolatedTarget;
 if(post){
  namespace M=DlssNr::Dx11CommandListWrites;
  ComPtr<ID3D11DeviceContext> dc;if(FAILED(d->CreateDeferredContext(0,&dc)))return 130;
  D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=4;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
  if(FAILED(d->CreateTexture2D(&td,nullptr,&isolatedTexture))||FAILED(d->CreateRenderTargetView(isolatedTexture.Get(),nullptr,&isolatedTarget)))return 131;
  D3D11_BUFFER_DESC commandBd{};commandBd.ByteWidth=32;ComPtr<ID3D11Buffer> source,copy;UINT initial[8]={1,2,3,4};D3D11_SUBRESOURCE_DATA data{initial,0,0};
  if(FAILED(d->CreateBuffer(&commandBd,&data,&source))||FAILED(d->CreateBuffer(&commandBd,nullptr,&copy)))return 132;
  dc->CopyResource(copy.Get(),source.Get());auto target=isolatedTarget.Get();dc->OMSetRenderTargets(1,&target,nullptr);D3D11_VIEWPORT viewport{0,0,4,4,0,1};dc->RSSetViewports(1,&viewport);dc->VSSetShader(vertex.Get(),nullptr,0);dc->PSSetShader(copyShader.Get(),nullptr,0);auto v=rootView.Get();dc->PSSetShaderResources(0,1,&v);dc->PSSetSamplers(0,1,&sp);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);dc->Draw(3,0);
  if(FAILED(dc->FinishCommandList(FALSE,&disjointList)))return 133;
  auto safe=M::Read<M::List>(disjointList.Get(),M::listTag);if(!safe||safe->writes.reasons||safe->writes.size!=2||safe->writes.draws!=1){printf("safe_metadata reasons=%u size=%u\n",safe?safe->writes.reasons:999,safe?safe->writes.size:999);return 134;}
  const FLOAT colour[]={.25f,.5f,.75f,1};dc->ClearRenderTargetView(lateTarget.Get(),colour);if(FAILED(dc->FinishCommandList(FALSE,&affectedList)))return 135;
  auto affected=M::Read<M::List>(affectedList.Get(),M::listTag);if(!affected||affected->writes.reasons||affected->writes.size!=1||affected->writes.resources[0]!=lateTexture)return 136;
  // Finish(TRUE) must retain target shadow across command lists, never invent an empty state.
  target=lateTarget.Get();dc->OMSetRenderTargets(1,&target,nullptr);D3D11_VIEWPORT full{0,0,3840,2160,0,1};dc->RSSetViewports(1,&full);dc->VSSetShader(vertex.Get(),nullptr,0);dc->PSSetShader(copyShader.Get(),nullptr,0);v=rootView.Get();dc->PSSetShaderResources(0,1,&v);dc->PSSetSamplers(0,1,&sp);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ComPtr<ID3D11CommandList> stateOnly;
  if(FAILED(dc->FinishCommandList(TRUE,&stateOnly)))return 137;dc->Draw(3,0);if(FAILED(dc->FinishCommandList(FALSE,&retainedList)))return 138;
  auto retained=M::Read<M::List>(retainedList.Get(),M::listTag);if(!retained||retained->writes.reasons||retained->writes.size!=1||retained->writes.resources[0]!=lateTexture)return 139;
  // Simulate observation attaching halfway through a recording session.
  dc->SetPrivateData(M::contextTag,0,nullptr);dc->ClearRenderTargetView(isolatedTarget.Get(),colour);if(FAILED(dc->FinishCommandList(FALSE,&prefixList)))return 140;
  auto prefix=M::Read<M::List>(prefixList.Get(),M::listTag);if(!prefix||!(prefix->writes.reasons&M::PrefixMissing))return 141;
  std::vector<ComPtr<ID3D11Buffer>> targets(65);for(auto& b:targets){if(FAILED(d->CreateBuffer(&commandBd,nullptr,&b)))return 142;dc->CopyResource(b.Get(),source.Get());}
  if(FAILED(dc->FinishCommandList(FALSE,&overflowList)))return 143;auto overflow=M::Read<M::List>(overflowList.Get(),M::listTag);if(!overflow||!(overflow->writes.reasons&M::Overflow)||overflow->writes.size!=64)return 144;
  printf("command_metadata safe_draw_copy=1 affected=1 retained_bindings=1 prefix_rejected=1 overflow_rejected=1\n");
 }
 unsigned menuToggles=0,presentStateChecks=0;
 for(unsigned frame=0;frame<(stress?1800u:90u);++frame){
  if(resizeCycle&&(frame==350||frame==750||frame==1150||frame==1550)){
   const UINT widths[]={2560,2227,1920,2880},heights[]={1440,1253,1080,1620};const UINT index=(frame-350)/400;
   tw=widths[index];th=heights[index];
   // Recreate the synthetic engine's low-resolution colour/depth/MV together.
   // Do not wait for D18's GPU work here: the adapter must drain its own ownership.
   c->OMSetRenderTargets(0,nullptr,nullptr);ID3D11ShaderResourceView* emptyViews[6]{};c->PSSetShaderResources(0,6,emptyViews);c->CSSetShaderResources(0,3,emptyViews);
   rootView.Reset();rootUav.Reset();motionView.Reset();motionTarget.Reset();motionUav.Reset();motionTexture.Reset();
   for(UINT i=0;i<2;++i){outputViews[i].Reset();outputTextures[i].Reset();D3D11_TEXTURE2D_DESC td{};td.Width=tw;td.Height=th;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R10G10B10A2_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    if(FAILED(d->CreateTexture2D(&td,nullptr,&outputTextures[i]))||FAILED(d->CreateRenderTargetView(outputTextures[i].Get(),nullptr,&outputViews[i])))return 180;rt[i]=outputViews[i].Get();}
   for(UINT i=0;i<3;++i){views[i].Reset();textures[i].Reset();std::vector<UINT> data(tw*th,pixels[i]);D3D11_TEXTURE2D_DESC td{};td.Width=tw;td.Height=th;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=formats[i];td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA init{data.data(),tw*4,0};if(FAILED(d->CreateTexture2D(&td,&init,&textures[i]))||FAILED(d->CreateShaderResourceView(textures[i].Get(),nullptr,&views[i])))return 181;}
   D3D11_TEXTURE2D_DESC td{};outputTextures[1]->GetDesc(&td);
   if(FAILED(d->CreateTexture2D(&td,nullptr,&motionTexture))||FAILED(d->CreateShaderResourceView(motionTexture.Get(),nullptr,&motionView))||FAILED(d->CreateRenderTargetView(motionTexture.Get(),nullptr,&motionTarget))||FAILED(d->CreateUnorderedAccessView(motionTexture.Get(),nullptr,&motionUav))||FAILED(d->CreateUnorderedAccessView(outputTextures[1].Get(),nullptr,&rootUav))||FAILED(d->CreateShaderResourceView(outputTextures[1].Get(),nullptr,&rootView)))return 182;
   float values[]={.5f,1.0f/tw,1.0f/th,1,1,0,0,0,1,1,1,1};c->UpdateSubresource(realCb.Get(),0,nullptr,values,0,0);
   vp.Width=float(tw);vp.Height=float(th);temporal[base+44]=1.0f/tw;temporal[base+45]=1.0f/th;temporal[base+46]=float(tw);temporal[base+47]=float(th);temporal[base+51]=temporal[base+49]/th;
   for(auto& view:oldViews)view=views[1].Get();
   printf("engine_resize frame=%u input=%ux%u output=3840x2160\n",frame,tw,th);
  }
  if(handoffMode&&frame==600)setSr(false);if(handoffMode&&frame==610)setSr(true);
  if(presetCycle && stageSetter && frame%300==100){
   const unsigned hints[]={12,13,11,0,12,0};
   stageSetter(hints[frame/300]);
   // Coalesce a burst while preserving the last selected value.
   if(frame==1300){stageSetter(11);stageSetter(12);}
  }
  if(!presetCycle && stageSetter){
   if(cycle&&frame%200==0){unsigned modes[]={0,1,7,2,1,7,2,0,1};stageSetter(modes[frame/200]);}
   else if(!cycle&&frame==0)stageSetter(static_cast<unsigned>(wcstoul(argv[3]+5,nullptr,10)));
  }
  if(transition&&frame==120)setSr(true);
  // Establish the exact pre-Draw state after Present/overlay activity.
  c->VSSetShader(vertex.Get(),nullptr,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->RSSetViewports(1,&vp);c->OMSetRenderTargets(2,rt,nullptr);
  for(UINT i=0;i<3;++i){auto p=views[i].Get();if(missing&&i==2)p=nullptr;c->PSSetShaderResources(slots[i],1,&p);auto b=buffers[i].Get();if(range)c1->PSSetConstantBuffers1(cbSlots[i],1,&b,&firsts[i],&counts[i]);else c->PSSetConstantBuffers(cbSlots[i],1,&b);}
  for(UINT slot:{1u,3u,4u}){auto p=views[0].Get();c->PSSetShaderResources(slot,1,&p);}c->PSSetSamplers(10,1,&sp);c->PSSetSamplers(12,1,&sp);
  c->CSSetShader(oldCS.Get(),nullptr,0);c->CSSetShaderResources(0,3,oldViews);c->CSSetSamplers(0,1,&sp);UINT preserve=UINT(-1);c->CSSetUnorderedAccessViews(0,1,&oldRaw,&preserve);
  if(range)c1->CSSetConstantBuffers1(1,2,oldCBs,oldFirst,oldCounts);else c->CSSetConstantBuffers(1,2,oldCBs);c->SetPredication(nullptr,FALSE);if(range){D18Dx11ManualState::Snapshot snapshot;if(FAILED(snapshot.Capture(c.Get())))return 97;snapshot.Reset();}
  if(stress){temporal[base+13*4+3]=sinf(float(frame)*.05f)*.3f;temporal[base+14*4+3]=cosf(float(frame)*.07f)*.1f;}
  temporal[base+12*4]=(frame%2?-.25f:.25f);temporal[base+12*4+2]=temporal[base+12*4]/tw;if(!stale||frame==0){if(dynamic){D3D11_MAPPED_SUBRESOURCE write{};if(FAILED(c->Map(buffers[2].Get(),0,D3D11_MAP_WRITE_DISCARD,0,&write)))return 31;memcpy(write.pData,temporal.data(),temporal.size()*4);c->Unmap(buffers[2].Get(),0);}else c->UpdateSubresource(buffers[2].Get(),0,nullptr,temporal.data(),0,0);}
  c->PSSetShader(ps.Get(),nullptr,0);if(predicated)c->SetPredication(predicate.Get(),TRUE);
  if(replay&&frame%2){
   deferred->VSSetShader(vertex.Get(),nullptr,0);deferred->PSSetShader(ps.Get(),nullptr,0);deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);deferred->RSSetViewports(1,&vp);deferred->OMSetRenderTargets(2,rt,nullptr);
   for(UINT i=0;i<3;++i){auto p=views[i].Get();deferred->PSSetShaderResources(slots[i],1,&p);auto b=buffers[i].Get();deferred->PSSetConstantBuffers(cbSlots[i],1,&b);}
   for(UINT slot:{1u,3u,4u}){auto p=views[0].Get();deferred->PSSetShaderResources(slot,1,&p);}deferred->PSSetSamplers(10,1,&sp);deferred->PSSetSamplers(12,1,&sp);
   deferred->Draw(3,0);ComPtr<ID3D11CommandList> list;if(FAILED(deferred->FinishCommandList(FALSE,&list)))return 41;c->ExecuteCommandList(list.Get(),TRUE);
  }else c->Draw(draw6?6:3,0);
  if(predicated)c->SetPredication(nullptr,FALSE);if(range){D18Dx11ManualState::Snapshot snapshot;if(FAILED(snapshot.Capture(c.Get())))return 97;snapshot.Reset();}
  ComPtr<ID3D11ComputeShader> csCheck;c->CSGetShader(&csCheck,nullptr,nullptr);if(csCheck.Get()!=oldCS.Get())return 15;
  ComPtr<ID3D11UnorderedAccessView> uvCheck;c->CSGetUnorderedAccessViews(0,1,&uvCheck);if(uvCheck.Get()!=oldUAV.Get())return 16;
  ComPtr<ID3D11Predicate> predCheck;BOOL value=FALSE;c->GetPredication(&predCheck,&value);if(predCheck.Get()!=nullptr)return 17;
  ComPtr<ID3D11SamplerState> sCheck;c->CSGetSamplers(0,1,&sCheck);if(sCheck.Get()!=sampler.Get())return 18;
  for(UINT i=0;i<3;++i){ComPtr<ID3D11ShaderResourceView> v;c->CSGetShaderResources(i,1,&v);if(v.Get()!=oldViews[i])return 19;}
  for(UINT i=0;i<2;++i){ComPtr<ID3D11Buffer> b;UINT first=0,count=0;if(range)c1->CSGetConstantBuffers1(i+1,1,&b,&first,&count);else c->CSGetConstantBuffers(i+1,1,&b);if(b.Get()!=oldCBs[i]||(range&&(first!=oldFirst[i]||count!=oldCounts[i])))return 20;}
  if(computeMode){
   // Real captured CS, private lineage seeded by the real SR path, original clear/dispatch.
   D18Dx11ManualState::Snapshot saved;if(FAILED(saved.Capture(c.Get())))return 123;
   const bool computeFirst=handoffMode&&(frame%2==0);
   auto target=motionTarget.Get();auto input=rootView.Get();
   if(!computeFirst){c->OMSetRenderTargets(1,&target,nullptr);c->PSSetShader(copyShader.Get(),nullptr,0);c->PSSetShaderResources(0,1,&input);c->Draw(3,0);}
   c->OMSetRenderTargets(0,nullptr,nullptr);input=nullptr;c->PSSetShaderResources(0,1,&input);
   auto u=computeFirst?motionUav.Get():rootUav.Get();const UINT zeros[4]{};c->ClearUnorderedAccessViewUint(u,zeros);
   ID3D11ShaderResourceView* inputs[]={computeFirst?rootView.Get():motionView.Get(),views[1].Get()};c->CSSetShaderResources(0,2,inputs);c->CSSetUnorderedAccessViews(0,1,&u,nullptr);auto b=realCb.Get();c->CSSetConstantBuffers(5,1,&b);c->CSSetShader(realCompute.Get(),nullptr,0);c->CSSetSamplers(0,1,&sp);c->Dispatch((tw+15)/16,(th+15)/16,1);
   ComPtr<ID3D11ComputeShader> cs;c->CSGetShader(&cs,nullptr,nullptr);ComPtr<ID3D11UnorderedAccessView> uv;c->CSGetUnorderedAccessViews(0,1,&uv);ComPtr<ID3D11Buffer> cb;c->CSGetConstantBuffers(5,1,&cb);
   if(cs!=realCompute||uv.Get()!=u||cb!=realCb)return 124;
   if(computeFirst){ID3D11UnorderedAccessView* emptyUav=nullptr;c->CSSetUnorderedAccessViews(0,1,&emptyUav,nullptr);ID3D11ShaderResourceView* empty[2]{};c->CSSetShaderResources(0,2,empty);target=rt[1];c->OMSetRenderTargets(1,&target,nullptr);c->PSSetShader(copyShader.Get(),nullptr,0);input=motionView.Get();c->PSSetShaderResources(0,1,&input);c->PSSetSamplers(0,1,&sp);c->Draw(3,0);}
   if(handoffMode&&(frame==420||frame==421)){
    ID3D11UnorderedAccessView* emptyUav=nullptr;c->CSSetUnorderedAccessViews(0,1,&emptyUav,nullptr);ID3D11ShaderResourceView* empty[2]{};c->CSSetShaderResources(0,2,empty);
    auto cb1=branchCb1.Get(),cb6=branchCb6.Get();c->PSSetConstantBuffers(1,1,&cb1);c->PSSetConstantBuffers(5,1,&b);c->PSSetConstantBuffers(6,1,&cb6);c->PSSetSamplers(6,1,&sp);
    target=rt[1];c->OMSetRenderTargets(1,&target,nullptr);inputs[0]=views[1].Get();inputs[1]=motionView.Get();c->PSSetShaderResources(0,2,inputs);c->PSSetShader(frame==420?branchBlur.Get():branchComposite.Get(),nullptr,0);c->Draw(3,0);
    printf("alternate_profile_frame=%u executed=1\n",frame);
   }
  }
  if(handoffMode&&frame>=1100&&frame<=1102){
   ComPtr<ID3D11DeviceContext> dc;ComPtr<ID3D11CommandList> prefix,conflict;
   if(FAILED(d->CreateDeferredContext(0,&dc))||FAILED(dc->FinishCommandList(FALSE,&prefix)))return 174;
   const FLOAT colour[]={.9f,.1f,.2f,0};dc->ClearRenderTargetView(rt[1],colour);
   if(FAILED(dc->FinishCommandList(FALSE,&conflict)))return 175;c->ExecuteCommandList(conflict.Get(),TRUE);
  }
  if(handoffMode&&(frame==605||frame==1000||frame==1799)){
   auto surfaces=reinterpret_cast<DlssNr::Dx11ReplaySurface*>(reinterpret_cast<unsigned char*>(module)+wcstoull(argv[9],nullptr,16));
   bool injected=false;for(UINT i=0;i<8;++i)if(surfaces[i].original.Get()==outputTextures[1].Get()&&surfaces[i].target){const FLOAT distinct[]={.8f,.2f,.1f,0};c->ClearRenderTargetView(surfaces[i].target.Get(),distinct);injected=true;break;}if(!injected)return 151;
  }
  if(post){
   auto target=lateTarget.Get();c->OMSetRenderTargets(1,&target,lateDsv.Get());c->OMSetDepthStencilState(lateState.Get(),0);
   D3D11_VIEWPORT highVp{0,0,3840,2160,0,1};c->RSSetViewports(1,&highVp);c->PSSetShader(lateShader.Get(),nullptr,0);
   ComPtr<ID3D11SamplerState> rejectSampler;if(handoffMode&&frame==1000){auto desc=samplerDesc;desc.Filter=D3D11_FILTER_ANISOTROPIC;desc.MaxAnisotropy=16;if(FAILED(d->CreateSamplerState(&desc,&rejectSampler)))return 159;auto raw=rejectSampler.Get();c->PSSetSamplers(12,1,&raw);}
   ID3D11ShaderResourceView* inputs[]={rootView.Get(),views[1].Get()};c->PSSetShaderResources(0,2,inputs);c->Draw(3,0);
   if(handoffMode&&(frame==605||frame==1000||frame==1100)){
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;if(FAILED(d->CreateTexture2D(&td,nullptr,&read)))return 160;
    D3D11_BOX pixel{100,100,0,101,101,1};c->CopySubresourceRegion(read.Get(),0,0,0,0,lateTexture.Get(),0,&pixel);D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&m)))return 161;auto colourBytes=static_cast<const unsigned char*>(m.pData);if(std::abs(int(colourBytes[0])-(frame==1100?230:64))>1||std::abs(int(colourBytes[1])-(frame==1100?26:128))>1||std::abs(int(colourBytes[2])-(frame==1100?51:191))>1)return 162;c->Unmap(read.Get(),0);if(frame==1100)printf("native_layer_original_change_reaches_display=1\n");else printf("handoff_fallback_frame=%u original_colour=1\n",frame);
   }
   if(rejectSampler)c->PSSetSamplers(12,1,&sp);
   if(handoffMode){ComPtr<ID3D11ShaderResourceView> restored[2];c->PSGetShaderResources(0,1,&restored[0]);c->PSGetShaderResources(1,1,&restored[1]);if(restored[0]!=rootView||restored[1]!=views[1])return 152;
    if(frame==1799){D3D11_VIEWPORT corner{0,0,4,4,0,1};c->RSSetViewports(1,&corner);c->OMSetRenderTargets(1,&target,nullptr);c->OMSetDepthStencilState(nullptr,0);c->PSSetShader(copyShader.Get(),nullptr,0);c->Draw(3,0);c->RSSetViewports(1,&highVp);c->OMSetRenderTargets(1,&target,lateDsv.Get());c->OMSetDepthStencilState(lateState.Get(),0);}
   }
   ComPtr<ID3D11DepthStencilView> restoredDepth;c->OMGetRenderTargets(0,nullptr,&restoredDepth);if(restoredDepth.Get()!=lateDsv.Get())return 106;
   // Exercise metadata during the selected initial frames and continue drawing after rejection.
   if(frame==2){const FLOAT values[]={.25f,.5f,.75f,1};c->ClearUnorderedAccessViewFloat(mutationUav.Get(),values);}
   if(frame==3){const UINT values[]={1,2,3,4};c->ClearUnorderedAccessViewUint(mutationUav.Get(),values);}
   // Same captured copy shader, new native target and then reuse of an existing target.
   auto copyRt=copyTarget.Get();c->OMSetRenderTargets(1,&copyRt,nullptr);c->OMSetDepthStencilState(nullptr,0);
   c->PSSetShader(copyShader.Get(),nullptr,0);auto cv=lateView.Get();c->PSSetShaderResources(0,1,&cv);c->Draw(3,0);
   c->OMSetRenderTargets(1,&target,nullptr);cv=copyView.Get();c->PSSetShaderResources(0,1,&cv);c->Draw(3,0);
   // Final target intentionally has RT bind only, never an original SRV.
   auto display=displayTarget.Get();c->OMSetRenderTargets(1,&display,nullptr);cv=lateView.Get();c->PSSetShaderResources(0,1,&cv);c->Draw(3,0);
   if(frame==1||frame==1001){
    ComPtr<ID3D11ComputeShader> saved;c->CSGetShader(&saved,nullptr,nullptr);ComPtr<ID3D11UnorderedAccessView> savedUav;c->CSGetUnorderedAccessViews(0,1,&savedUav);
    ID3D11ShaderResourceView* empty=nullptr;c->PSSetShaderResources(0,1,&empty);auto u=mutationUav.Get();c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->CSSetShader(noOpCs.Get(),nullptr,0);c->Dispatch(1,1,1);
    u=savedUav.Get();const UINT keep=~0u;c->CSSetUnorderedAccessViews(0,1,&u,&keep);c->CSSetShader(saved.Get(),nullptr,0);cv=copyView.Get();c->PSSetShaderResources(0,1,&cv);
   }
   // Interleave real original API writes to a tracked target. Each must invalidate only this frame.
   const auto step=frame%600;
   c->ExecuteCommandList(disjointList.Get(),TRUE);
   if(step==110)c->ExecuteCommandList(affectedList.Get(),TRUE);
   if(step==120)c->ExecuteCommandList(retainedList.Get(),TRUE);
   if(step==130)c->ExecuteCommandList(prefixList.Get(),TRUE);
   if(step==140)c->ExecuteCommandList(overflowList.Get(),TRUE);
   if(step==100){const FLOAT clear[]={.25f,.5f,.75f,1};c->ClearRenderTargetView(target,clear);}
   if(step==200)c->CopyResource(lateTexture.Get(),copyTexture.Get());
   if(step==300)c->CopySubresourceRegion(lateTexture.Get(),0,0,0,0,copyTexture.Get(),0,nullptr);
   if(step==400){const UINT pixel=0xffbf8040;const D3D11_BOX box{0,0,0,1,1,1};c->UpdateSubresource(lateTexture.Get(),0,&box,&pixel,4,4);}
   if(step==450||frame==2){
    ComPtr<ID3D11Buffer> savedCb;c->CSGetConstantBuffers(5,1,&savedCb);auto cbForCompute=buffers[0].Get();c->CSSetConstantBuffers(5,1,&cbForCompute);
    ComPtr<ID3D11ComputeShader> savedCs;c->CSGetShader(&savedCs,nullptr,nullptr);ComPtr<ID3D11UnorderedAccessView> savedUav;c->CSGetUnorderedAccessViews(1,1,&savedUav);
    c->OMSetRenderTargets(0,nullptr,nullptr);auto u=mutationUav.Get();c->CSSetUnorderedAccessViews(1,1,&u,nullptr);c->CSSetShader(mutationCs.Get(),nullptr,0);c->Dispatch(1,1,1);
    u=savedUav.Get();const UINT keep=~0u;c->CSSetUnorderedAccessViews(1,1,&u,&keep);c->CSSetShader(savedCs.Get(),nullptr,0);auto cbRestore=savedCb.Get();c->CSSetConstantBuffers(5,1,&cbRestore);c->OMSetRenderTargets(1,&target,nullptr);
   }
   if(step==550){auto untracked=views[0].Get();c->PSSetShaderResources(0,1,&untracked);c->Draw(3,0);}
   if(step==500){ComPtr<ID3D11DeviceContext> dc;ComPtr<ID3D11CommandList> commands;if(FAILED(d->CreateDeferredContext(0,&dc))||FAILED(dc->FinishCommandList(FALSE,&commands)))return 115;c->ExecuteCommandList(commands.Get(),TRUE);}
   c->OMSetRenderTargets(2,rt,nullptr);c->OMSetDepthStencilState(nullptr,0);c->RSSetViewports(1,&vp);c->PSSetShader(ps.Get(),nullptr,0);
  }
  // Hold an offset VS CB binding across the actual overlay draw as well.
  auto vsCB=buffers[0].Get();UINT vsFirst=16,vsCount=16;c1->VSSetConstantBuffers1(0,1,&vsCB,&vsFirst,&vsCount);
  if(ui&&frame%300==60){keyUp(VK_INSERT);++menuToggles;}
  // Hidden windows are occluded, so the ordinary Present path skips UI.
  // Exercise that exact renderer directly without showing a desktop window.
  if(ui)uiPresent(swap.Get(),0,0,nullptr,d.Get(),w,false);
  if(FAILED(swap->Present(0,0)))return 21;
  ComPtr<ID3D11ComputeShader> afterUI;c->CSGetShader(&afterUI,nullptr,nullptr);if(afterUI.Get()!=oldCS.Get())return 36;
  ID3D11RenderTargetView* afterRT[2]{};c->OMGetRenderTargets(2,afterRT,nullptr);bool rts=afterRT[0]==rt[0]&&afterRT[1]==rt[1];for(auto p:afterRT)if(p)p->Release();if(!rts)return 37;
  ComPtr<ID3D11Buffer> afterCB;UINT af=0,ac=0;c1->VSGetConstantBuffers1(0,1,&afterCB,&af,&ac);if(afterCB.Get()!=vsCB||af!=vsFirst||ac!=vsCount)return 38;
  ComPtr<ID3D11PixelShader> afterPS;c->PSGetShader(&afterPS,nullptr,nullptr);if(afterPS.Get()!=ps.Get())return 39;
  ++presentStateChecks;if(stress){c->Flush();Sleep(1);}else Sleep(16);
 }
 c->SetPredication(nullptr,FALSE);if(range){D18Dx11ManualState::Snapshot snapshot;if(FAILED(snapshot.Capture(c.Get())))return 97;snapshot.Reset();}
 // Fixture-only GPU drain verifies the preserved hidden UAV counter. The probe never flushes/waits.
 ComPtr<ID3D11Buffer> counterGPU,counterCPU;D3D11_BUFFER_DESC cb{};cb.ByteWidth=4;if(FAILED(d->CreateBuffer(&cb,nullptr,&counterGPU)))return 22;cb.Usage=D3D11_USAGE_STAGING;cb.CPUAccessFlags=D3D11_CPU_ACCESS_READ;if(FAILED(d->CreateBuffer(&cb,nullptr,&counterCPU)))return 23;
 c->CopyStructureCount(counterGPU.Get(),0,oldUAV.Get());c->CopyResource(counterCPU.Get(),counterGPU.Get());ComPtr<ID3D11Query> q;D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};if(FAILED(d->CreateQuery(&qd,&q)))return 24;c->End(q.Get());c->Flush();BOOL ready=FALSE;
 for(unsigned i=0;i<1000;++i){if(c->GetData(q.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&ready)break;Sleep(1);}if(!ready)return 25;
 D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->Map(counterCPU.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped)))return 26;UINT actualCounter=*static_cast<UINT*>(mapped.pData);c->Unmap(counterCPU.Get(),0);if(actualCounter!=7)return 27;
 if(offscreen){
  if(argc!=9&&argc!=10)return 91;
  // Exact-DLL linker-map identity; isolated fixture only, never inspect the game process.
  auto privateTextures=reinterpret_cast<ComPtr<ID3D11Texture2D>*>(reinterpret_cast<unsigned char*>(module)+wcstoull(argv[8],nullptr,16));
  auto output=privateTextures[3].Get();if(!output)return 92;
  D3D11_TEXTURE2D_DESC td{};output->GetDesc(&td);
  if(td.Width!=3840||td.Height!=2160||td.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return 93;
  td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> readback;if(FAILED(d->CreateTexture2D(&td,nullptr,&readback)))return 94;
  c->CopyResource(readback.Get(),output);D3D11_MAPPED_SUBRESOURCE m{};
  if(FAILED(c->Map(readback.Get(),0,D3D11_MAP_READ,0,&m)))return 95;
  unsigned finite=0,nonzero=0;
  for(UINT y=0;y<td.Height;++y){auto row=reinterpret_cast<const unsigned short*>(static_cast<const char*>(m.pData)+y*m.RowPitch);
   for(UINT x=0;x<td.Width;++x)for(UINT k=0;k<3;++k){auto v=row[x*4+k];if((v&0x7c00)!=0x7c00)++finite;if(v&0x7fff)++nonzero;}}
  c->Unmap(readback.Get(),0);
  printf("private_output=%ux%u finite_rgb=%u nonzero_rgb=%u\n",td.Width,td.Height,finite,nonzero);
  if(finite!=td.Width*td.Height*3||!nonzero)return 96;
 }
 if(post&&!handoffMode){
  if(argc!=10)return 107;
  auto surfaces=reinterpret_cast<DlssNr::Dx11ReplaySurface*>(reinterpret_cast<unsigned char*>(module)+wcstoull(argv[9],nullptr,16));
  if(surfaces[computeMode?4:3].originalView)return 119;
  auto finalTexture=surfaces[computeMode?4:3].texture.Get();if(!finalTexture)return 108;
  D3D11_TEXTURE2D_DESC td{};finalTexture->GetDesc(&td);if(td.Width!=3840||td.Height!=2160||td.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)return 109;
  td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;if(FAILED(d->CreateTexture2D(&td,nullptr,&staging)))return 110;
  c->CopyResource(staging.Get(),finalTexture);D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)))return 111;
  double means[3]{};for(UINT y=0;y<td.Height;++y){auto row=static_cast<const unsigned char*>(m.pData)+y*m.RowPitch;for(UINT x=0;x<td.Width;++x)for(UINT k=0;k<3;++k)means[k]+=double(row[x*4+k])/(255.0*td.Width*td.Height);}
  c->Unmap(staging.Get(),0);
  printf("private_final_rgb=%.6f,%.6f,%.6f\n",means[0],means[1],means[2]);
  if(std::abs(means[0]-.25)>.015||std::abs(means[1]-.5)>.015||std::abs(means[2]-.75)>.015)return 112;
 }
 if(handoffMode){
  D3D11_TEXTURE2D_DESC td{};displayTexture->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;
  if(FAILED(d->CreateTexture2D(&td,nullptr,&read)))return 153;c->CopyResource(read.Get(),displayTexture.Get());D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&m)))return 154;
  auto sample=[&](UINT x,UINT y,UINT k){return UINT(static_cast<const unsigned char*>(m.pData)[y*m.RowPitch+x*4+k]);};
  const UINT expected[]={204,51,26},overlay[]={64,128,191};for(UINT k=0;k<3;++k){if(std::abs(int(sample(100,100,k))-int(expected[k]))>1||std::abs(int(sample(1,1,k))-int(overlay[k]))>1)return 155;}c->Unmap(read.Get(),0);
  lateDepth->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;read.Reset();if(FAILED(d->CreateTexture2D(&td,nullptr,&read)))return 156;c->CopyResource(read.Get(),lateDepth.Get());if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&m)))return 157;
  for(UINT y:{1u,100u,2159u})for(UINT x:{1u,100u,3839u})if(std::abs(reinterpret_cast<const float*>(static_cast<const char*>(m.pData)+y*m.RowPitch)[x]-.75f)>1e-6f)return 158;c->Unmap(read.Get(),0);
  printf("handoff_private_colour=1 native_overlay=1 native_depth=1 input_bindings_restored=1\n");
 }
 if(!predicated&&!missing){
  std::vector<UINT> finalPixels[2];
  c->ClearState();
  for(unsigned i=0;i<2;++i){D3D11_TEXTURE2D_DESC td{};outputTextures[i]->GetDesc(&td);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;if(FAILED(d->CreateTexture2D(&td,nullptr,&staging)))return 32;c->CopyResource(staging.Get(),outputTextures[i].Get());D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)))return 33;finalPixels[i].resize(tw*th);for(UINT y=0;y<th;++y)memcpy(finalPixels[i].data()+y*tw,static_cast<char*>(m.pData)+y*m.RowPitch,tw*4);c->Unmap(staging.Get(),0);}
  double mean[2]{};for(unsigned i=0;i<2;++i)for(UINT pixel:finalPixels[i])mean[i]+=double(pixel&1023)/(1023.0*tw*th);
  for(size_t i=0;i<finalPixels[0].size();++i)if(!computeMode&&(finalPixels[0][i]>>30)!=(finalPixels[1][i]>>30))return 34;
  if(offscreen && !computeMode && finalPixels[0]!=finalPixels[1])return 98;
  if(computeMode)for(UINT pixel:finalPixels[1])if(pixel>>30)return 125;
  printf("original_r=%.8f composed_r=%.8f alpha_preserved=%d\n",mean[0],mean[1],int(!computeMode));if(mean[0]<.20||mean[0]>.30||mean[1]<.10||mean[1]>.40)return 35;
 }
 for(unsigned i=0;i<4;++i){swap->Present(0,0);Sleep(16);}swap.Reset();std::printf("state_restored=1 uav_counter=%u range=%d menu_toggles=%u present_state_checks=%u\n",actualCounter,int(range),menuToggles,presentStateChecks);return 0;
}
