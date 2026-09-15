// Test DLL only: corrupt the provided output, then fail. The bridge must retain SR.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <nvsdk_ngx_params.h>
#include <dlssnr/NativeControlAbi.h>
#pragma comment(lib,"d3d11.lib")
static DlssNrNative::Status status;
extern "C" __declspec(dllexport) int D24Configure(const DlssNrNative::Settings*){return 1;}
extern "C" __declspec(dllexport) int D24ReadStatus(DlssNrNative::Status* out){*out=status;return 1;}
extern "C" __declspec(dllexport) int D24Process(void*,ID3D11DeviceContext* c,NVSDK_NGX_Parameter* p,unsigned){
 ID3D11Resource* output=nullptr;
 if(p->Get("Output",&output)!=NVSDK_NGX_Result_Success||!output)return -3;
 Microsoft::WRL::ComPtr<ID3D11Device> d;c->GetDevice(&d);
 Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> view;
 if(FAILED(d->CreateUnorderedAccessView(output,nullptr,&view)))return -5;
 const float poison[]={65504,65504,65504,65504};c->ClearUnorderedAccessViewFloat(view.Get(),poison);
 status.result=-13;status.failed=1;status.tick=GetTickCount64();return -13;
}
