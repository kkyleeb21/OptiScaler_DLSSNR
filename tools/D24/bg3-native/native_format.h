#pragma once
// Typed colour contract shared by all native DX11 SR -> NR entry points.
inline bool nativeColourFormat(DXGI_FORMAT format){
    switch(format){
    case DXGI_FORMAT_R16G16B16A16_FLOAT:case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:return true;
    default:return false;
    }
}
inline bool nativeColourSupport(ID3D11Device* device,DXGI_FORMAT format){
    UINT support=0;
    if(FAILED(device->CheckFormatSupport(format,&support)))return false;
    const UINT required=D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_SHADER_LOAD|D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
    return (support&required)==required;
}
