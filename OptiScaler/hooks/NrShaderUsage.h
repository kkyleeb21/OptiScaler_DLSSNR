#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl/client.h>

namespace NrState {
struct ShaderUsage { HRESULT result=E_FAIL; bool known=false; UINT samplers=0; UINT resources=0; };
inline ShaderUsage InspectDxil(const void* bytes,size_t size) {
    ShaderUsage usage;
    if(!bytes || !size || size>UINT32_MAX) {usage.result=E_INVALIDARG;return usage;}
    // Resolve relative to this module, never the current directory or a developer's SDK.
    // Installer mirrors this private dependency alongside both supported module locations.
    static HMODULE compiler=[]() -> HMODULE {
        HMODULE self=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(&InspectDxil), &self)) return nullptr;
        wchar_t path[32768]{};
        const DWORD count=GetModuleFileNameW(self,path,32768);
        if(!count || count>=32768) return nullptr;
        std::wstring full(path,count);
        const auto slash=full.find_last_of(L"\\/");
        if(slash==std::wstring::npos) return nullptr;
        full.resize(slash+1);
        full+=L"OptiScaler\\D18\\dxcompiler.dll";
        return LoadLibraryExW(full.c_str(),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    }();
    if(!compiler) {usage.result=HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);return usage;}
    auto create=reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(compiler,"DxcCreateInstance"));
    if(!create) {usage.result=E_NOINTERFACE;return usage;}
    Microsoft::WRL::ComPtr<IDxcUtils> utils;
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> blob;
    Microsoft::WRL::ComPtr<IDxcContainerReflection> container;
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
    auto& hr=usage.result;
    hr=create(CLSID_DxcUtils,IID_PPV_ARGS(&utils)); if(FAILED(hr))return usage;
    hr=utils->CreateBlob(bytes,static_cast<UINT32>(size),0,&blob); if(FAILED(hr))return usage;
    hr=create(CLSID_DxcContainerReflection,IID_PPV_ARGS(&container)); if(FAILED(hr))return usage;
    hr=container->Load(blob.Get()); if(FAILED(hr))return usage;
    UINT32 index=0;
    hr=container->FindFirstPartKind(0x4c495844,&index); if(FAILED(hr))return usage; // DXIL
    hr=container->GetPartReflection(index,IID_PPV_ARGS(&reflection)); if(FAILED(hr))return usage;
    D3D12_SHADER_DESC desc{};
    hr=reflection->GetDesc(&desc); if(FAILED(hr))return usage;
    if(D3D12_SHVER_GET_TYPE(desc.Version)!=D3D12_SHVER_COMPUTE_SHADER) {hr=E_INVALIDARG;return usage;}
    usage.resources=desc.BoundResources;
    for(UINT i=0;i<desc.BoundResources;++i) {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        hr=reflection->GetResourceBindingDesc(i,&binding); if(FAILED(hr))return usage;
        if(binding.Type==D3D_SIT_SAMPLER) ++usage.samplers;
    }
    usage.known=true;return usage;
}
}
