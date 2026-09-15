#pragma once
#include "NativeControl.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <NVNGX_Parameter.h>

namespace DlssNr {
// Called under the producer's immediate-context transaction, with bindings cleared.
// Work is private until the addon reports completion. The producer fences our final copy.
class NativeNrDx11Bridge {
    using Process = int (*)(void*, ID3D11DeviceContext*, NVSDK_NGX_Parameter*, unsigned);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> privateOutput;
    HMODULE module = nullptr;
    bool attempted = false, failed = false, wasEnabled = false, ownsSession = false;
    D3D11_TEXTURE2D_DESC descriptor{};
    int Reject(int code) { NativeControl::Unavailable(code); wasEnabled=false; return code; }
public:
    int Run(ID3D11DeviceContext* context, NVSDK_NGX_Parameter* parameters, unsigned flags) {
        if (!Config::Instance()->DlssNrEnabled.value_or_default()) {
            wasEnabled = false;
            return 0;
        }
        if (failed) return 0;
        if (!context || !parameters || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return Reject(-2);
        if (!attempted) {
            attempted = true;
            HMODULE core = nullptr; wchar_t path[32768]{};
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&NativeControl::Read), &core) && GetModuleFileNameW(core,path,32768)) {
                const auto addon=std::filesystem::path(path).parent_path()/L"D24Native.dll";
                module=LoadLibraryExW(addon.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
            }
        }
        if (!module) { NativeControl::Unavailable(-100); failed=true; return -100; }
        const auto process=reinterpret_cast<Process>(GetProcAddress(module,"D24Process"));
        if (!process || !NativeControl::Apply(module)) { NativeControl::Unavailable(-101); failed=true; return -101; }
        ID3D11Resource* original=nullptr;
        if(parameters->Get(NVSDK_NGX_Parameter_Output,&original)!=NVSDK_NGX_Result_Success || !original) return Reject(-3);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if(FAILED(original->QueryInterface(IID_PPV_ARGS(&texture)))) return Reject(-4);
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        Microsoft::WRL::ComPtr<ID3D11Device> device,resourceDevice;
        context->GetDevice(&device);texture->GetDevice(&resourceDevice);
        if(device!=resourceDevice) return Reject(-7);
        if(desc.MipLevels!=1 || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 || desc.SampleDesc.Quality ||
           desc.Usage!=D3D11_USAGE_DEFAULT || desc.CPUAccessFlags || desc.MiscFlags ||
           !(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE) || !(desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS)) return Reject(-5);
        if(privateOutput) { Microsoft::WRL::ComPtr<ID3D11Device> previous; privateOutput->GetDevice(&previous); if(previous!=device) return Reject(-7); }
        if(!privateOutput || desc.Width!=descriptor.Width || desc.Height!=descriptor.Height || desc.Format!=descriptor.Format) {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> replacement;
            if(FAILED(device->CreateTexture2D(&desc,nullptr,&replacement))) { NativeControl::Unavailable(-26);failed=true;return -26; }
            privateOutput=replacement;descriptor=desc;wasEnabled=false;
        }
        // Restore the producer parameter block even on an early return.
        unsigned oldReset=0;parameters->Get(NVSDK_NGX_Parameter_Reset,&oldReset);
        struct Restore {
            NVSDK_NGX_Parameter* p;ID3D11Resource* output;unsigned reset;
            ~Restore(){p->Set(NVSDK_NGX_Parameter_Output,output);p->Set(NVSDK_NGX_Parameter_Reset,reset);}
        } restore{parameters,original,oldReset};
        context->CopyResource(privateOutput.Get(),texture.Get());
        parameters->Set(NVSDK_NGX_Parameter_Output,static_cast<ID3D11Resource*>(privateOutput.Get()));
        if(!wasEnabled) parameters->Set(NVSDK_NGX_Parameter_Reset,1u);
        const int result=process(this,context,parameters,flags);
        NativeControl::Observe(module);
        wasEnabled=true;ownsSession=true;
        if(result==1 && SUCCEEDED(device->GetDeviceRemovedReason())) {
            context->CopyResource(texture.Get(),privateOutput.Get());
            return 1;
        }
        if(result<0) failed=true; // Retain private resources on an uncertain GPU failure.
        return result;
    }
    void ReleaseAfterDrain() {
        if(failed || !module || !ownsSession) return;
        const auto release=reinterpret_cast<int(*)(void*)>(GetProcAddress(module,"D24Release"));
        if(release && release(this)==1) { privateOutput.Reset();ownsSession=false;wasEnabled=false; }
    }
};
}
