#pragma once
#include <d3d11.h>
#include <wrl/client.h>
namespace DlssNr {
struct Dx11ReplaySurface {
 Microsoft::WRL::ComPtr<ID3D11Resource> original;
 Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
 Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view,originalView;
 Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
 UINT w=0,h=0,ow=0,oh=0;
 UINT64 epoch=~0ULL;
};
}
