#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <initializer_list>
namespace DlssNr {
// The engine adapter identifies the completed colour and its handoff point.
// The caller unbinds/restores context state and owns synchronization/lifetime.
inline bool SameSizeColourCopyValid(ID3D11DeviceContext* context,ID3D11Texture2D* source,
                                   ID3D11Texture2D* target,UINT width,UINT height){
 if(!context||!source||!target||source==target||!width||!height)return false;
 D3D11_TEXTURE2D_DESC a{},b{};source->GetDesc(&a);target->GetDesc(&b);
 if(a.Format!=b.Format||(a.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&a.Format!=DXGI_FORMAT_R10G10B10A2_UNORM&&a.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT))return false;
 for(const auto& d:{a,b})if(d.Width!=width||d.Height!=height||d.MipLevels!=1||d.ArraySize!=1||d.SampleDesc.Count!=1||d.SampleDesc.Quality||d.Usage!=D3D11_USAGE_DEFAULT||d.CPUAccessFlags||d.MiscFlags||(d.BindFlags&D3D11_BIND_DEPTH_STENCIL))return false;
 if(!(a.BindFlags&D3D11_BIND_SHADER_RESOURCE)||!(b.BindFlags&D3D11_BIND_RENDER_TARGET))return false;
 Microsoft::WRL::ComPtr<ID3D11Device> cd,sd,td;context->GetDevice(&cd);source->GetDevice(&sd);target->GetDevice(&td);
 return cd==sd&&cd==td&&context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE;
}
}
