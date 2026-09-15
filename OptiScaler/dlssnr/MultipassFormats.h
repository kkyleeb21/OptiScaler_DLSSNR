#pragma once
#include <d3d12.h>
namespace DlssNr::Multipass {
// External colour storage is independent of model/residual storage. Do not
// infer a typeless interpretation without the caller's typed-view contract.
constexpr bool ColorFormat(DXGI_FORMAT format) {
    switch(format){
    case DXGI_FORMAT_R16G16B16A16_FLOAT:case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:return true;
    default:return false;
    }
}
constexpr DXGI_FORMAT ModelFormat(DXGI_FORMAT external,bool multipass){return multipass?DXGI_FORMAT_R16G16B16A16_FLOAT:external;}
inline bool ColorFormatSupported(ID3D12Device* device,DXGI_FORMAT format) {
    if(!device || !ColorFormat(format))return false;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
    if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))))return false;
    constexpr auto first=D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_LOAD|D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
    constexpr auto second=D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD|D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    return (support.Support1&first)==first && (support.Support2&second)==second;
}
}
