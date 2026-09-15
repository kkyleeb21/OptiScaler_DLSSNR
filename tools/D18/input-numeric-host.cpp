#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstring>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"user32.lib")
using Microsoft::WRL::ComPtr;
struct DebugDump {ComPtr<ID3D11Device> d;~DebugDump(){
 FILE* f=nullptr;fopen_s(&f,"debug.txt","w");if(!f)return;fprintf(f,"removed_reason=%08lx\n",static_cast<unsigned long>(d->GetDeviceRemovedReason()));ComPtr<ID3D11InfoQueue> q;
 if(SUCCEEDED(d.As(&q))){auto count=q->GetNumStoredMessages();for(UINT64 i=0;i<count&&i<128;++i){SIZE_T size=0;q->GetMessage(i,nullptr,&size);if(size>65536)continue;std::vector<char> buffer(size);auto msg=reinterpret_cast<D3D11_MESSAGE*>(buffer.data());if(SUCCEEDED(q->GetMessage(i,msg,&size)))fprintf(f,"%u %s\n",unsigned(msg->Severity),msg->pDescription);}}fclose(f);
}};
int wmain(int argc,wchar_t** argv){
 if(argc!=4||!LoadLibraryW(argv[1]))return 2;
 const bool range=wcscmp(argv[3],L"range")==0,missing=wcscmp(argv[3],L"missing")==0;
 HWND w=CreateWindowExW(0,L"STATIC",L"D18 numeric fixture",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!w)return 3;
 DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=128;sd.BufferDesc.Height=128;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=1;sd.OutputWindow=w;sd.Windowed=TRUE;
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;ComPtr<IDXGISwapChain> swap;D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0,actual{};
 if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,&level,1,D3D11_SDK_VERSION,&sd,&swap,&d,&actual,&c))||actual!=level)return 4;
 DebugDump debug{d};
 ComPtr<ID3D11DeviceContext1> c1;c.As(&c1);if(range&&!c1)return 5;
 std::ifstream f(argv[2],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});ComPtr<ID3D11PixelShader> ps;
 if(bytes.empty()||FAILED(d->CreatePixelShader(bytes.data(),bytes.size(),nullptr,&ps)))return 6;
 const char vsSource[]="struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;}; O main(uint id:SV_VertexID){O o;o.p=float4(0,0,0,1);o.uv=float2(0.5,0.5);return o;}";
 ComPtr<ID3DBlob> vsCode,vsErrors;ComPtr<ID3D11VertexShader> vertex;
 if(FAILED(D3DCompile(vsSource,sizeof(vsSource)-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vsCode,&vsErrors))||FAILED(d->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vertex)))return 28;
 c->VSSetShader(vertex.Get(),nullptr,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 D3D11_VIEWPORT vp{};vp.Width=vp.Height=128;vp.MaxDepth=1;c->RSSetViewports(1,&vp);
 ComPtr<ID3D11Texture2D> outputTextures[2];ComPtr<ID3D11RenderTargetView> outputViews[2];
 for(UINT i=0;i<2;++i){D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=128;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R10G10B10A2_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
  if(FAILED(d->CreateTexture2D(&td,nullptr,&outputTextures[i]))||FAILED(d->CreateRenderTargetView(outputTextures[i].Get(),nullptr,&outputViews[i])))return 29;
 }ID3D11RenderTargetView* rt[]={outputViews[0].Get(),outputViews[1].Get()};c->OMSetRenderTargets(2,rt,nullptr);
 ComPtr<ID3D11Texture2D> textures[3];ComPtr<ID3D11ShaderResourceView> views[3];
 const DXGI_FORMAT formats[]={DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
 const UINT pixels[]={256u|(512u<<10)|(768u<<20)|(1u<<30),0x3f400000,0xa4002800};
 for(UINT i=0;i<3;++i){std::vector<UINT> data(128*128,pixels[i]);D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=128;td.MipLevels=td.ArraySize=1;td.Format=formats[i];td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA init{data.data(),128*4,0};
  if(FAILED(d->CreateTexture2D(&td,&init,&textures[i]))||FAILED(d->CreateShaderResourceView(textures[i].Get(),nullptr,&views[i])))return 7;
 }
 const UINT slots[]={0,2,5};for(UINT i=0;i<3;++i){auto p=views[i].Get();if(missing&&i==2)p=nullptr;c->PSSetShaderResources(slots[i],1,&p);}
 for(UINT slot:{1u,3u,4u}){auto p=views[0].Get();c->PSSetShaderResources(slot,1,&p);}
 std::vector<float> cbData[3];cbData[0].resize(range?1024:752);cbData[1].resize(252);cbData[2].resize(range?192:80);
 const UINT firsts[]={range?16u:0u,0,range?16u:0u};const UINT counts[]={192,64,32};
 cbData[0][firsts[0]*4+160*4]=1.0f;
 auto& temporal=cbData[2];const auto base=firsts[2]*4;
 temporal[base+11*4]=temporal[base+11*4+1]=1.0f/128;temporal[base+11*4+2]=temporal[base+11*4+3]=128;
 temporal[base+12*4]=0.25f;temporal[base+12*4+1]=-0.25f;temporal[base+12*4+2]=0.001f;temporal[base+12*4+3]=-0.002f;
 for(UINT i=0;i<4;++i)temporal[base+(13+i)*4+i]=1;
 ComPtr<ID3D11Buffer> buffers[3];const UINT cbSlots[]={1,2,5};
 for(UINT i=0;i<3;++i){D3D11_BUFFER_DESC bd{};bd.ByteWidth=static_cast<UINT>(cbData[i].size()*4);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA init{cbData[i].data(),0,0};if(FAILED(d->CreateBuffer(&bd,&init,&buffers[i])))return 8;
  auto p=buffers[i].Get();if(range)c1->PSSetConstantBuffers1(cbSlots[i],1,&p,&firsts[i],&counts[i]);else c->PSSetConstantBuffers(cbSlots[i],1,&p);
 }
 ComPtr<ID3D11SamplerState> sampler;D3D11_SAMPLER_DESC samplerDesc{};samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;
 if(FAILED(d->CreateSamplerState(&samplerDesc,&sampler)))return 9;auto sp=sampler.Get();c->PSSetSamplers(10,1,&sp);c->PSSetSamplers(12,1,&sp);
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
 ComPtr<ID3D11Predicate> predicate;D3D11_QUERY_DESC pred{D3D11_QUERY_OCCLUSION_PREDICATE,0};if(FAILED(d->CreatePredicate(&pred,&predicate)))return 13;c->Begin(predicate.Get());c->End(predicate.Get());c->SetPredication(predicate.Get(),TRUE);
 if(FAILED(swap->Present(0,0)))return 14;
 for(unsigned frame=0;frame<12;++frame){
  // Establish the exact pre-Draw state after Present/overlay activity.
  c->VSSetShader(vertex.Get(),nullptr,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->RSSetViewports(1,&vp);c->OMSetRenderTargets(2,rt,nullptr);
  for(UINT i=0;i<3;++i){auto p=views[i].Get();if(missing&&i==2)p=nullptr;c->PSSetShaderResources(slots[i],1,&p);auto b=buffers[i].Get();if(range)c1->PSSetConstantBuffers1(cbSlots[i],1,&b,&firsts[i],&counts[i]);else c->PSSetConstantBuffers(cbSlots[i],1,&b);}
  for(UINT slot:{1u,3u,4u}){auto p=views[0].Get();c->PSSetShaderResources(slot,1,&p);}c->PSSetSamplers(10,1,&sp);c->PSSetSamplers(12,1,&sp);
  c->CSSetShader(oldCS.Get(),nullptr,0);c->CSSetShaderResources(0,3,oldViews);c->CSSetSamplers(0,1,&sp);UINT preserve=UINT(-1);c->CSSetUnorderedAccessViews(0,1,&oldRaw,&preserve);
  if(range)c1->CSSetConstantBuffers1(1,2,oldCBs,oldFirst,oldCounts);else c->CSSetConstantBuffers(1,2,oldCBs);c->SetPredication(predicate.Get(),TRUE);
  temporal[base+12*4]=0.25f+static_cast<float>(frame)*0.1f;c->UpdateSubresource(buffers[2].Get(),0,nullptr,temporal.data(),0,0);
  c->PSSetShader(ps.Get(),nullptr,0);c->Draw(0,0);
  ComPtr<ID3D11ComputeShader> csCheck;c->CSGetShader(&csCheck,nullptr,nullptr);if(csCheck.Get()!=oldCS.Get())return 15;
  ComPtr<ID3D11UnorderedAccessView> uvCheck;c->CSGetUnorderedAccessViews(0,1,&uvCheck);if(uvCheck.Get()!=oldUAV.Get())return 16;
  ComPtr<ID3D11Predicate> predCheck;BOOL value=FALSE;c->GetPredication(&predCheck,&value);if(predCheck.Get()!=predicate.Get()||!value)return 17;
  ComPtr<ID3D11SamplerState> sCheck;c->CSGetSamplers(0,1,&sCheck);if(sCheck.Get()!=sampler.Get())return 18;
  for(UINT i=0;i<3;++i){ComPtr<ID3D11ShaderResourceView> v;c->CSGetShaderResources(i,1,&v);if(v.Get()!=oldViews[i])return 19;}
  for(UINT i=0;i<2;++i){ComPtr<ID3D11Buffer> b;UINT first=0,count=0;if(range)c1->CSGetConstantBuffers1(i+1,1,&b,&first,&count);else c->CSGetConstantBuffers(i+1,1,&b);if(b.Get()!=oldCBs[i]||(range&&(first!=oldFirst[i]||count!=oldCounts[i])))return 20;}
  if(FAILED(swap->Present(0,0)))return 21;Sleep(16);
 }
 c->SetPredication(nullptr,FALSE);
 // Fixture-only GPU drain verifies the preserved hidden UAV counter. The probe never flushes/waits.
 ComPtr<ID3D11Buffer> counterGPU,counterCPU;D3D11_BUFFER_DESC cb{};cb.ByteWidth=4;if(FAILED(d->CreateBuffer(&cb,nullptr,&counterGPU)))return 22;cb.Usage=D3D11_USAGE_STAGING;cb.CPUAccessFlags=D3D11_CPU_ACCESS_READ;if(FAILED(d->CreateBuffer(&cb,nullptr,&counterCPU)))return 23;
 c->CopyStructureCount(counterGPU.Get(),0,oldUAV.Get());c->CopyResource(counterCPU.Get(),counterGPU.Get());ComPtr<ID3D11Query> q;D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};if(FAILED(d->CreateQuery(&qd,&q)))return 24;c->End(q.Get());c->Flush();BOOL ready=FALSE;
 for(unsigned i=0;i<1000;++i){if(c->GetData(q.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&ready)break;Sleep(1);}if(!ready)return 25;
 D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->Map(counterCPU.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped)))return 26;UINT actualCounter=*static_cast<UINT*>(mapped.pData);c->Unmap(counterCPU.Get(),0);if(actualCounter!=7)return 27;
 for(unsigned i=0;i<4;++i){swap->Present(0,0);Sleep(16);}std::printf("state_restored=1 uav_counter=%u range=%d\n",actualCounter,int(range));return 0;
}
