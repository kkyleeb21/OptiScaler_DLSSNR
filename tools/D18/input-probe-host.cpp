#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <chrono>
#include <cwchar>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"d3dcompiler.lib")
int wmain(int argc,wchar_t** argv){
 if(argc!=3&&argc!=4)return 2;
 HMODULE proxy=LoadLibraryW(argv[1]);if(!proxy)return 2;
 const bool quotaMode=argc==4&&wcscmp(argv[3],L"--quota")==0;
 const bool computeMode=(argc==4&&wcsstr(argv[3],L"compute")!=nullptr)||quotaMode;
 const bool earlyMode=argc==4&&wcscmp(argv[3],L"--early")==0;
 const bool sweepMode=argc==4&&wcscmp(argv[3],L"--sweeps")==0;
 // A capability-probe device must not claim the observer before the display device.
 ID3D11Device* probe=nullptr;D3D_FEATURE_LEVEL probeLevel=D3D_FEATURE_LEVEL_10_0;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&probeLevel,1,D3D11_SDK_VERSION,&probe,nullptr,nullptr)))return 9;
 probe->Release();
 ID3D11Device* d=nullptr;ID3D11DeviceContext* c=nullptr;D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0,actual{};
 HWND window=CreateWindowExW(0,L"STATIC",L"D18 isolated input fixture",WS_OVERLAPPEDWINDOW,0,0,128,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 if(!window)return 10;
 DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=128;sd.BufferDesc.Height=64;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=1;sd.OutputWindow=window;sd.Windowed=TRUE;
 IDXGISwapChain* swapchain=nullptr;
 ID3D11PixelShader* ps=nullptr;ID3D11VertexShader* vs=nullptr;
 if(earlyMode){
  sd.BufferDesc.Height=128; // Factory path treats sub-100 surfaces as auxiliary overlays.
  if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&d,&actual,&c)))return 20;
  std::ifstream input(argv[2],std::ios::binary);std::vector<char> earlyCode((std::istreambuf_iterator<char>(input)),{});
  if(earlyCode.empty()||FAILED(d->CreatePixelShader(earlyCode.data(),earlyCode.size(),nullptr,&ps)))return 21;
  const char source[]="struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;float3 a:TEXCOORD1;float3 b:TEXCOORD2;float4 c:TEXCOORD3;};O main(uint id:SV_VertexID){O o=(O)0;o.p=float4(id,0,0,1);return o;}";
  ID3DBlob* blob=nullptr;ID3DBlob* errors=nullptr;HRESULT hr=D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&blob,&errors);
  if(errors)errors->Release();if(FAILED(hr))return 22;
  hr=d->CreateVertexShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&vs);blob->Release();if(FAILED(hr))return 23;
  IDXGIFactory* factory=nullptr;
  using FactoryFn=HRESULT(WINAPI*)(UINT,REFIID,void**);
  const auto makeFactory=reinterpret_cast<FactoryFn>(GetProcAddress(proxy,"CreateDXGIFactory2"));
  if(!makeFactory||FAILED(makeFactory(0,IID_PPV_ARGS(&factory))))return 24;
  hr=factory->CreateSwapChain(d,&sd,&swapchain);factory->Release();if(FAILED(hr))return 25;
 }else if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&sd,&swapchain,&d,&actual,&c))||actual!=level)return 3;
 if(wcscmp(argv[2],L"--benchmark")==0){
  const auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<500000;++i)c->Draw(0,0);
  const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
  std::printf("empty_draws=500000 elapsed_us=%lld\n",static_cast<long long>(us));return 0;
 }
 std::ifstream f(argv[2],std::ios::binary);std::vector<char> code((std::istreambuf_iterator<char>(f)),{});
 if(argc==4&&wcscmp(argv[3],L"--unknown")==0){
  const char source[]="float4 main():SV_Target {return float4(0.1,0.2,0.3,1);}";
  ID3DBlob* blob=nullptr;ID3DBlob* errors=nullptr;
  HRESULT compiled=D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&blob,&errors);
  if(errors)errors->Release();if(FAILED(compiled))return 12;
  const auto bytes=static_cast<const char*>(blob->GetBufferPointer());code.assign(bytes,bytes+blob->GetBufferSize());blob->Release();
 }
 if(!ps&&(code.empty()||FAILED(d->CreatePixelShader(code.data(),code.size(),nullptr,&ps))))return 4;
 if(!vs){const char src[]="struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;float3 a:TEXCOORD1;float3 b:TEXCOORD2;float4 c:TEXCOORD3;};O main(uint id:SV_VertexID){O o=(O)0;o.p=float4(id,0,0,1);return o;}";ID3DBlob* blob=nullptr;ID3DBlob* errors=nullptr;const auto hr=D3DCompile(src,sizeof(src)-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&blob,&errors);if(errors)errors->Release();if(FAILED(hr))return 30;auto made=d->CreateVertexShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&vs);blob->Release();if(FAILED(made))return 31;}
 ID3D11DeviceContext* immediate=c;
 if(argc==4&&wcsstr(argv[3],L"deferred")!=nullptr){if(FAILED(d->CreateDeferredContext(0,&c)))return 14;}
 if(argc==4&&wcscmp(argv[3],L"--untagged")==0){
  const GUID actualTag={0x7fcd24a8,0x4c72,0x4ecd,{0x9e,0xa1,0x93,0x4b,0x0d,0x47,0xd6,0x28}};
  if(FAILED(ps->SetPrivateData(actualTag,0,nullptr)))return 13;
 }
 ID3D11Texture2D* texture=nullptr;D3D11_TEXTURE2D_DESC desc{};desc.Width=128;desc.Height=64;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R32_FLOAT;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 std::vector<float> initialPixels(128*64,0.5f);D3D11_SUBRESOURCE_DATA initialTex{initialPixels.data(),128*4,0};if(FAILED(d->CreateTexture2D(&desc,&initialTex,&texture)))return 5;
 ID3D11ShaderResourceView* view=nullptr;if(FAILED(d->CreateShaderResourceView(texture,nullptr,&view)))return 6;
 ID3D11ShaderResourceView* views[]={view,view,view};c->PSSetShaderResources(0,3,views);
 c->PSSetShaderResources(127,1,&view);
 c->CSSetShaderResources(0,3,views);c->CSSetShaderResources(127,1,&view);
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=4096;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ID3D11Buffer* buffer=nullptr;
 float constants[1024]{};constants[160*4]=1;constants[6*4+1]=1;constants[7*4+3]=1;D3D11_SUBRESOURCE_DATA initialBuffer{constants,0,0};if(FAILED(d->CreateBuffer(&bd,&initialBuffer,&buffer)))return 7;
 for(UINT slot:{0u,1u,2u,5u,13u}){c->PSSetConstantBuffers(slot,1,&buffer);c->CSSetConstantBuffers(slot,1,&buffer);}
 ID3D11Texture2D* target=nullptr;ID3D11RenderTargetView* rtv=nullptr;
 desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
 if(FAILED(d->CreateTexture2D(&desc,nullptr,&target))||FAILED(d->CreateRenderTargetView(target,nullptr,&rtv)))return 15;
 c->OMSetRenderTargets(1,&rtv,nullptr);
 D3D11_VIEWPORT vp{};vp.Width=128;vp.Height=64;vp.MaxDepth=1;c->RSSetViewports(1,&vp);
 ID3D11ComputeShader* computeShader=nullptr;
 if(computeMode){
  const char source[]="RWTexture2D<float> dst:register(u0); [numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){dst[id.xy]=0.5;}";
  ID3DBlob* blob=nullptr;ID3DBlob* errors=nullptr;
  HRESULT compiled=D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&blob,&errors);
  if(errors)errors->Release();if(FAILED(compiled))return 17;
  HRESULT made=d->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&computeShader);blob->Release();if(FAILED(made))return 18;
  ID3D11Texture2D* outputTexture=nullptr;ID3D11UnorderedAccessView* uav=nullptr;desc.Format=DXGI_FORMAT_R32_FLOAT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
  if(FAILED(d->CreateTexture2D(&desc,nullptr,&outputTexture))||FAILED(d->CreateUnorderedAccessView(outputTexture,nullptr,&uav)))return 19;
  c->CSSetUnorderedAccessViews(0,1,&uav,nullptr);uav->Release();outputTexture->Release();
 }
 std::vector<ID3D11PixelShader*> quotaShaders;
 if(quotaMode){for(unsigned i=0;i<50;++i){
  char source[128]{};sprintf_s(source,"float4 main():SV_Target {return float4(%u.0,0,0,1);}",i);
  ID3DBlob* blob=nullptr;ID3DBlob* errors=nullptr;const auto hr=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&blob,&errors);if(errors)errors->Release();if(FAILED(hr))return 27;
  ID3D11PixelShader* q=nullptr;const auto made=d->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&q);blob->Release();if(FAILED(made))return 28;quotaShaders.push_back(q);
 }}
 ID3D11SamplerState* sampler=nullptr;D3D11_SAMPLER_DESC sm{};sm.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sm.AddressU=sm.AddressV=sm.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sm.MaxLOD=D3D11_FLOAT32_MAX;if(FAILED(d->CreateSamplerState(&sm,&sampler)))return 32;
 auto bindGraphics=[&](){c->VSSetShader(vs,nullptr,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->OMSetRenderTargets(1,&rtv,nullptr);c->RSSetViewports(1,&vp);c->PSSetShaderResources(0,3,views);c->PSSetShaderResources(127,1,&view);c->PSSetSamplers(10,1,&sampler);for(UINT slot:{0u,1u,2u,5u,13u})c->PSSetConstantBuffers(slot,1,&buffer);};
 if(FAILED(swapchain->Present(0,0)))return 11;
 bindGraphics();
 if(quotaMode){for(auto q:quotaShaders){c->PSSetShader(q,nullptr,0);c->Draw(0,0);q->Release();}}
 if(vs)c->VSSetShader(vs,nullptr,0);
 if(computeMode){c->CSSetShader(computeShader,nullptr,0);c->Dispatch(1,1,1);c->Dispatch(1,1,1);c->Dispatch(1,1,1);}
 else {c->PSSetShader(ps,nullptr,0);c->Draw(0,0);c->Draw(0,0);c->Draw(0,0);}
 if(sweepMode){for(unsigned frame=0;frame<2;++frame){if(FAILED(swapchain->Present(0,0)))return 26;bindGraphics();c->PSSetShader(ps,nullptr,0);c->Draw(0,0);c->Draw(0,0);c->Draw(0,0);}if(FAILED(swapchain->Present(0,0)))return 26;}
 ID3D11ShaderResourceView* after=nullptr;c->PSGetShaderResources(1,1,&after);bool unchanged=after==view;if(after)after->Release();
 if(c!=immediate){ID3D11CommandList* list=nullptr;if(FAILED(c->FinishCommandList(FALSE,&list)))return 16;immediate->ExecuteCommandList(list,FALSE);list->Release();}
 // Drain the WARP fixture before exit; this is not a live-game rendering path.
 ID3D11Query* end=nullptr;D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};if(FAILED(d->CreateQuery(&qd,&end)))return 33;immediate->End(end);immediate->Flush();BOOL done=FALSE;auto until=GetTickCount64()+2000;while(GetTickCount64()<until){if(immediate->GetData(end,&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&done)break;Sleep(1);}end->Release();if(!done||FAILED(d->GetDeviceRemovedReason()))return 34;
 std::printf("fixture_draw_completed bindings_unchanged=%d warp=1 drained=1\n",int(unchanged));return unchanged?0:8;
}
