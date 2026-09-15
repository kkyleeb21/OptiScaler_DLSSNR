// Bounded WARP regression for the project's real ImGui DX11 backend.
// raw mirrors the overlay call site; isolated evaluates a full-context guard.
#include <windows.h>
#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
#ifdef D18_UI_GUARD_PRODUCTION
#include <menu/Dx11UiState.h>
#endif
#include "imgui.h"
#include "imgui_impl_dx11.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
int main(int argc,char** argv){
 const bool isolated=argc>1&&strcmp(argv[1],"isolated")==0;
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;
 D3D_FEATURE_LEVEL fl=argc>2&&strcmp(argv[2],"11.1")==0?D3D_FEATURE_LEVEL_11_1:D3D_FEATURE_LEVEL_11_0,actual{};
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,&fl,1,D3D11_SDK_VERSION,&d,&actual,&c)))return 2;
 ComPtr<ID3D11Device1> d1;ComPtr<ID3D11DeviceContext1> c1;
 if(FAILED(d.As(&d1))||FAILED(c.As(&c1)))return 3;
 ComPtr<ID3DDeviceContextState> own,saved;
 if(FAILED(d1->CreateDeviceContextState(0,&fl,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&actual,&own)))return 4;
 ComPtr<ID3D11Texture2D> tex[3];ComPtr<ID3D11RenderTargetView> rt[3];
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=128;td.ArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
 for(int i=0;i<3;++i)if(FAILED(d->CreateTexture2D(&td,nullptr,&tex[i]))||FAILED(d->CreateRenderTargetView(tex[i].Get(),nullptr,&rt[i])))return 5;
 const char code[]="[numthreads(1,1,1)] void main(uint3 p:SV_DispatchThreadID){}";
 ComPtr<ID3DBlob> blob,error;ComPtr<ID3D11ComputeShader> cs;
 if(FAILED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&blob,&error))||FAILED(d->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&cs)))return 6;
 ComPtr<ID3D11Buffer> cb;D3D11_BUFFER_DESC bd{};bd.ByteWidth=1024;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 if(FAILED(d->CreateBuffer(&bd,nullptr,&cb)))return 7;
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(128,128);io.DeltaTime=1.0f/60;
 if(!ImGui_ImplDX11_Init(d.Get(),c.Get()))return 8;
 ID3D11RenderTargetView* original[]={rt[0].Get(),rt[1].Get()};c->OMSetRenderTargets(2,original,nullptr);
 c->CSSetShader(cs.Get(),nullptr,0);auto rawCB=cb.Get();UINT first=16,count=16;c1->VSSetConstantBuffers1(0,1,&rawCB,&first,&count);
#ifdef D18_UI_GUARD_PRODUCTION
 D18InputProbe::boundContext=c.Get();D18InputProbe::boundCompute=cs.Get();D18InputProbe::computeContext=c.Get();
 Dx11UiState owner;std::unique_ptr<Dx11UiState::Scope> isolatedState;
 if(isolated){isolatedState=std::make_unique<Dx11UiState::Scope>(owner,c.Get());if(!*isolatedState)return 11;Dx11UiState::Scope nested(owner,c.Get());if(nested||nested.Result()!=E_PENDING)return 13;}
#else
 if(isolated){c1->SwapDeviceContextState(own.Get(),&saved);c->ClearState();}
#endif
 ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
 ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0),ImVec2(100,100),IM_COL32(255,128,0,255));ImGui::Render();
 const int vertices=ImGui::GetDrawData()->TotalVtxCount;
 auto overlayRT=rt[2].Get();c->OMSetRenderTargets(1,&overlayRT,nullptr);
 ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#ifdef D18_UI_GUARD_PRODUCTION
 // Model the hook cache mutations caused by internal SetShader calls.
 D18InputProbe::boundContext=nullptr;D18InputProbe::boundCompute=nullptr;D18InputProbe::computeContext=nullptr;
 isolatedState.reset();
 if(isolated&&(D18InputProbe::boundContext!=c.Get()||D18InputProbe::boundCompute!=cs.Get()||D18InputProbe::computeContext!=c.Get()||D18InputProbe::internalWork))return 12;
#else
 if(isolated){c->ClearState();c1->SwapDeviceContextState(saved.Get(),nullptr);}
#endif
 ComPtr<ID3D11ComputeShader> afterCS;c->CSGetShader(&afterCS,nullptr,nullptr);
 ID3D11RenderTargetView* afterRT[2]{};c->OMGetRenderTargets(2,afterRT,nullptr);
 const bool targets=afterRT[0]==rt[0].Get()&&afterRT[1]==rt[1].Get();for(auto p:afterRT)if(p)p->Release();
 ComPtr<ID3D11Buffer> afterCB;UINT afterFirst=0,afterCount=0;c1->VSGetConstantBuffers1(0,1,&afterCB,&afterFirst,&afterCount);
 const bool range=afterCB==cb&&afterFirst==first&&afterCount==count;
 ComPtr<ID3D11Query> end;D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};if(FAILED(d->CreateQuery(&qd,&end)))return 9;c->End(end.Get());c->Flush();
 BOOL ready=FALSE;const auto start=GetTickCount64();HRESULT hr=S_FALSE;
 while((hr=c->GetData(end.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH))==S_FALSE&&GetTickCount64()-start<2000)Sleep(1);
 ComPtr<ID3D11InfoQueue> info;unsigned errors=0;
 if(SUCCEEDED(d.As(&info)))for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<char> bytes(size);auto msg=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());if(SUCCEEDED(info->GetMessage(i,msg,&size))&&msg->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){++errors;fprintf(stderr,"%s\n",msg->pDescription);}}
 const bool compute=afterCS==cs,drained=hr==S_OK&&ready;
 printf("isolated=%d warp=1 feature_level=%x vertices=%d cs_preserved=%d targets_preserved=%d vs_range_preserved=%d first=%u count=%u drained=%d debug_errors=%u removed_reason=%08lx\n",isolated,unsigned(actual),vertices,compute,targets,range,afterFirst,afterCount,drained,errors,static_cast<unsigned long>(d->GetDeviceRemovedReason()));
 ImGui_ImplDX11_Shutdown(false);ImGui::DestroyContext();
 return vertices>0&&compute&&targets&&range&&drained&&!errors?0:10;
}
